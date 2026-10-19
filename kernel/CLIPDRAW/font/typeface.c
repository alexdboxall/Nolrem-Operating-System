#include "font_structures.h"
#include <heap.h>
#include <errno.h>
#include <omf.h>
#include "../api.h"
#include "../clipdraw_internal.h"


/*
struct typeface_file_header {
    char signature[3];
    uint8_t version;
    uint8_t num_fonts;
    char name[27];
    struct {
        font_style_id style;
        uint32_t file_offset;
        uint32_t font_length;
    } font_entries[];
};

struct font_file_header {
    uint32_t min_char;
    uint32_t num_chars;
    uint16_t fixed_width;
    uint16_t avg_advance;
    uint16_t ascend_px;
    uint16_t height_px;
    uint8_t is_large_font; 
    uint8_t is_32bit_glyph_offsets; 
    uint32_t gap_table_offset;
    uint32_t kern_table_offset;
    uint32_t glyph_table_offset;
};

Glyph table:
    uint16/32_t offsets[];
    // where the offsets are also relative to `glyph_table_offset`

Glyph data:
    uint8/16_t bitmap_width;
    uint8/16_t advance;
    uint8_t glyph[];

Gap data:
    uint32_t num_gaps;
    struct {
        uint8/16/32_t if;
        uint8/16/32_t then;
    } gaps[];

*/


export pageable struct typeface* CdOpenTypeface(const char* file) {
    struct typeface* tf = AllocHeap(sizeof(struct typeface));
    if (tf == NULL) {
        return NULL;
    }
    InitUserObject(tf, UOBJ_TYPEFACE);

    int res = OpenAndMapFile(file, &tf->omf);
    if (res != 0) {
        return NULL;
    }

    uint8_t sig1 = OmfRead8AndAdvance(&tf->omf);
    uint8_t sig2 = OmfRead8AndAdvance(&tf->omf);
    uint8_t sig3 = OmfRead8AndAdvance(&tf->omf);
    uint8_t ver  = OmfRead8AndAdvance(&tf->omf);
    if (sig1 != 'T' || sig2 != 'Y' || sig3 != 'P' || ver != 0x00) {
        /* Omf will also be closed at the cleanup, but Omf is fine to 
         * double close*/
        goto fail_after_open;
    }

    tf->num_fonts = OmfRead8AndAdvance(&tf->omf);
    if (tf->omf.error || tf->num_fonts == 0) {
        goto fail_after_open;
    }

    return tf;

fail_after_open:
    OmfClose(&tf->omf);
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
    fn->underlying_style = CdGetTypefaceClosestStyle(tf, style, &fn->index);

    OmfAdvance(&tf->omf, 32 + fn->index * 12);
    font_style_id underlying_again = OmfRead32AndAdvanceLE(&tf->omf);
    fn->file_offset = OmfRead32AndAdvanceLE(&tf->omf);
    fn->font_length = OmfRead32AndAdvanceLE(&tf->omf);

    OmfAdvance(&tf->omf, fn->file_offset);
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
    if (tf == NULL || index >= 256 || index < 0) {
        return INVALID_FONT_STYLE;
    }
    JUMP_OMF(32 + index * 12, INVALID_FONT_STYLE);
    font_style_id val = OmfRead32LE(&tf->omf);
    FAIL_ERR(INVALID_FONT_STYLE);
    return val;
}

export pageable font_style_id CdGetTypefaceClosestStyle(struct typeface* tf, font_style_id style, int* best_index) {
    if (tf == NULL || tf->num_fonts == 0) {
        return INVALID_FONT_STYLE;
    }

    const int  want_size   = GetFontStylePointsIn20ths(style);
    const bool want_bold   = IsFontStyleBold(style);
    const bool want_italic = IsFontStyleItalic(style);

    font_style_id best = INVALID_FONT_STYLE;
    uint32_t best_penalty = UINT32_MAX;

    if (best_index != NULL) {
        *best_index = 0;
    }

    JUMP_OMF(32, INVALID_FONT_STYLE);
    for (int i = 0; i < tf->num_fonts; ++i) {
        font_style_id val = OmfRead32AndAdvanceLE(&tf->omf);
        FAIL_ERR(INVALID_FONT_STYLE);

        int size = GetFontStylePointsIn20ths(val);
        uint32_t size_penalty = (size >= want_size)
            ? (uint32_t)(size - want_size)
            : (uint32_t)(want_size - size) * 2;

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
    } else if (fn->codepoint_size == FNT_CODEPOINT_SIZE_8) {
        return OmfRead16AndAdvanceLE(&fn->tf->omf);
    } else {
        return OmfRead32AndAdvanceLE(&fn->tf->omf);
    }
}

static uint32_t GetGlyphIndex(struct font* fn, uint32_t c) {
    if (c < fn->min_char) {
        return BAD_GLYPH_CHAR;
    }
    c -= fn->min_char;
    if (fn->gap_table_offset != 0) {
        OmfJump(&fn->tf->omf, fn->file_offset + fn->gap_table_offset);
        int num_gaps = OmfRead32AndAdvanceLE(&fn->tf->omf);
        if (fn->tf->omf.error) {
            return BAD_GLYPH_CHAR;
        }
        for (int i = 0; i < num_gaps; ++i) {
            uint32_t if_this = ReadAndAdvanceCodePointSize(fn);
            uint32_t then_sub = ReadAndAdvanceCodePointSize(fn);
            if (fn->tf->omf.error) {
                return BAD_GLYPH_CHAR;
            }
            if (c >= if_this) {
                if (c < then_sub) {
                    return BAD_GLYPH_CHAR;
                }
                c -= then_sub;
            } else {
                break;
            }
        }
    }
    if (c > fn->num_chars) {
        return BAD_GLYPH_CHAR;
    }
    return c;
}


static bool AdvanceToGlyphData(struct font* fn, uint32_t c) {
    uint32_t index = GetGlyphIndex(fn, c);
    if (c == BAD_GLYPH_CHAR) {
        return false;
    }
    OmfJump(&fn->tf->omf, 
        fn->file_offset 
        + fn->glyph_table_offset
        + (fn->is_32bit_glyph_offsets ? 4 : 2) * index);
    uint32_t pos = fn->file_offset + fn->glyph_table_offset;
    if (fn->is_32bit_glyph_offsets) {
        pos += OmfRead32LE(&fn->tf->omf);
    } else {
        pos += OmfRead16LE(&fn->tf->omf);
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

export struct glyph_data CdGetGlyphUnderlyingData(struct font* fn, uint32_t c) {
    struct glyph_data data;
    data.error = false;
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
}