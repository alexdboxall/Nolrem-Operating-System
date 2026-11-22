#pragma once

#include <stdint.h>

/*
 * Input device -> consumer plumbing.
 *
 * Device drivers report what the hardware did with the KeReport...() functions,
 * and have no idea who is listening. Whichever component consumes input (the
 * window manager) registers a handler with the matching KeSet...Handler().
 * Input that arrives while nobody is registered is dropped.
 *
 * Handlers are called straight from the driver's interrupt handler, so they
 * must be IRQ-safe: no blocking, no heap allocation.
 */

/* click_bits: bit 0 = left, bit 1 = right, bit 2 = middle. */
typedef void (*ke_mouse_handler_t)(uint8_t click_bits, int16_t delta_x, int16_t delta_y,
                                   int scroll_x, int scroll_y);

/* Passing NULL removes the handler. */
void KeSetMouseHandler(ke_mouse_handler_t handler);
void KeReportMouseInput(uint8_t click_bits, int16_t delta_x, int16_t delta_y,
                        int scroll_x, int scroll_y);
