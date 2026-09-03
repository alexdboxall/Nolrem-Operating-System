#include <common.h>
#include <interrupt.h>
#include <log.h>
#include <errno.h>
#include <../CLIPDRAW/api.h>
#include <machine/x86.h>
#include "ps2controller.h"

extern void WmHandleMouseInput(uint8_t click_bits, int16_t delta_x, int16_t delta_y, int scrollx, int scrolly);

/* Standard PS/2 Mouse Commands */
#define PS2_MOUSE_CMD_ENABLE_DATA_REPORTING  0xF4
#define PS2_MOUSE_CMD_SET_DEFAULTS            0xF6
#define PS2_MOUSE_CMD_SET_SAMPLE_RATE         0xF3
#define PS2_MOUSE_CMD_GET_DEVICE_ID           0xF2

/*
 * Device IDs returned by 0xF2 after the relevant sample-rate "magic knock"
 * sequence has been sent (see Ps2MouseTryEnableWheel/Ps2MouseTryEnable5Button
 * below). Note that the 5-button (IntelliMouse Explorer) variant does NOT
 * add a 5th packet byte - it reuses the same 4th byte as the plain wheel
 * mouse, packing buttons 4/5 into its high bits alongside the Z delta.
 */
#define PS2_MOUSE_ID_STANDARD       0x00
#define PS2_MOUSE_ID_SCROLL_WHEEL   0x03  /* IntelliMouse: adds a Z (wheel) axis */
#define PS2_MOUSE_ID_5_BUTTON       0x04  /* IntelliMouse Explorer: Z axis + buttons 4/5 */

static uint8_t mouse_cycle = 0;
static uint8_t mouse_bytes[4];

/* Negotiated packet size for the currently attached device: 3 for a
 * standard mouse, 4 for a scroll-wheel or 5-button mouse. */
static uint8_t mouse_packet_size = 3;
static bool mouse_has_wheel = false;
static bool mouse_has_5buttons = false;

static void Ps2MouseSetSampleRate(uint8_t rate) {
    Ps2DeviceWrite(PS2_MOUSE_CMD_SET_SAMPLE_RATE, true);
    Ps2DeviceWrite(rate, true);
}

static uint8_t Ps2MouseGetId(void) {
    Ps2DeviceWrite(PS2_MOUSE_CMD_GET_DEVICE_ID, true);
    return Ps2DeviceRead();
}

/*
 * Sends the standard sample-rate "magic knock" (200, 100, 80) that tells an
 * IntelliMouse-compatible device to start reporting a 4th (Z/wheel) byte.
 * Returns true if the device acknowledged by reporting ID 0x03.
 */
static bool Ps2MouseTryEnableWheel(void) {
    Ps2MouseSetSampleRate(200);
    Ps2MouseSetSampleRate(100);
    Ps2MouseSetSampleRate(80);
    return Ps2MouseGetId() == PS2_MOUSE_ID_SCROLL_WHEEL;
}

/*
 * Sends the second-stage knock (200, 200, 80) that upgrades an
 * already-wheel-enabled IntelliMouse Explorer device to also report
 * buttons 4/5 in the high bits of that same 4th byte. Only meaningful
 * after Ps2MouseTryEnableWheel() has already succeeded.
 */
static bool Ps2MouseTryEnable5Button(void) {
    Ps2MouseSetSampleRate(200);
    Ps2MouseSetSampleRate(200);
    Ps2MouseSetSampleRate(80);
    return Ps2MouseGetId() == PS2_MOUSE_ID_5_BUTTON;
}

static void Ps2MouseIrqHandler(struct x86_regs*) {
    uint8_t status = inb(0x64);
    /* Ensure there is data to read and that it actually came from the auxiliary device (mouse) */
    if ((status & 0x01) == 0 || (status & 0x20) == 0) {
        return;
    }

    uint8_t byte = inb(0x60);

    /* 
     * Byte 0 alignment check: Bit 3 must ALWAYS be 1 in the first byte of
     * every packet, regardless of packet size. If out of sync, reset cycle
     * count to realign stream.
     */
    if (mouse_cycle == 0 && !(byte & 0x08)) {
        return;
    }

    mouse_bytes[mouse_cycle++] = byte;

    if (mouse_cycle == mouse_packet_size) {
        mouse_cycle = 0;

        uint8_t flags = mouse_bytes[0];
        
        /* Check sign overflow flags (bits 6 and 7); discard invalid packet if overflow occurred */
        if (flags & 0xC0) {
            return;
        }

        /* 
         * Extract button states:
         * bit 0 = Left, bit 1 = Right
         * Combine to format: (bit 1 = Right, bit 0 = Left)
         * Buttons 4/5, when the attached mouse supports them, are OR'd in
         * further below from the extended 4th packet byte.
         */
        uint8_t click_bits = flags & 0x03;

        /* Extract X/Y deltas and sign-extend using bit 4 (X sign) and bit 5 (Y sign) */
        int16_t delta_x = (int16_t)mouse_bytes[1];
        if (flags & (1 << 4)) {
            delta_x |= 0xFF00;
        }

        int16_t delta_y = (int16_t)mouse_bytes[2];
        if (flags & (1 << 5)) {
            delta_y |= 0xFF00;
        }

        /* PS/2 mouse Y axis is inverted relative to standard screen space coordinates */
        delta_y = -delta_y;

        int scrollx = 0;
        int scrolly = 0;

        if (mouse_packet_size == 4) {
            uint8_t z_byte = mouse_bytes[3];

            if (mouse_has_5buttons) {
                /*
                 * Explorer packet: low nibble is a signed 4-bit wheel delta
                 * (-8..7), bits 4/5 are buttons 4 and 5.
                 */
                uint8_t z_nibble = z_byte & 0x0F;
                if (z_nibble & 0x08) {
                    z_nibble |= 0xF0; /* sign-extend 4 bits -> 8 bits */
                }
                scrolly = (int8_t)z_nibble;

                if (z_byte & 0x10) click_bits |= 0x08; /* button 4 */
                if (z_byte & 0x20) click_bits |= 0x10; /* button 5 */
            } else {
                /* Plain IntelliMouse packet: the whole byte is the delta. */
                scrolly = (int8_t)z_byte;
            }
        }

        WmHandleMouseInput(click_bits, delta_x, delta_y, scrollx, scrolly);
    }
}

void InitPs2Mouse(void) {
    /* Test second PS/2 port */
    int res = Ps2ControllerTestPort(true);
    if (res != 0) {
        LogString("PS/2 mouse port test failed.\n");
    }

    /* Enable second PS/2 port */
    Ps2ControllerEnableDevice(true);

    /* Reset to defaults before probing capabilities */
    Ps2DeviceWrite(PS2_MOUSE_CMD_SET_DEFAULTS, true);

    /*
     * Probe for scroll wheel / extra button support using the standard
     * sample-rate "magic knock" sequences, and switch packet parsing to
     * match whatever the device turns out to support. This runs before
     * IRQ 12 is registered/unmasked below, so the device-ID response bytes
     * can be read back synchronously here instead of racing the
     * packet-assembly IRQ handler (the mouse isn't streaming yet at this
     * point - that only starts once ENABLE_DATA_REPORTING is sent further
     * down).
     */
    mouse_packet_size = 3;
    mouse_has_wheel = false;
    mouse_has_5buttons = false;

    if (Ps2MouseTryEnableWheel()) {
        mouse_has_wheel = true;
        mouse_packet_size = 4;
        if (Ps2MouseTryEnable5Button()) {
            mouse_has_5buttons = true;
        }
    }

    mouse_cycle = 0;

    /* Set up IRQ handling now that the packet format is known */
    RegisterInterruptHandler(12, Ps2MouseIrqHandler);
    Ps2ControllerSetIrqEnable(true, true);

    /* Enable packet reporting on the mouse device */
    Ps2DeviceWrite(PS2_MOUSE_CMD_ENABLE_DATA_REPORTING, true);

}
