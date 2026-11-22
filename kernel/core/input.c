#include <common.h>
#include <input.h>
#include <stdatomic.h>

static _Atomic(ke_mouse_handler_t) mouse_handler = NULL;

export void KeSetMouseHandler(ke_mouse_handler_t handler) {
    atomic_store(&mouse_handler, handler);
}

export void KeReportMouseInput(uint8_t click_bits, int16_t delta_x, int16_t delta_y,
                               int scroll_x, int scroll_y) {
    ke_mouse_handler_t handler = atomic_load(&mouse_handler);
    if (handler != NULL) {
        handler(click_bits, delta_x, delta_y, scroll_x, scroll_y);
    }
}
