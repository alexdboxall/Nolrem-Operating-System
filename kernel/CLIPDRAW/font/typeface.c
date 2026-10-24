#include "font_structures.h"
#include <heap.h>
#include <errno.h>
#include <omf.h>
#include "../api.h"
#include "../clipdraw_internal.h"

export pageable struct typeface* CdOpenTypeface(const char* file) {
    struct typeface* tf = AllocHeap(sizeof(struct typeface));
    if (tf == NULL) {
        return NULL;
    }
    InitUserObject(tf, UOBJ_TYPEFACE);

    int res = OpenAndMapFile(file, &tf->omf);
    if (res != 0) {
        FreeHeap(tf);
        return NULL;
    }

    uint8_t sig1 = OmfRead8AndAdvance(&tf->omf);
    uint8_t sig2 = OmfRead8AndAdvance(&tf->omf);
    uint8_t sig3 = OmfRead8AndAdvance(&tf->omf);
    uint8_t ver  = OmfRead8AndAdvance(&tf->omf);
    if (sig1 != 'T' || sig2 != 'Y' || sig3 != 'P' || ver != 0x00) {
        goto fail_after_open;
    }

    tf->num_fonts = OmfRead8AndAdvance(&tf->omf);
    if (tf->omf.error || tf->num_fonts == 0) {
        goto fail_after_open;
    }

    return tf;

fail_after_open:
    /* CleanupTypeface closes the Omf and frees tf. */
    DerefObject(tf);
    return NULL;
}


export pageable struct font* CdOpenFont(struct typeface* tf, font_style_id style) {
    if (tf == NULL || style == INVALID_FONT_STYLE) {
        return NULL;
    }
    struct font* fn = AllocHeap(sizeof(struct font));
    if (fn == NULL) {
        return NULL;
    }
    InitUserObject(fn, UOBJ_FONT);
    RefObject(tf);

    fn->tf = tf;
    fn->requested_style = style;
    fn->index = 0;
    fn->underlying_style = CdGetTypefaceClosestStyle(tf, style, &fn->index);

    /* Both of these are absolute positions in the file, so they are jumps,
     * not advances from wherever the cursor happens to be. */
    OmfJump(&tf->omf, 32 + fn->index * 12);
    font_style_id underlying_again = OmfRead32AndAdvanceLE(&tf->omf);
    fn->file_offset = OmfRead32AndAdvanceLE(&tf->omf);
    fn->font_length = OmfRead32AndAdvanceLE(&tf->omf);

    OmfJump(&tf->omf, fn->file_offset);
    fn->min_char = OmfRead32AndAdvanceLE(&tf->omf);
    fn->num_chars = OmfRead32AndAdvanceLE(&tf->omf);
    fn->fixed_width = OmfRead16AndAdvanceLE(&tf->omf);
    fn->avg_advance = OmfRead16AndAdvanceLE(&tf->omf);
    fn->ascend_px = OmfRead16AndAdvanceLE(&tf->omf);
    fn->height_px = OmfRead16AndAdvanceLE(&tf->omf);
    fn->is_large_font = OmfRead8AndAdvance(&tf->omf);
    fn->is_32bit_glyph_offsets = OmfRead8AndAdvance(&tf->omf);
    fn->gap_table_offset = OmfRead32AndAdvanceLE(&tf->omf);
    fn->kern_table_offset = OmfRead32AndAdvanceLE(&tf->omf);
    fn->glyph_table_offset = OmfRead32AndAdvanceLE(&tf->omf);
    
    if (fn->num_chars > 0xFFFF) {
        fn->codepoint_size = FNT_CODEPOINT_SIZE_32;
    } else if (fn->num_chars > 0xFF) {
        fn->codepoint_size = FNT_CODEPOINT_SIZE_16;
    } else {
        fn->codepoint_size = FNT_CODEPOINT_SIZE_8;
    }

    /* Now everything's set up, we can destroy if it's invalid (to ensure that 
     * everything initialised for the cleanup hander instead of it seeing a 
     * partially set up object.) */
    if (fn->underlying_style == INVALID_FONT_STYLE 
        || tf->omf.error
        || underlying_again != fn->underlying_style
        || (fn->min_char > 0x10FFFF)    /* above UTF32 range - prevent overflow when adding*/
        || (fn->num_chars > 0x10FFFF)   /* above UTF32 range - prevent overflow when adding*/
    ) {
        DerefObject(fn);
        return NULL;
    }

    return fn;
}

#define ADV_OMF(off, failret)  OmfAdvance(&tf->omf, off); if (tf->omf.error) {return failret;} 
#define JUMP_OMF(pos, failret) OmfJump(&tf->omf, pos); if (tf->omf.error) {return failret;} 
#define FAIL_ERR(failret)      if (tf->omf.error) {return failret;} 

export pageable int CdGetTypefaceName(struct typeface* tf, char* out_buffer, size_t buffer_len) {
    if (tf == NULL || out_buffer == NULL || buffer_len < 27) {
        return EINVAL;
    }
    JUMP_OMF(5, EINVAL);
    for (int i = 0; i < 27; ++i) {
        uint8_t c = OmfRead8AndAdvance(&tf->omf);
        FAIL_ERR(EINVAL);
        out_buffer[i] = c;
        if (i == 26 && c != 0) {
            /* Not null terminated! */
            return EINVAL;
        }
        if (c == 0) {
            break;
        }
    }
    return 0;
}

export pageable font_style_id CdEnumerateTypefaceStyle(struct typeface* tf, int index) {
    if (tf == NULL || index < 0 || index >= (int)tf->num_fonts) {
        return INVALID_FONT_STYLE;
    }
    JUMP_OMF(32 + index * 12, INVALID_FONT_STYLE);
    font_style_id val = OmfRead32LE(&tf->omf);
    FAIL_ERR(INVALID_FONT_STYLE);
    return val;
}

export pageable font_style_id CdGetTypefaceClosestStyle(struct typeface* tf, font_style_id style, int* best_index) {
    if (best_index != NULL) {
        *best_index = 0;
    }
    if (tf == NULL || tf->num_fonts == 0) {
        return INVALID_FONT_STYLE;
    }

    const int  want_size   = GetFontStylePointsIn20ths(style);
    const bool want_bold   = IsFontStyleBold(style);
    const bool want_italic = IsFontStyleItalic(style);

    font_style_id best = INVALID_FONT_STYLE;
    uint32_t best_penalty = UINT32_MAX;

    JUMP_OMF(32, INVALID_FONT_STYLE);
    for (int i = 0; i < tf->num_fonts; ++i) {
        font_style_id val = OmfRead32LE(&tf->omf);
        FAIL_ERR(INVALID_FONT_STYLE);
        /* Entries are 12 bytes (style, file_offset, font_length), so step 
         * over the other two fields to reach the next style. */
        ADV_OMF(12, INVALID_FONT_STYLE);

        /* Size dominates. Prefer larger over smaller, since shrinking
         * a bitmap loses less than stretching it. */
        int size = GetFontStylePointsIn20ths(val);
        uint32_t size_penalty = (size >= want_size)
            ? (uint32_t)(size - want_size)
            : (uint32_t)(want_size - size) * 2;

        /* Bold next (a faked bold changes glyph widths), then italic
         * (a faked italic is just a shear). */
        uint32_t bold_penalty   = (IsFontStyleBold(val)   != want_bold)   ? 1 : 0;
        uint32_t italic_penalty = (IsFontStyleItalic(val) != want_italic) ? 1 : 0;
        uint32_t penalty = (size_penalty << 2) | (bold_penalty << 1) | italic_penalty;

        if (penalty < best_penalty) {
            best_penalty = penalty;
            best = val;
            if (best_index != NULL) {
                *best_index = i;
            }
            if (penalty == 0) {
                return best;
            }
        }
    }

    return best;
}

export pageable bool CdCheckTypefaceForStyle(struct typeface* tf, font_style_id style) {
    font_style_id closest = CdGetTypefaceClosestStyle(tf, style, NULL);
    return closest != INVALID_FONT_STYLE && closest == style;
}

static uint32_t ReadAndAdvanceCodePointSize(struct font* fn) {
    if (fn->codepoint_size == FNT_CODEPOINT_SIZE_8) {
        return OmfRead8AndAdvance(&fn->tf->omf);
    } else if (fn->codepoint_size == FNT_CODEPOINT_SIZE_16) {
        return OmfRead16AndAdvanceLE(&fn->tf->omf);
    } else {
        return OmfRead32AndAdvanceLE(&fn->tf->omf);
    }
}

/* Maps a code point to an index into the glyph table, or BAD_GLYPH_CHAR if the
 * font has no glyph for it. */
static uint32_t GetGlyphIndex(struct font* fn, uint32_t c) {
    if (c < fn->min_char) {
        return BAD_GLYPH_CHAR;
    }
    c -= fn->min_char;
    if (fn->gap_table_offset != 0) {
        OmfJump(&fn->tf->omf, fn->file_offset + fn->gap_table_offset);
        uint32_t num_gaps = OmfRead32AndAdvanceLE(&fn->tf->omf);
        if (fn->tf->omf.error) {
            return BAD_GLYPH_CHAR;
        }
        for (uint32_t i = 0; i < num_gaps; ++i) {
            uint32_t if_this = ReadAndAdvanceCodePointSize(fn);
            uint32_t then_sub = ReadAndAdvanceCodePointSize(fn);
            if (fn->tf->omf.error) {
                return BAD_GLYPH_CHAR;
            }
            if (then_sub > if_this) {
                /* Corrupt: the gap would start before the first code point. */
                return BAD_GLYPH_CHAR;
            }
            if (c >= if_this) {
                /* Past this gap: close up the space it leaves. */
                c -= then_sub;
            } else {
                /* Before this gap. If we're inside it, there's no glyph. The
                 * gap occupies [if_this - then_sub, if_this) in the running
                 * value. Entries are sorted, so either way we're done. */
                if (c >= if_this - then_sub) {
                    return BAD_GLYPH_CHAR;
                }
                break;
            }
        }
    }
    if (c >= fn->num_chars) {
        return BAD_GLYPH_CHAR;
    }
    return c;
}

static bool AdvanceToGlyphData(struct font* fn, uint32_t c) {
    uint32_t index = GetGlyphIndex(fn, c);
    if (index == BAD_GLYPH_CHAR) {
        return false;
    }
    uint32_t table_pos = fn->file_offset + fn->glyph_table_offset;
    OmfJump(&fn->tf->omf, 
        table_pos + (fn->is_32bit_glyph_offsets ? 4 : 2) * index);

    /* Offsets in the glyph table are relative to the start of the table. */
    uint32_t pos = table_pos;
    if (fn->is_32bit_glyph_offsets) {
        pos += OmfRead32LE(&fn->tf->omf);
    } else {
        pos += OmfRead16LE(&fn->tf->omf);
    }
    if (fn->tf->omf.error) {
        return false;
    }
    OmfJump(&fn->tf->omf, pos);
    return !fn->tf->omf.error;
}

struct glyph_data {
    uint16_t width_px;
    uint16_t height_px;
    uint16_t advance;
    uint32_t absolute_advance_to_bitmap;
    bool error;
};

export pageable uint8_t* ScaleGlyphData(uint8_t* glyph, int old_w, int old_h, int new_w, int new_h) {
    int c, k, dy;

    if (new_w <= 0 || new_h <= 0) {
        return NULL;
    }

    int new_chunks = (new_w + 7) >> 3;
    uint8_t* new_glyph = AllocHeap(new_chunks * new_h);
    if (new_glyph == NULL) {
        return NULL;
    }
    uint8_t* out = new_glyph;

    if (glyph == NULL || old_w <= 0 || old_h <= 0) {
        for (k = new_chunks * new_h; k > 0; k--) *out++ = 0;
        return new_glyph;
    }

    int x_den = new_w << 1;
    int x_q   = (old_w << 1) / x_den;
    int x_r   = (old_w << 1) % x_den;
    int x_sx  = old_w / x_den;
    int x_rem = old_w % x_den;

    int y_den = new_h << 1;
    int y_q   = (old_h << 1) / y_den;
    int y_r   = (old_h << 1) % y_den;
    int y_sy0 = old_h / y_den; 
    int y_rem0 = old_h % y_den;

    for (c = 0; c < new_chunks; c++) {
        uint32_t xo[8];
        uint8_t xs[8]; 
        int n = new_w - (c << 3);
        if (n > 8) n = 8;

        for (k = 0; k < n; k++) {
            xo[k] = (uint32_t)(x_sx >> 3) * (uint32_t)old_h;
            xs[k] = (uint8_t)(7 - (x_sx & 7));
            x_sx  += x_q;
            x_rem += x_r;
            if (x_rem >= x_den) { x_rem -= x_den; x_sx++; }
        }

        int sy = y_sy0, rem = y_rem0, prev_sy = -1;
        uint32_t b = 0;

        for (dy = 0; dy < new_h; dy++) {
            if (sy != prev_sy) { 
                b = 0;
                for (k = 0; k < n; k++) {
                    b = (b << 1) | ((glyph[xo[k] + sy] >> xs[k]) & 1);
                }
                b <<= (8 - n); 
                prev_sy = sy;
            }
            *out++ = (uint8_t) b;

            sy  += y_q;
            rem += y_r;
            if (rem >= y_den) { rem -= y_den; sy++; }
        }
    }

    return new_glyph;
}

export struct glyph_data CdGetGlyphUnderlyingData(struct font* fn, uint32_t c) {
    struct glyph_data data;
    data.width_px = 0;
    data.height_px = 0;
    data.advance = 0;
    data.absolute_advance_to_bitmap = 0;
    data.error = false;

    if (fn == NULL) {
        data.error = true;
        return data;
    }
    data.height_px = fn->height_px;
    
    bool ok = AdvanceToGlyphData(fn, c);
    if (!ok) {
        data.error = true;
        return data;
    }

    if (fn->fixed_width != 0) {
        data.width_px = fn->fixed_width;
        data.advance = fn->avg_advance;
    } else if (fn->is_large_font) {
        data.width_px = OmfRead16AndAdvanceLE(&fn->tf->omf);
        data.advance = OmfRead16AndAdvanceLE(&fn->tf->omf);
    } else {
        data.width_px = OmfRead8AndAdvance(&fn->tf->omf);
        data.advance = OmfRead8AndAdvance(&fn->tf->omf);
    }
    data.absolute_advance_to_bitmap = fn->tf->omf.cursor;
    if (fn->tf->omf.error) {
        data.error = true;
        return data;
    }
    return data;
}

static void CleanupTypeface(void* _tf) {
    struct typeface* tf = _tf;
    OmfClose(&tf->omf);
    FreeHeap(tf);
}

static void CleanupFont(void* _fn) {
    struct font* fn = _fn;
    DerefObject(fn->tf);
    FreeHeap(fn);
}

void CdInitTypefaceSubsystem(void) {
    RegisterUserObjectType(UOBJ_TYPEFACE, CleanupTypeface);
    RegisterUserObjectType(UOBJ_FONT, CleanupFont);

    extern void InitScaledGlyphCache();
    InitScaledGlyphCache();
}