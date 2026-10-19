#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <obj.h>
#include <vmm.h>
#include <omf.h>

typedef uint32_t font_style_id;
#define INVALID_FONT_STYLE ((font_style_id) 0)
#define BAD_GLYPH_CHAR     ((uint32_t) -1)

struct typeface {
    struct user_obj_header hdr;
    struct open_mapped_file omf;
    uint8_t num_fonts;
};

#define FNT_CODEPOINT_SIZE_8    2
#define FNT_CODEPOINT_SIZE_16   0
#define FNT_CODEPOINT_SIZE_32   1

struct font {
    struct user_obj_header hdr;
    struct typeface* tf;
    font_style_id requested_style;
    font_style_id underlying_style;
    int index;
    uint8_t codepoint_size : 2;

    /* Read from the typeface file data*/
    uint32_t file_offset;
    uint32_t font_length;

    /* Read from the font file data */
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

font_style_id MakeFontStyle(int points, int points_20ths, bool bold, bool italic);
bool IsFontStyleBold(font_style_id id);
bool IsFontStyleItalic(font_style_id id);
int GetFontStylePointsIn20ths(font_style_id id);

struct typeface* CdOpenTypeface(const char* file);
font_style_id CdEnumerateTypefaceStyle(struct typeface* tf, int index);
bool CdCheckTypefaceForStyle(struct typeface* tf, font_style_id style);
font_style_id CdGetTypefaceClosestStyle(struct typeface* tf, font_style_id style, int* best_index_out);
int CdGetTypefaceName(struct typeface* tf, char* out_buffer, size_t buffer_len);

struct font* CdOpenFont(struct typeface* tf, font_style_id style);

