#include <common.h>
#include <string.h>
#include <limits.h>
#include <log.h>
#include <heap.h>

#include "dynamic_array.h"
#include "api.h"
#include "region_internal.h"
#include "clipdraw_internal.h"

static void ExpandToFit(struct region* rgn, int extra_bytes) {
    rgn->used_length += extra_bytes;
    while (rgn->used_length >= rgn->allocated_length) {
        bool power2 = ((rgn->allocated_length) & (rgn->allocated_length << 1)) == 0;
        if (rgn->allocated_length < 512) {
            rgn->allocated_length *= 2;
        } else if (power2) {
            rgn->allocated_length += rgn->allocated_length >> 1;
        } else {
            rgn->allocated_length /= 3;
            rgn->allocated_length *= 4;
        }
        rgn->data = ReallocHeap(rgn->data, rgn->allocated_length);
    }
}

void BuildNewRegion(struct region* rgn, int16_t y0, struct region_build_context* ctxt) {
    rgn->used_length = sizeof(struct region_data);
    rgn->allocated_length = 32;
    rgn->data = AllocHeap(rgn->allocated_length);
    
    struct region_data* data = rgn->data;
    data->num_bands = 0;
    data->trans_x = 0;
    data->trans_y = 0;

    ctxt->prev_y1 = y0;
    ctxt->current_points = NULL;
}

/*
 * This should be the only function ever used to generate regions. We need
 * to ensure all the regions are canonical, i.e. there's only ever one
 * representation. This includes choosing the right encoding mode at all cases
 * (e.g. if you can use small/compact mode, you must!).
 */
void AddScanline(
    struct region* rgn,
    struct region_build_context* ctxt,
    int num_spans,
    int16_t* x_points,
    bool known_to_be_same_as_prev
                       // for optimisation when you call this in a loop on the 
                       // same data. will work 100% the same way if you just
                       // always set to false, but slightly slower.
                       // (skips a memcmp if set)
) {
    int new_span_bytes = sizeof(int16_t) * 2 * num_spans;

    /* 
     * Make sure the x spans are canonical. (i.e. merge any adjacent ones)
     */
    if (num_spans > 1) {
        /* TODO: ! do you want to allow out of order spans here or not ? */
        /* definitely check for adjacent spans and merge them though */
    }
    
    if (ctxt->current_points == NULL) {
        /* 
         * This is the first time we're calling this on the region.
         */
        ctxt->current_ptr = 0;

        if (num_spans == -1) {
            /* You've made the empty region! */
            return;
        }

        if (num_spans == 0) {
            ctxt->prev_y1++;
            return;
        }

        ctxt->count0x7F = 0;
        ctxt->current_spans = num_spans;
        ctxt->current_points = AllocHeap(new_span_bytes);
        ctxt->current_height = 1;
        memcpy(ctxt->current_points, x_points, new_span_bytes);
        return;
    }

    if (ctxt->current_spans == num_spans) {
        if (ctxt->current_spans == 0 
            || known_to_be_same_as_prev 
            || !memcmp(ctxt->current_points, x_points, new_span_bytes)) {
            /* 
             * This scanline is the same as the previous one!
             */
            if (ctxt->current_height < 32000) {
                ctxt->current_height++;
                return;
            } else {
                /* 
                 * Fall through and pretend it is different.
                 */
            }
        }
    }

    /* 
     * We've got a new scanline! We must save the old one first!
     */
    
    /* 
     * Check if we can use small mode!
     */
    if (ctxt->current_spans == ctxt->prev_spans 
        && ctxt->current_spans != 0     /* we add a gap on empty spans, breaking the prev_y1 + 1 restriction on small mode */
        && ctxt->current_height <= 8
        && ctxt->current_spans <= 15
        && ((struct region_data*) rgn->data)->num_bands > 0          /* can't use small mode on first band */
    ) {
        /* 
         * We've got the same number of spans as the previous time. So it's
         * possible that we can encode it in small mode.
         */
        bool possible = true;
        for (int i = 0; i < ctxt->current_spans; ++i) {
            int16_t old_start = ctxt->prev_x0[i];
            int16_t old_end   = ctxt->prev_x1[i];
            int16_t new_start = ctxt->current_points[i * 2 + 0];
            int16_t new_end   = ctxt->current_points[i * 2 + 1];

            int16_t diff_start = new_start - old_start;
            int16_t diff_end   = new_end - old_end;
            
            if (diff_start < -8 || diff_start > 7) {
                possible = false;
                break;
            }
            if (diff_end < -8 || diff_end > 7) {
                possible = false;
                break;
            }
        }

        if (possible) {
            /*
             * We can encode in `small` mode!
             */

            uint8_t small_output[16];
            int small_ptr = 0;

            uint8_t ctrl_byte = (uint8_t) (ctxt->current_spans | ((ctxt->current_height - 1) << 4));

            /* 
             * We use 0x7F as the special 'repeat previous' indicator, so don't
             * let that be encoded normally. (0x7F would be otherwise be 
             * generated from 15 spans and height of 8, which is hopefully 
             * quite rare).
             * 
             * Same for 0x7E. We use 0x7E to mean 'a run of 0x7F' (next byte
             * gives run count).
             */
            if (ctrl_byte == 0x7F || ctrl_byte == 0x7E) {
                goto regular_mode;
            }
            small_output[small_ptr++] = ctrl_byte;

            for (int i = 0; i < ctxt->current_spans; ++i) {
                int adj_start = ctxt->current_points[i * 2 + 0] - ctxt->prev_x0[i];
                int adj_end   = ctxt->current_points[i * 2 + 1] - ctxt->prev_x1[i];
        
                uint8_t start_part = (uint8_t) (adj_start + 8);
                uint8_t end_part = (uint8_t) (adj_end + 8);
                uint8_t adj_byte = (uint8_t) ((end_part << 4) | start_part);

                small_output[small_ptr++] = adj_byte;
            }

            if (small_ptr == ctxt->prev_small_output_len 
                && !memcmp(small_output, ctxt->prev_small_output, small_ptr)) {
                
                /* Same as the previous small output! */

                if (ctxt->count0x7F == 0) {
                    ctxt->count0x7F = 1;

                    ExpandToFit(rgn, 1);
                    uint8_t* ptr = ((struct region_data*) rgn->data)->band_data;
                    ptr[ctxt->current_ptr++] = 0x7F;

                } else if (ctxt->count0x7F == 1) {
                    ctxt->count0x7F++;

                    ExpandToFit(rgn, 1);
                    uint8_t* ptr = ((struct region_data*) rgn->data)->band_data;
                    ptr[ctxt->current_ptr - 1] = 0x7E;
                    ptr[ctxt->current_ptr++] = (uint8_t) (ctxt->count0x7F - 2);

                } else {
                    ctxt->count0x7F++;

                    uint8_t* ptr = ((struct region_data*) rgn->data)->band_data;
                    ptr[ctxt->current_ptr - 1] = (uint8_t) (ctxt->count0x7F - 2);
                    if (ctxt->count0x7F - 2 == 0xFF) {
                        ctxt->count0x7F = 0;
                    }
                }
            
            } else {
                ExpandToFit(rgn, 1 + ctxt->current_spans);
            
                uint8_t* ptr = ((struct region_data*) rgn->data)->band_data;
                memcpy(ptr + ctxt->current_ptr, small_output, small_ptr);
                memcpy(ctxt->prev_small_output, small_output, small_ptr);
                ctxt->prev_small_output_len = small_ptr;
                ctxt->current_ptr += small_ptr;

                ctxt->count0x7F = 0;
            }
            goto done_encoding;
        }
    }

regular_mode:
    /* 
     * Small mode didn't work, so go with regular mode.
     */

    ctxt->count0x7F = 0;

    if (ctxt->current_spans == 0) {
        /* Don't encode a gap! */

    } else {
        /* 
         * See if we can skip the high byte of current spans, and use implict
         * y1 = y0 + 1 to save a few bytes.
         */
        int num_bands = ((struct region_data*) rgn->data)->num_bands;

        /* Don't allow compact mode on first line, to make CreateRectRegion easier */
        bool compact = ctxt->current_spans <= 0x1F && ctxt->current_height == 1 && num_bands > 0;
        bool compact_spans = false;

        uint8_t* ptr;
        
        if (compact) {
            compact_spans = true;
            uint8_t first_span_top_byte = ((uint16_t) ctxt->current_points[0]) >> 8;
            for (int i = 0; i < ctxt->current_spans * 2; ++i) {
                uint8_t other_top_byte = ((uint16_t) ctxt->current_points[i]) >> 8;
                if (other_top_byte != first_span_top_byte) {
                    compact_spans = false;
                    break;
                }
            }

            ExpandToFit(rgn, 3 + ctxt->current_spans * (compact_spans ? 2 : 4) + (compact_spans ? 1 : 0));
            ptr = ((struct region_data*) rgn->data)->band_data;

            ptr[ctxt->current_ptr++] = 0x80 | (compact_spans ? 0x20 : 0) | (ctxt->current_spans & 0x1F);

            int16_t y0 = ctxt->prev_y1;
            memcpy(ptr + ctxt->current_ptr, &y0, 2);
            ctxt->current_ptr += 2;

        } else {
            /* Full regular mode */

            ExpandToFit(rgn, 6 + ctxt->current_spans * 4);
            ptr = ((struct region_data*) rgn->data)->band_data;

            ptr[ctxt->current_ptr++] = 0x80 | 0x40 | (ctxt->current_spans & 0x3F);
            ptr[ctxt->current_ptr++] = (ctxt->current_spans >> 6) & 0xFF;

            int16_t y0 = ctxt->prev_y1;
            int16_t y1 = (int16_t) (y0 + ctxt->current_height);
            memcpy(ptr + ctxt->current_ptr, &y0, 2);
            ctxt->current_ptr += 2;
            memcpy(ptr + ctxt->current_ptr, &y1, 2);
            ctxt->current_ptr += 2;
        }
        
        if (compact_spans) {
            for (int i = 0; i < ctxt->current_spans; ++i) {
                if (i == 0) {
                    ptr[ctxt->current_ptr++] = ((uint16_t) ctxt->current_points[i * 2 + 0]) >> 8;
                    ptr[ctxt->current_ptr++] = ((uint16_t) ctxt->current_points[i * 2 + 0]) & 0xFF;
                } else {
                    ptr[ctxt->current_ptr++] = ((uint16_t) ctxt->current_points[i * 2 + 0]) & 0xFF;
                }
                
                ptr[ctxt->current_ptr++] = ((uint16_t) ctxt->current_points[i * 2 + 1]) & 0xFF;
            }
        } else {
            for (int i = 0; i < ctxt->current_spans; ++i) {
                memcpy(ptr + ctxt->current_ptr, ctxt->current_points + i * 2 + 0, 2);
                ctxt->current_ptr += 2;
                memcpy(ptr + ctxt->current_ptr, ctxt->current_points + i * 2 + 1, 2);
                ctxt->current_ptr += 2;
            }
        }
    }

done_encoding:
    /* 
     * If we got to here, we added a band. (or there's a gap to encode!)
     */
    if (ctxt->current_spans != 0) {
        ((struct region_data*) rgn->data)->num_bands++;

        /* 
         * Got to set prev_x0, prev_x1 and prev_y1. Doesn't need setting if 
         * current_spans == 0, as small mode can't follow a 0-span band.
         * 
         * Stop after 16, lest we overflow the array. But small mode only ever
         * reads up to 16, so we're fine!
         */
        for (int i = 0; i < MIN(16, ctxt->current_spans); ++i) {
            ctxt->prev_x0[i] = ctxt->current_points[i * 2 + 0];
            ctxt->prev_x1[i] = ctxt->current_points[i * 2 + 1];
        }
    }

    ctxt->prev_y1 += ctxt->current_height;
    ctxt->prev_spans = ctxt->current_spans; 

    FreeHeap(ctxt->current_points);

    /* 
     * Used by FinishRegion to flush out the data.
     */
    if (num_spans == -1) {
        return;
    }

    /* 
     * Now we mark down the new scanline we're working on.
     */
    ctxt->current_spans = num_spans;
    // don't allow allocheap(0) - that gives NULL!! and then we go back to
    // 'this is the first scanline'!
    ctxt->current_points = AllocHeap(new_span_bytes + 1);
    ctxt->current_height = 1;
    memcpy(ctxt->current_points, x_points, new_span_bytes);
}

struct region_iteration_context {
    uint8_t* ptr;
    int prev_small_mode_len;
    int16_t prev_y1;

     /* 
     * In small mode, you need to know the values of each of the spans on the 
     * previous scanline. We must store them all here! Luckily there's a limit
     * of 16 spans in small mode!
     */
    int16_t prev_x0[16];
    int16_t prev_x1[16];

    int num_repeats;        // = 0  
    bool used0x7E;          // = false
    int i;                  // = 0
    uint8_t* start_ptr;     
    uint8_t ctrl_byte;
    int num_spans;
    int16_t y0;             // safe to read post- band callback
    int16_t y1;             // safe to read post- band callback
                            // ^^ not translated by DC!
    
    int16_t scaled_y0;      // translated by DC
    int16_t scaled_y1;      // translated by DC
    bool compact_spans;
    uint8_t compact_spans_top_byte;
    bool in_repeat_mode;
    int j;
    bool done_band_callback;
    bool done;
    uint8_t* prev_small_mode;       // caller must provide as must be stable memory address across calls
                                    // (we have cx.ptr pointing to within it, so can't be on a local frame
                                    //  to IterateRegion3 like it would be if we kept it in cx)
                                    // must be 16 bytes long of memory behind it
};

static struct region_iteration_context CreateIterationContext(struct region* rgn) {
    struct region_iteration_context ctxt;
    ctxt.num_repeats = 0;
    ctxt.used0x7E = false;
    ctxt.i = 0;
    ctxt.j = 0;
    ctxt.done = false;
    ctxt.done_band_callback = false;
    ctxt.prev_small_mode = AllocHeap(16);
    ctxt.ptr = ((struct region_data*) rgn->data)->band_data;
    return ctxt;
}

static void CloseIterationContext(struct region_iteration_context ctxt) {
    FreeHeap(ctxt.prev_small_mode);
}

struct iterate_region_thunk_context {
    void* user_context;
    int (*rect_callback)(struct rect r, void* context, int rv, bool* cancel);
};

static bool IterateRegionThunk(int y0, int y1, int x0, int x1, int* retv, void* context) {
    struct iterate_region_thunk_context* thunk_context = (struct iterate_region_thunk_context*) context;
    struct rect r = (struct rect) {
        .x = (int) x0,
        .y = (int) y0,
        .w = (int) (x1 - x0),
        .h = (int) (y1 - y0)
    };
    bool cancel = false;
    *retv = thunk_context->rect_callback(r, thunk_context->user_context, *retv, &cancel);
    return cancel;
}

int IterateRegion(
    struct region rgn, 
    int (*rect_callback)(struct rect r, void* context, int rv, bool* cancel), 
    void* context,
    int init_rv
) {
    struct iterate_region_thunk_context thunk_context = (struct iterate_region_thunk_context) {
        .user_context = context,
        .rect_callback = rect_callback
    };
    struct region_iteration_context ctxt = CreateIterationContext(&rgn);
    int result = IterateRegionCoroutine(rgn, init_rv, (void*)&thunk_context, &ctxt, NULL, IterateRegionThunk, NULL);
    CloseIterationContext(ctxt);
    return result;
}

int IterateRegionCoroutine(
    struct region rgn,
    int retv,
    void* context,
    struct region_iteration_context* ctxt,
    bool (*band_callback)(int y0, int y1, int* retv, void* context),                        /* return TRUE to stop early */
    bool (*rect_callback)(int y0, int y1, int x0, int x1, int* retv, void* context),        /* as above */
    struct dc* scale_dc     /* NULL is okay - means no special scaling */
) {
    struct region_data* data = rgn.data;
    int trans_x = data->trans_x;
    int trans_y = data->trans_y;
    int num_bands = data->num_bands;

    if (ctxt->done) {
        return retv;
    }

    if (num_bands == 0) {
        ctxt->done = true;
        return retv;
    }

    /*
    Hmm I'm a bit concerned about how much dereferencing will be going on.
    Maybe best to copy the `ctxt` structure into the local frame and then when returning copy it back out
    */

    struct region_iteration_context cx = *ctxt;

    for (int i = cx.i; i < num_bands; ++i, cx.i++, cx.done_band_callback = false) {
        // TODO: got to skip all this part if already done band callback

        if (!cx.done_band_callback) {
            cx.start_ptr = cx.ptr;
            cx.ctrl_byte = *cx.ptr++;

            cx.compact_spans = false;
            cx.compact_spans_top_byte = 0;
            cx.in_repeat_mode = false;

            if (cx.num_repeats > 0) {
                --cx.num_repeats;
                cx.ptr = cx.prev_small_mode;
                cx.ctrl_byte = *cx.ptr++;
                cx.in_repeat_mode = true;

            } else if (cx.ctrl_byte == 0x7F || cx.ctrl_byte == 0x7E) {
                if (cx.ctrl_byte == 0x7E) {
                    cx.used0x7E = true;
                    cx.num_repeats = (*cx.ptr++) + 1; // because this time counts as one,
                                                // its +1, not +2
                }

                cx.ptr = cx.prev_small_mode;
                cx.ctrl_byte = *cx.ptr++;

                /* 
                * We need to know we're in repeat mode to get out of it at the 
                * end (i.e. to reset `ptr` back to normal). We overwrote ctrl_byte
                * just a momement ago, so need a new flag.
                */
                cx.in_repeat_mode = true;
            }

            if (cx.ctrl_byte & 0x80) {
                cx.num_spans = cx.ctrl_byte & 0x3F;
                
                if (cx.ctrl_byte & 0x40) {
                    cx.num_spans |= ((int) *cx.ptr++) << 6;
                    memcpy(&cx.y0, cx.ptr, 2);
                    memcpy(&cx.y1, cx.ptr + 2, 2);
                    cx.ptr += 4;

                } else {
                    if (cx.ctrl_byte & 0x20) {
                        cx.compact_spans = true;
                        cx.num_spans &= 0x1F;
                    }
                    memcpy(&cx.y0, cx.ptr, 2);
                    cx.y1 = cx.y0 + 1;
                    cx.ptr += 2;
                }

            } else {
                cx.num_spans = cx.ctrl_byte & 0xF;
                cx.y0 = cx.prev_y1;
                cx.y1 = cx.y0 + 1 + ((cx.ctrl_byte >> 4) & 7);
            }

            if (band_callback != NULL) {
                int sy0 = cx.y0;
                int sy1 = cx.y1;
                if (scale_dc) {
                    int dummy;
                    CdMapDCCoordinates(scale_dc, &dummy, &sy0, &dummy, &sy1);
                }
                cx.scaled_y0 = sy0;
                cx.scaled_y1 = sy1;
                if (band_callback(sy0 + trans_y, sy1 + trans_y, &retv, context)) {
                    cx.done_band_callback = true;
                    *ctxt = cx;
                    return retv;
                }
            }

            cx.done_band_callback = true;
        }

        for (int j = cx.j; j < cx.num_spans; ++j) {
            int16_t x0;
            int16_t x1;

            if (cx.ctrl_byte & 0x80) {
                if (cx.compact_spans) {
                    if (j == 0) {
                        cx.compact_spans_top_byte = *cx.ptr++;
                    }

                    x0 = *cx.ptr++;
                    x0 |= ((uint16_t) cx.compact_spans_top_byte) << 8;

                    x1 = *cx.ptr++;
                    x1 |= ((uint16_t) cx.compact_spans_top_byte) << 8;
                    
                } else {
                    memcpy(&x0, cx.ptr, 2);
                    memcpy(&x1, cx.ptr + 2, 2);
                    cx.ptr += 4;
                }
                
            } else {
                uint8_t adj_byte = *cx.ptr++;
                int adj_0 = (adj_byte & 0xF) - 8;
                int adj_1 = ((adj_byte >> 4) & 0xF) - 8;

                x0 = (int16_t) (cx.prev_x0[j] + adj_0);
                x1 = (int16_t) (cx.prev_x1[j] + adj_1);
            }

            if (j < 16) {
                /* 
                 * These are read in small mode, and small mode can only read
                 * the first 16, so that's fine.
                 */
                cx.prev_x0[j] = x0;
                cx.prev_x1[j] = x1;
            }

            if (rect_callback != NULL) {
                int sx0 = x0;
                int sx1 = x1;
                if (scale_dc) {
                    int dummy;
                    CdMapDCCoordinates(scale_dc, &sx0, &dummy, &sx1, &dummy);
                }

                int sy0 = cx.y0;
                int sy1 = cx.y1;
                if (scale_dc) {
                    int dummy;
                    CdMapDCCoordinates(scale_dc, &dummy, &sy0, &dummy, &sy1);
                }
                cx.scaled_y0 = sy0;
                cx.scaled_y1 = sy1;

                if (rect_callback(cx.scaled_y0 + trans_y, cx.scaled_y1 + trans_y, sx0 + trans_x, sx1 + trans_x, &retv, context)) {
                    cx.j++;
                    *ctxt = cx;
                    return retv;
                }
            }
        }
        
        cx.j = 0;   /* Reset for next time */

        cx.prev_y1 = cx.y1;

        if (!(cx.ctrl_byte & 0x80) && !cx.in_repeat_mode) {
            cx.prev_small_mode_len = (int) (cx.ptr - cx.start_ptr);
            memcpy(cx.prev_small_mode, cx.start_ptr, cx.prev_small_mode_len);
        }
        if (cx.in_repeat_mode) {
            /* Get out of the prev_small_mode array and back to the real ptr */
            if (cx.num_repeats == 0) {
                if (cx.used0x7E) {
                    cx.ptr = cx.start_ptr + 2;
                    cx.used0x7E = false;
                } else {
                    cx.ptr = cx.start_ptr + 1;
                }
            } else {
                cx.ptr = cx.start_ptr;
            }
        }
    }

    cx.done = true;
    *ctxt = cx;

    return retv;
}

struct region CdCopyRegion(struct region rgn) {
    struct region new_rgn = rgn;
    new_rgn.data = AllocHeap(rgn.allocated_length);
    memcpy(new_rgn.data, rgn.data, rgn.allocated_length);
    return new_rgn;
}

export void CdFreeRegion(struct region rgn) {
    FreeHeap(rgn.data);
}


static bool RegionCombinationEdgeCallback(int y0, int y1, int* retv, void* context) {
    int16_t* y_edges = (int16_t*) context;

    y_edges[0] = (int16_t) y0;
    y_edges[1] = (int16_t) y1;

    *retv = 2;
    return true;                // Stop here!
}

static bool NopCallback(int y0, int y1, int* retv, void* context) {
    (void) y0;
    (void) y1;
    (void) retv;
    (void) context;

    return true;
}

#include <log.h>

static bool RectCallback(int y0, int y1, int x0, int x1, int* retv, void* context) {
    (void) y0;
    (void) y1;

    int16_t* arr = (int16_t*) context;
    arr[0] = (int16_t) x0;
    arr[1] = (int16_t) x1;
    *retv = 1;

    return true;
}

static bool CheckRegionCondition(int mode, bool a, bool b) {
    switch (mode) {
        case REGION_COMBINE_INTERSECT:  return a && b;
        case REGION_COMBINE_UNION:      return a || b;
        case REGION_COMBINE_XOR:        return a ^ b;
        case REGION_COMBINE_DIFFERENCE: return a && !b;
        default: return false;
    }
}

export struct region CdGetRegionCombinationEx(int mode, struct region a, struct region b, struct dc* scale_dc) { 
    struct region_data* a_data = a.data;
    struct region_data* b_data = b.data;

    bool a_empty = a_data->num_bands == 0;
    bool b_empty = b_data->num_bands == 0;

    if (a_empty || b_empty) {
        switch (mode) {
        case REGION_COMBINE_INTERSECT:
            return CdEmptyRegion();

        case REGION_COMBINE_DIFFERENCE:
            /* a && !b : empty if a is empty, else b contributes nothing. */
            return a_empty ? CdEmptyRegion() : CdCopyRegion(a);

        case REGION_COMBINE_UNION:
        case REGION_COMBINE_XOR:
            if (b_empty) {
                return CdCopyRegion(a);
            }
            /*
             * a is empty, so the answer is b -- but only if b isn't being
             * remapped. With a scale_dc we'd have to transform it, so fall
             * through to the general path instead.
             */
            if (scale_dc == NULL) {
                return CdCopyRegion(b);
            }
            break;

        default:
            return CdEmptyRegion();
        }
    }

    /* 
     * Get, in order, all the Y edges across both regions where something 
     * 'happens'. We do this by merging in the lowest values we see into 
     * the list.
     */
    int allocated = MAX(a_data->num_bands, b_data->num_bands) + 8;
    int num_edges = 0;
    int16_t* y_edges = AllocHeap(allocated * sizeof(int16_t));

    struct region_iteration_context a_ctxt = CreateIterationContext(&a);
    struct region_iteration_context b_ctxt = CreateIterationContext(&b);

    int16_t a_edges_buffer[2];
    int16_t b_edges_buffer[2];
    int num_a_edges_buffer = 0;
    int num_b_edges_buffer = 0;

    int previous_written = INT16_MIN - 1;

    while (!a_ctxt.done || !b_ctxt.done) {
        while (num_edges + 2 >= allocated) {
            allocated *= 2;
            y_edges = ReallocHeap(y_edges, allocated * sizeof(int16_t));
        }

        if (num_a_edges_buffer == 0 && !a_ctxt.done) {
            int16_t probe[2];
            int a_consumed = IterateRegionCoroutine(a, 0, probe, &a_ctxt, RegionCombinationEdgeCallback, NULL, NULL);
            if (a_consumed == 2) {
                num_a_edges_buffer = 2;
                memcpy(a_edges_buffer, probe, sizeof(a_edges_buffer));
            }
        }
        if (num_b_edges_buffer == 0 && !b_ctxt.done) {
            // here's the plan. (?)
            // if we detect that Y axis is negated, we'll basically swap 
            // out `RegionCombinationEdgeCallback` for another thunk callback
            // and we'll, before this loop even starts, go through all of `b`
            // using the 'normal' routine to copy it into an allocated buffer.
            // we'll then invert that buffer, and then the thunk routine can
            // just the buffer data instead of the data from the actual coroutine 
            
            int16_t probe[2];
            int b_consumed = IterateRegionCoroutine(b, 0, probe, &b_ctxt, RegionCombinationEdgeCallback, NULL, scale_dc);
            if (b_consumed == 2) {
                num_b_edges_buffer = 2;
                memcpy(b_edges_buffer, probe, sizeof(b_edges_buffer));
            }
        }

        if (num_a_edges_buffer == 0 && num_b_edges_buffer == 0) {
            break;
        }

        bool choose_b;
        if      (num_a_edges_buffer == 0) choose_b = true;
        else if (num_b_edges_buffer == 0) choose_b = false;
        else {
            choose_b = b_edges_buffer[0] < a_edges_buffer[0];
        }

        if (choose_b) {
            num_b_edges_buffer--;
            if (b_edges_buffer[0] > previous_written) {
                y_edges[num_edges++] = b_edges_buffer[0];
                previous_written = b_edges_buffer[0];
            }
            b_edges_buffer[0] = b_edges_buffer[1];
        } else {
            num_a_edges_buffer--;
            if (a_edges_buffer[0] > previous_written) {
                y_edges[num_edges++] = a_edges_buffer[0];
                previous_written = a_edges_buffer[0];
            }
            a_edges_buffer[0] = a_edges_buffer[1];
        }
    }
    
    CloseIterationContext(a_ctxt);
    CloseIterationContext(b_ctxt);

    a_ctxt = CreateIterationContext(&a);
    b_ctxt = CreateIterationContext(&b);

    /* 
     * For each adjacent pair of Y edges, find the corresponding band in both
     * the A region and the B region, if present. Then we merge them and add
     * them to the output region.
     */

    struct region_build_context build_context;
    struct region out_rgn;
    BuildNewRegion(&out_rgn, y_edges[0], &build_context);

    /* 
     * Clear off the initial band callbacks, we don't need them. The y0, y1
     * values from this are used though in the initial loading of x values.
     */
    IterateRegionCoroutine(a, 0, NULL, &a_ctxt, NopCallback, NULL, NULL);
    IterateRegionCoroutine(b, 0, NULL, &b_ctxt, NopCallback, NULL, scale_dc);

    /*
     * These represent the ranges in which the stored (x0, x1) data corresponds.
     */
    int a_y0 = INT16_MIN - 1;
    int a_y1 = INT16_MIN - 1;
    int b_y0 = INT16_MIN - 1;
    int b_y1 = INT16_MIN - 1;

    int16_t* a_x_points = NULL;
    int16_t* b_x_points = NULL;
    int num_a_x_spans;
    int num_b_x_spans;

    for (int j = 0; j < num_edges - 1; ++j) {
        int16_t y0 = y_edges[j];
        int16_t y1 = y_edges[j + 1];

        while (!a_ctxt.done && a_y1 <= y0) {
            /*
             * This is the y0,y1 of the rects we're reading in. It comes either
             * from the initial priming of the pump, or from the previous
             * loop iteration. 
             */
            a_y0 = a_ctxt.scaled_y0 + a_data->trans_y;
            a_y1 = a_ctxt.scaled_y1 + a_data->trans_y;
            num_a_x_spans = 0;
            if (a_x_points != NULL) {
                FreeHeap(a_x_points);
            }
            a_x_points = AllocHeap(a_ctxt.num_spans * 2 * sizeof(int16_t));

            while (true) {
                /* 
                 * Read the rectangles for the current band into an array.
                 * Stop when we see the next band. The YieldCallback doesn't 
                 * modify the return value, but the RectCallback does.
                 */
                int16_t x[2];
                int retv = IterateRegionCoroutine(
                    a, 0, (void*) x, &a_ctxt, NopCallback, RectCallback, NULL
                );
                if (retv == 0) {
                    /* This was a new band being seen. */
                    break;
                } else {
                    a_x_points[num_a_x_spans * 2 + 0] = x[0];
                    a_x_points[num_a_x_spans * 2 + 1] = x[1];
                    num_a_x_spans++;
                }
            }
        }
        while (!b_ctxt.done && b_y1 <= y0) {
            b_y0 = b_ctxt.scaled_y0 + b_data->trans_y;
            b_y1 = b_ctxt.scaled_y1 + b_data->trans_y;
            num_b_x_spans = 0;
            if (b_x_points != NULL) {
                FreeHeap(b_x_points);
            }
            b_x_points = AllocHeap(b_ctxt.num_spans * 2 * sizeof(int16_t));

            while (true) {
                int16_t x[2];
                int retv = IterateRegionCoroutine(
                    b, 0, (void*) x, &b_ctxt, NopCallback, RectCallback, scale_dc
                );
                if (retv == 0) {
                    break;
                } else {
                    if (scale_dc == NULL) {
                        b_x_points[num_b_x_spans * 2 + 0] = x[0];
                        b_x_points[num_b_x_spans * 2 + 1] = x[1];
                    } else {
                        int tmp1 = x[0];
                        int tmp2 = x[1];
                        if (tmp2 < tmp1) {
                            b_x_points[(b_ctxt.num_spans - num_b_x_spans - 1) * 2 + 0] = tmp2;
                            b_x_points[(b_ctxt.num_spans - num_b_x_spans - 1) * 2 + 1] = tmp1;
                        } else {
                            b_x_points[num_b_x_spans * 2 + 0] = tmp1;
                            b_x_points[num_b_x_spans * 2 + 1] = tmp2;
                        }
                    }
                    num_b_x_spans++;
                }
            }
        }

        /* 
         * Check if there's actually data there in this (y0, y1).
         */
        bool a_has_data = y0 >= a_y0 && y0 < a_y1 && !(a_ctxt.i == 0 && a_ctxt.done);
        bool b_has_data = y0 >= b_y0 && y0 < b_y1 && !(b_ctxt.i == 0 && b_ctxt.done);

        /* 
         * We now have the information we need to figure out what the combined
         * spans are.
         */

        int a_idx = 0;
        int b_idx = 0;
        int a_limit = a_has_data ? num_a_x_spans * 2 : 0;
        int b_limit = b_has_data ? num_b_x_spans * 2 : 0;

        int16_t* out_spans = AllocHeap(sizeof(int16_t) * (a_limit + b_limit));

        int out_count = 0;
        bool active_a = false;
        bool active_b = false;
        bool in_span = false;
        
        while (a_idx < a_limit || b_idx < b_limit) {
            int16_t cur_x;
            
            if (a_idx < a_limit && (b_idx >= b_limit || a_x_points[a_idx] < b_x_points[b_idx])) {
                cur_x = a_x_points[a_idx++];
                active_a = !active_a;

            } else if (b_idx < b_limit && (a_idx >= a_limit || b_x_points[b_idx] < a_x_points[a_idx])) {
                cur_x = b_x_points[b_idx++];
                active_b = !active_b;

            } else {
                cur_x = a_x_points[a_idx++];
                active_a = !active_a;
                cur_x = b_x_points[b_idx++]; 
                active_b = !active_b;
            }

            bool should_be_active = CheckRegionCondition(mode, active_a, active_b);

            if (should_be_active && !in_span) {
                out_spans[out_count++] = cur_x;
                in_span = true;

            } else if (!should_be_active && in_span) {
                if (out_spans[out_count - 1] == cur_x) {
                    out_count--;
                } else {
                    out_spans[out_count++] = cur_x;
                }
                in_span = false;
            }
        }

        for (int i = 0; i < (y1 - y0); ++i) {
            AddScanline(&out_rgn, &build_context, out_count / 2, out_spans, i != 0);
        }

        FreeHeap(out_spans);
    }

    FreeHeap(y_edges);

    if (a_x_points != NULL) {
        FreeHeap(a_x_points);
    }
    if (b_x_points != NULL) {
        FreeHeap(b_x_points);
    }

    CloseIterationContext(a_ctxt);
    CloseIterationContext(b_ctxt);

    FinishRegion(&out_rgn, &build_context);

    return out_rgn;
}

export struct region CdGetRegionCombination(int mode, struct region a, struct region b) {
    return CdGetRegionCombinationEx(mode, a, b, NULL);
}

export bool CdIsRegionEmpty(struct region rgn) {
    struct region_data* data = rgn.data;
    return data->num_bands == 0;
}

int LogRect(struct rect r, void*, int rv, bool* cancel) {
    *cancel = false;
    LogPrintf("RECT: %d, %d (W: %d, H: %d)\n", r.x, r.y, r.w, r.h);
    return rv;
}

void CdLogRegion(struct region r) {
    IterateRegion(r, LogRect, NULL, 0);
}

int IterateRegion(
    struct region rgn, 
    int (*rect_callback)(struct rect r, void* context, int rv, bool* cancel), 
    void* context,
    int init_rv
);

static int GetRegionBoundsCallback(struct rect r, void* ctxt, int rv, bool* cancel) {
    // We treat `w` as x2 and `h` as y2.
    struct rect* bound_rect = ctxt;
    if (r.y < bound_rect->y) {
        bound_rect->y = r.y;
    }
    if (r.x < bound_rect->x) {
        bound_rect->x = r.x;
    }
    if (r.x + r.w > bound_rect->w) {
        bound_rect->w = r.x + r.w;
    }
    if (r.y + r.h > bound_rect->h) {
        bound_rect->h = r.y + r.h;
    }
    *cancel = false;
    return rv;
}

export struct rect CdGetRegionBounds(struct region rgn) {
    if (CdIsRegionEmpty(rgn)) {
        struct rect empty = {0};
        return empty;
    }

    // We treat `w` as x2 and `h` as y2.
    struct rect r;
    r.x = INT16_MAX;
    r.y = INT16_MAX;
    r.w = INT16_MIN;
    r.h = INT16_MIN;
    IterateRegion(rgn, GetRegionBoundsCallback, &r, 0);

    // Correct the w/h from x2/y2
    r.w -= r.x;
    r.h -= r.y;
    return r;
}



struct region CdEmptyRegion(void) {
    struct region rgn;
    rgn.allocated_length = sizeof(struct region_data);
    rgn.used_length = rgn.allocated_length;
    rgn.data = AllocHeap(rgn.allocated_length);

    struct region_data* data = rgn.data;
    data->trans_x = 0;
    data->trans_y = 0;
    data->num_bands = 0;

    return rgn;
}

export int CdTranslateRegion(struct region* rgn, int offx, int offy) {
    if (rgn == NULL) {
        return -1;//TODO: EINVAL;
    }

    struct region_data* data = rgn->data;
    data->trans_x += offx;
    data->trans_y += offy;
    return 0;
}

export struct region CdCreateRectRegion(int x, int y, int width, int height) {
    /* 
     * This function will bypass AddScanline, just because this one needs to be 
     * fast! (And it's easy to write!).
     * 
     * Luckily, all regions must start with their first band in regular mode.
     * So it's not too bad. We also prohibit compact mode on the first line to 
     * make our lives here even easier.
     */

    if (width <= 0 || height <= 0) {
        return CdEmptyRegion();
    }

    struct region rgn;
    rgn.allocated_length = sizeof(struct region_data) + 10;
    rgn.data = AllocHeap(rgn.allocated_length);
    rgn.used_length = rgn.allocated_length;

    struct region_data* data = rgn.data;
    data->trans_x = 0;
    data->trans_y = 0;
    data->num_bands = 1;

    data->band_data[0] = 0x80 | 0x40 | 1;      /* regular mode, 1 span */
    data->band_data[1] = 0;                    /* high byte of spans   */

    int16_t y0 = (int16_t) y;
    int16_t y1 = (int16_t) (y + height);
    memcpy(data->band_data + 2, &y0, 2);
    memcpy(data->band_data + 4, &y1, 2);

    int16_t x0 = (int16_t) x;
    int16_t x1 = (int16_t) (x + width);
    memcpy(data->band_data + 6, &x0, 2);
    memcpy(data->band_data + 8, &x1, 2);

    return rgn;
}

export struct region CdEverythingRegion(void) {
    return CdCreateRectRegion(INT16_MIN, INT16_MIN, 65535, 65535);
}

export void CdGetRegionCombinationInPlace(int mode, struct region* a, struct region b) {
    struct region c = CdGetRegionCombination(mode, *a, b);
    CdFreeRegion(*a);
    *a = c;
}
