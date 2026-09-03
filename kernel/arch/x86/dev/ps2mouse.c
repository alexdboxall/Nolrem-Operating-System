#include <common.h>
#include <interrupt.h>
#include <log.h>
#include <errno.h>
#include <../CLIPDRAW/api.h>
#include <machine/x86.h>
#include "ps2controller.h"

extern void WmHandleMouseInput(uint8_t click_bits, int16_t delta_x, int16_t delta_y, int scrollx, int scrolly);

#define PS2_MOUSE_CMD_ENABLE_DATA_REPORTING  0xF4
#define PS2_MOUSE_CMD_SET_DEFAULTS            0xF6
#define PS2_MOUSE_CMD_SET_SAMPLE_RATE         0xF3
#define PS2_MOUSE_CMD_GET_DEVICE_ID           0xF2

#define PS2_MOUSE_ID_STANDARD       0x00
#define PS2_MOUSE_ID_SCROLL_WHEEL   0x03
#define PS2_MOUSE_ID_5_BUTTON       0x04

static uint8_t mouse_cycle = 0;
static uint8_t mouse_bytes[4];
static uint8_t mouse_packet_size = 3;
static bool mouse_has_wheel = false;
static bool mouse_has_5buttons = false;

static int Ps2MouseSetSampleRate(uint8_t rate) {
    int res = Ps2DeviceWrite(PS2_MOUSE_CMD_SET_SAMPLE_RATE, true);
    if (res != 0) {
        return res;
    }
    return Ps2DeviceWrite(rate, true);
}

static int Ps2MouseGetId(uint8_t* out) {
    int res = Ps2DeviceWrite(PS2_MOUSE_CMD_GET_DEVICE_ID, true);
    if (res != 0) {
        return res;
    }
    return Ps2DeviceRead(out);
}

static bool Ps2MouseTryEnableWheel(void) {
    if (Ps2MouseSetSampleRate(200) != 0) return false;
    if (Ps2MouseSetSampleRate(100) != 0) return false;
    if (Ps2MouseSetSampleRate(80) != 0) return false;

    uint8_t id;
    if (Ps2MouseGetId(&id) != 0) {
        return false;
    }
    return id == PS2_MOUSE_ID_SCROLL_WHEEL;
}

static bool Ps2MouseTryEnable5Button(void) {
    if (Ps2MouseSetSampleRate(200) != 0) return false;
    if (Ps2MouseSetSampleRate(200) != 0) return false;
    if (Ps2MouseSetSampleRate(80) != 0) return false;

    uint8_t id;
    if (Ps2MouseGetId(&id) != 0) {
        return false;
    }
    return id == PS2_MOUSE_ID_5_BUTTON;
}

static void Ps2MouseIrqHandler(struct x86_regs*) {
    uint8_t status = inb(0x64);
    if ((status & 0x01) == 0 || (status & 0x20) == 0) {
        return;
    }

    uint8_t byte = inb(0x60);

    if (mouse_cycle == 0 && !(byte & 0x08)) {
        return;
    }

    mouse_bytes[mouse_cycle++] = byte;

    if (mouse_cycle == mouse_packet_size) {
        mouse_cycle = 0;

        uint8_t flags = mouse_bytes[0];
        
        if (flags & 0xC0) {
            return;
        }

        uint8_t click_bits = flags & 0x07;

        int16_t delta_x = (int16_t)mouse_bytes[1];
        if (flags & (1 << 4)) {
            delta_x |= 0xFF00;
        }

        int16_t delta_y = (int16_t)mouse_bytes[2];
        if (flags & (1 << 5)) {
            delta_y |= 0xFF00;
        }

        delta_y = -delta_y;

        int scrollx = 0;
        int scrolly = 0;

        if (mouse_has_wheel) {
            uint8_t z_byte = mouse_bytes[3];

            if (mouse_has_5buttons) {
                uint8_t z_nibble = z_byte & 0x0F;
                if (z_nibble & 0x08) {
                    z_nibble |= 0xF0;
                }
                scrolly = (int8_t)z_nibble;

                if (z_byte & 0x10) click_bits |= 0x08;
                if (z_byte & 0x20) click_bits |= 0x10;
            } else {
                scrolly = (int8_t)z_byte;
            }
        }

        WmHandleMouseInput(click_bits, delta_x, delta_y, scrollx, scrolly);
    }
}

void InitPs2Mouse(void) {
    LogString("[mouse] init start\n");

    /* Enable port 2. */
    Ps2ControllerEnableDevice(true);

    /* Flush any stray data. */
    Ps2ControllerFlushOutputBuffer();

    /* Reset to defaults. This disables data reporting, so the mouse stays
     * quiet during negotiation. */
    if (Ps2DeviceWrite(PS2_MOUSE_CMD_SET_DEFAULTS, true) != 0) {
        LogString("[mouse] set-defaults not ACKed;\n  no mouse present\n");
        return;
    }

    /* Try to negotiate higher packet modes. Start with 3-byte standard. */
    mouse_packet_size = 3;
    mouse_has_wheel = false;
    mouse_has_5buttons = false;

    if (Ps2MouseTryEnableWheel()) {
        mouse_has_wheel = true;
        mouse_packet_size = 4;
        if (Ps2MouseTryEnable5Button()) {
            mouse_has_5buttons = true;
        }
        LogString(mouse_has_5buttons ? "[mouse] 5-button mode (4-byte\npackets)\n"
                                     : "[mouse] wheel mode (4-byte\npackets)\n");
    } else {
        /* If wheel negotiation fails part-way, reset to standard mode. */
        Ps2DeviceWrite(PS2_MOUSE_CMD_SET_DEFAULTS, true);
        LogString("[mouse] standard mode (3-byte\npackets)\n");
    }

    mouse_cycle = 0;
    Ps2ControllerFlushOutputBuffer();

    /* Enable IRQ12 on the controller. */
    Ps2ControllerSetIrqEnable(true, true);

    /* Register the handler. */
    RegisterInterruptHandler(12, Ps2MouseIrqHandler);

    /* Enable packet reporting. */
    if (Ps2DeviceWrite(PS2_MOUSE_CMD_ENABLE_DATA_REPORTING, true) != 0) {
        LogString("[mouse] enable-reporting (0xF4) not\nACKed\n");
    }

    LogString("[mouse] init done\n");
}
