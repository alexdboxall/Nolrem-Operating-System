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

/*
 * Font header layout (at `file_offset` within the typeface file, all
 * multi-byte values little endian):
 *
 *   +0   u32  min_char
 *   +4   u32  num_chars
 *   +8   u16  fixed_width
 *   +10  u16  avg_advance
 *   +12  u16  ascend_px
 *   +14  u16  height_px          height of the tallest *bitmap*
 *   +16  u16  lineheight_px      baseline-to-baseline distance
 *   +18  u8   is_large_font
 *   +19  u8   is_32bit_glyph_offsets
 *   +20  u32  gap_table_offset   relative to font start, 0 = none
 *   +24  u32  kern_table_offset  relative to font start, 0 = none
 *   +28  u32  glyph_table_offset relative to font start
 *
 * Kern table layout (at `file_offset + kern_table_offset`):
 *
 *   u32 num_pairs
 *   num_pairs entries, sorted ascending by (left, right):
 *       left    unsigned, codepoint_size wide   (glyph index, not code point)
 *       right   unsigned, codepoint_size wide   (glyph index, not code point)
 *       amount  signed,   codepoint_size wide   (pixels, added to the advance)
 *
 * `codepoint_size` is derived from num_chars (see CdOpenFont), so every field
 * in the table is the same width and is always wide enough for a glyph index.
 */
struct font {
    struct user_obj_header hdr;
    struct typeface* tf;
    font_style_id requested_style;
    font_style_id underlying_style;
    int index;
    uint8_t codepoint_size : 2;

    /* Read from the typeface file data */
    uint32_t file_offset;
    uint32_t font_length;

    /* Read from the font file data */
    uint32_t min_char;
    uint32_t num_chars;
    uint16_t fixed_width;
    uint16_t avg_advance;
    uint16_t ascend_px;
    uint16_t height_px;       /* height of the tallest bitmap in the font */
    uint16_t lineheight_px;   /* distance from one baseline to the next */
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

/* Kerning adjustment in pixels (usually <= 0) to apply between two characters.
 * Takes code points. Returns 0 if the font has no kern table, either character
 * has no glyph, the pair isn't in the table, or the file is corrupt. */
int CdGetKern(struct font* fn, uint32_t left, uint32_t right);