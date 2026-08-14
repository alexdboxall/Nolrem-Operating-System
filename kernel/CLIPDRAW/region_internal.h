#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct region;

struct region_data {
    uint16_t num_bands;
    int16_t trans_x;
    int16_t trans_y;
    uint8_t band_data[];
};

struct region_build_context {
    int current_spans;
    int16_t* current_points;
    int current_height;
    size_t current_ptr;

    int16_t prev_x0[16];
    int16_t prev_x1[16];
    int16_t prev_y1;
    int prev_spans;

    uint8_t prev_small_output[16];
    int prev_small_output_len;

    int count0x7F;
};

void BuildNewRegion(struct region* rgn, int16_t y0, struct region_build_context* ctxt);
void AddScanline(
    struct region* rgn,
    struct region_build_context* ctxt,
    int num_spans,
    int16_t* x_points,
    bool known_to_be_same_as_prev
);

static inline void FinishRegion(struct region* rgn, struct region_build_context* ctxt) {
    AddScanline(rgn, ctxt, -1, NULL, false);
}