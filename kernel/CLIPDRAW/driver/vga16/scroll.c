#include "vga16.h"

static void CopyMaskedRow(volatile uint8_t* dest_row, volatile uint8_t* src_row, 
                            int start_byte, int end_byte, uint8_t start_mask, uint8_t end_mask) {
    uint8_t dummy;

    if (start_byte == end_byte) {
        outb(VGA_GC_INDEX, 8);
        outb(VGA_GC_DATA, start_mask & end_mask);
        
        dummy = src_row[start_byte];
        dest_row[start_byte] = dummy;
        return;
    }

    outb(VGA_GC_INDEX, 8);
    outb(VGA_GC_DATA, start_mask);
    dummy = src_row[start_byte];
    dest_row[start_byte] = dummy;

    if (end_byte - start_byte > 1) {
        outb(VGA_GC_INDEX, 8);
        outb(VGA_GC_DATA, 0xFF);
        for (int b = start_byte + 1; b < end_byte; b++) {
            dummy = src_row[b];
            dest_row[b] = dummy;
        }
    }

    outb(VGA_GC_INDEX, 8);
    outb(VGA_GC_DATA, end_mask);
    dummy = src_row[end_byte];
    dest_row[end_byte] = dummy;
}

static void ClearMaskedRow(volatile uint8_t* dest_row, int start_byte, int end_byte, 
                             uint8_t start_mask, uint8_t end_mask) {
    if (start_byte == end_byte) {
        outb(VGA_GC_INDEX, 8);
        outb(VGA_GC_DATA, start_mask & end_mask);
        dest_row[start_byte] = 0;
        return;
    }

    outb(VGA_GC_INDEX, 8);
    outb(VGA_GC_DATA, start_mask);
    dest_row[start_byte] = 0;

    if (end_byte - start_byte > 1) {
        outb(VGA_GC_INDEX, 8);
        outb(VGA_GC_DATA, 0xFF);
        for (int b = start_byte + 1; b < end_byte; b++) {
            dest_row[b] = 0;
        }
    }

    outb(VGA_GC_INDEX, 8);
    outb(VGA_GC_DATA, end_mask);
    dest_row[end_byte] = 0;
}

void VgaScrollRect(struct graphics_driver*, int x_start, int y_start, int x_end, int y_end, int delta_y) {
    volatile uint8_t* vram = VRAM_BASE;

    if (delta_y == 0) return;
    if (x_start < 0) x_start = 0;
    if (x_end > 639) x_end = 639;
    if (y_start < 0) y_start = 0;
    if (y_end > 479) y_end = 479;
    if (x_start > x_end || y_start > y_end) return;

    int start_byte = x_start / 8;
    int end_byte   = x_end / 8;

    uint8_t start_mask = 0xFF >> (x_start & 7);
    uint8_t end_mask   = 0xFF << (7 - (x_end & 7));

    int window_height = (y_end - y_start) + 1;

    outb(VGA_GC_INDEX, 5);
    uint8_t orig_mode = inb(VGA_GC_DATA);

    if (delta_y >= window_height || delta_y <= -window_height) {
        outb(VGA_SEQ_INDEX, 2);  
        outb(VGA_SEQ_DATA, 0x0F); 
        outb(VGA_GC_INDEX, 5);   
        outb(VGA_GC_DATA, orig_mode & ~3);
        for (int y = y_start; y <= y_end; y++) {
            ClearMaskedRow(vram + (y * BYTES_PER_ROW), start_byte, end_byte, start_mask, end_mask);
        }
        outb(VGA_GC_INDEX, 8);   
        outb(VGA_GC_DATA, 0xFF);
        return;
    }

    outb(VGA_SEQ_INDEX, 2);
    outb(VGA_SEQ_DATA, 0x0F);

    if (delta_y > 0) {
        // --- SCROLL UP ---
        outb(VGA_GC_INDEX, 5);
        outb(VGA_GC_DATA, (orig_mode & ~3) | 1);

        for (int y = y_start; y <= (y_end - delta_y); y++) {
            volatile uint8_t* dest_row = vram + (y * BYTES_PER_ROW);
            volatile uint8_t* src_row  = vram + ((y + delta_y) * BYTES_PER_ROW);
            CopyMaskedRow(dest_row, src_row, start_byte, end_byte, start_mask, end_mask);
        }

        outb(VGA_GC_INDEX, 5);
        outb(VGA_GC_DATA, orig_mode & ~3);

        for (int y = (y_end - delta_y) + 1; y <= y_end; y++) {
            ClearMaskedRow(vram + (y * BYTES_PER_ROW), start_byte, end_byte, start_mask, end_mask);
        }
    } 
    else {
        // --- SCROLL DOWN ---
        int abs_delta = -delta_y;

        outb(VGA_GC_INDEX, 5);
        outb(VGA_GC_DATA, (orig_mode & ~3) | 1);

        for (int y = y_end; y >= (y_start + abs_delta); y--) {
            volatile uint8_t* dest_row = vram + (y * BYTES_PER_ROW);
            volatile uint8_t* src_row  = vram + ((y - abs_delta) * BYTES_PER_ROW);
            CopyMaskedRow(dest_row, src_row, start_byte, end_byte, start_mask, end_mask);
        }

        outb(VGA_GC_INDEX, 5);
        outb(VGA_GC_DATA, orig_mode & ~3);

        for (int y = y_start; y < (y_start + abs_delta); y++) {
            ClearMaskedRow(vram + (y * BYTES_PER_ROW), start_byte, end_byte, start_mask, end_mask);
        }
    }

    outb(VGA_GC_INDEX, 8);   
    outb(VGA_GC_DATA, 0xFF);
    outb(VGA_GC_INDEX, 5);   
    outb(VGA_GC_DATA, orig_mode);
}

