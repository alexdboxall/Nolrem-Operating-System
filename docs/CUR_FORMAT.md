# Cursor File Format (.CUR) Specification

Oct 23, 2026

## Overview

A .CUR file holds up to 32 mouse cursors. Each cursor may have a black layer and a white layer; any pixel set in neither is transparent. This is version 0. All multi-byte values are little-endian.

Cursors are selected by ID, which is their index in the file's offset table:

| ID | Name | Used for |
| --- | --- | --- |
| 0 | NORMAL | Default arrow |
| 1 | TEXT | I-beam |
| 2 | WAIT | Hourglass |
| 3 | TLDR | Diagonal resize, top-left to bottom-right |
| 4 | VERT | Vertical resize |
| 5 | HORZ | Horizontal resize |
| 6 | HAND | Pointing hand |
| 7 | TRDL | Diagonal resize, top-right to bottom-left |

## File layout and header

A file is an 8-byte header, then an offset table, then the cursor records. Records can sit anywhere after the table, in any order.

| Offset | Size | Field | Value |
| --- | --- | --- | --- |
| 0 | 3 | signature | `C` `U` `R` (0x43 0x55 0x52) |
| 3 | 1 | version | 0x00 |
| 4 | 1 | num_cursors | 1 to 32: number of entries in the offset table |
| 5 | 3 | reserved | 0, 0, 0 |
| 8 | 2 × num_cursors | offsets | uint16 per entry, indexed by cursor ID |

Each offset table entry is:

- **Bits 0 to 14:** absolute offset of the cursor record from the start of the file (maximum 0x7FFF). An offset of 0 means the file has no cursor for that ID.
- **Bit 15:** vertical flip. If set, both layers are mirrored top to bottom when decoded (stored row i is drawn as row lines_tall − 1 − i). The hotspot is not adjusted.

A file with fewer entries than there are cursor IDs simply has no cursors for the higher IDs.

## Cursor record

Each record is a 4-byte header followed by the bitmap rows.

| Offset | Size | Field | Meaning |
| --- | --- | --- | --- |
| 0 | 1 | hotspot_x | int8. Hotspot column, counted from the bitmap's left edge. Positive is inside the bitmap, negative is outside it. |
| 1 | 1 | hotspot_y | int8. Hotspot row, counted from the bitmap's top edge, same sign rule. |
| 2 | 1 | lines_tall | uint8, 0 to 32. Number of stored rows. |
| 3 | 1 | flags_and_width | Bit 7 (0x80) HAS_BLACK_LAYER. Bit 6 (0x40) HAS_WHITE_LAYER. Bits 0 to 5: width, 0 to 32. |
| 4 | rows | black rows | Present only if HAS_BLACK_LAYER. lines_tall rows. |
| next | rows | white rows | Present only if HAS_WHITE_LAYER. lines_tall rows. |

**Row size.** If width is 17 or more, each row is a uint32. Otherwise each row is a uint16. A record is 4 + lines_tall × row size × (number of layers present) bytes.

**Width.** The distance from the bitmap's left edge to the right-most drawn pixel, so every set pixel lies in columns 0 to width − 1. A cursor whose widest point is 20 pixels across, starting at the left edge, has width 20. It sets the row size and the cursor's bounding box.

**Height.** Rows are stored from the top edge (row 0) down to the last row that has a pixel, so lines_tall is that row number + 1. Blank leading rows are allowed.

A layer that would be empty can be left out by clearing its flag. A text cursor, for example, needs only a black layer.

## Bitmap layout and drawing

In every row the most significant bit is the leftmost pixel and the least significant bit is the rightmost. What matters is the pixel's column, so the bit number depends on the row size:

| Row size | Leftmost pixel (column 0) | Column 15 |
| --- | --- | --- |
| uint16 | bit 15 (0x8000) | bit 0 |
| uint32 | bit 31 (0x80000000) | bit 16 |

To draw a cursor at pointer position (px, py):

1. Place the bitmap's top-left corner at (px − hotspot_x, py − hotspot_y).
2. Draw every set pixel of the black layer in black.
3. Draw every set pixel of the white layer in white, on top. Where both layers are set, white wins.
4. Leave every other pixel alone: the screen shows through. This includes all rows past lines_tall and all columns past width.

## Validation

A loader rejects the file or the request, rather than clamping, if any of these fail:

- The file is at least 8 bytes and starts with `C` `U` `R` 0x00 (the version byte must be 0).
- num_cursors is 1 to 32, and the file is at least 8 + 2 × num_cursors bytes.
- The requested ID is less than num_cursors. If its offset (after masking off bit 15) is 0, the cursor is absent, which is reported separately from an invalid file.
- The record's 4-byte header lies inside the file.
- lines_tall is at most 32 and width is at most 32.
- All the rows the flags promise lie inside the file.

Writers set the reserved bytes to 0.

## Worked example: STANDARD.CUR

The standard file is 478 bytes and begins:

```
43 55 52 00  07 00 00 00   signature CUR, version 0, 7 table entries, reserved
16 00 66 00 8A 00 E6 00 26 01 7A 01 00 00   offset table, one uint16 per ID
00 00 13 CB ...                              NORMAL record header, then its rows
```

| ID | Name | Offset | Hotspot | Width × lines | Row size | Layers |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | NORMAL | 0x0016 | (0, 0) | 11 × 19 | uint16 | black, white |
| 1 | TEXT | 0x0066 | (5, 7) | 9 × 16 | uint16 | black |
| 2 | WAIT | 0x008A | (6, 10) | 13 × 22 | uint16 | black, white |
| 3 | TLDR | 0x00E6 | (7, 7) | 15 × 15 | uint16 | black, white |
| 4 | VERT | 0x0126 | (8, 9) | 13 × 20 | uint16 | black, white |
| 5 | HORZ | 0x017A | (11, 7) | 24 × 12 | uint32 | black, white |
| 6 | HAND | 0x0000 | none | none | none | absent |

The NORMAL header `00 00 13 CB` reads as hotspot (0, 0), 19 lines, and flags 0xCB: both layers present, width 11. That makes the record 4 + 19 × 2 × 2 = 80 bytes, so the next record (TEXT) starts at 0x16 + 80 = 0x66. The first black row is `00 80`, the uint16 0x8000, which is a single pixel in column 0.
