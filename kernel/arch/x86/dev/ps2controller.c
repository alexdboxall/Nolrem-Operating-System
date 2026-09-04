#include <common.h>
#include <interrupt.h>
#include <log.h>
#include <errno.h>
#include <string.h>
#include <machine/x86.h>
#include "ps2keyboard.h"
#include "ps2mouse.h"

#define PS2_STATUS_BIT_OUT_FULL		1
#define PS2_STATUS_BIT_IN_FULL		2
#define PS2_STATUS_BIT_SYSFLAG		4
#define PS2_STATUS_BIT_CONTROLLER	8
#define PS2_STATUS_BIT_MOUSE_DATA	32

#define PS2_WAIT_ITERATIONS     400000
#define PS2_FLUSH_MAX_BYTES     32

/*
 * Shadow copy of the controller configuration byte. If a read times out,
 * we fall back to the last value we successfully read, or the default.
 * This is transparent to callers - Ps2ControllerGetConfiguration() always
 * returns a value, never an error.
 */
static uint8_t config_shadow = 0x44;

static int Ps2Wait(bool writing) {
    for (int timeout = 0; timeout < PS2_WAIT_ITERATIONS; ++timeout) {
        uint8_t status = inb(0x64);

        if (writing) {
            if (!(status & PS2_STATUS_BIT_IN_FULL)) {
                return 0;
            }
        } else {
            if (status & PS2_STATUS_BIT_OUT_FULL) {
                return 0;
            }
        }
    }

    LogString(writing ? "PS/2 INPUT BUFFER STUCK\n" : "PS/2 NO REPLY\n");
    return EIO;
}

/*
 * Core write: always issues the command byte regardless of wait status.
 * Logging only; return value is advisory.
 */
static void Ps2ControllerWrite(uint8_t data) {
    if (Ps2Wait(true) != 0) {
        LogStringAndHexLine("  (retrying after wait timeout) cmd 0x", data);
    }
    if (data == 0xFE) {
        LogString("outb(0x64, 0xFE);\n");
        while (true) {
            asm ("hlt");
        }
    }
    outb(0x64, data);
}

/*
 * Core write-with-parameter: both bytes are always written, even if the
 * wait fails between them. Never return partway through this sequence.
 */
static void Ps2ControllerWrite2(uint8_t data1, uint8_t data2) {
    Ps2ControllerWrite(data1);
    if (Ps2Wait(true) != 0) {
        LogStringAndHexLine("  (retrying after wait timeout) param 0x", data2);
    }
    outb(0x60, data2);
}

/*
 * Core read: out-parameter for the byte, returns EIO on timeout.
 * Callers check the return value to decide if the byte is valid.
 */
static int Ps2ControllerRead(uint8_t* out) {
    if (Ps2Wait(false) != 0) {
        return EIO;
    }
    *out = inb(0x60);
    LogString("PS/2 read [");
    LogHex(*out);
    LogString("] ");
    return 0;
}

void Ps2ControllerFlushOutputBuffer(void) {
    for (int i = 0; i < PS2_FLUSH_MAX_BYTES; ++i) {
        if (!(inb(0x64) & PS2_STATUS_BIT_OUT_FULL)) {
            return;
        }
        inb(0x60);
    }
}

/*
 * PUBLIC API: returns the config value. Tries to read from hardware first;
 * if the read times out, returns the last successfully-read value or the
 * default. Never fails, never blocks indefinitely.
 */
uint8_t Ps2ControllerGetConfiguration(void) {
    uint8_t value;
    Ps2ControllerWrite(0x20);
    if (Ps2ControllerRead(&value) == 0) {
        config_shadow = value;
    }
    return config_shadow;
}

/*
 * PUBLIC API: writes the config value. Bits are sanitised (reserved bits
 * cleared, system flag set). Never fails.
 */
void Ps2ControllerSetConfiguration(uint8_t value) {
    value &= ~0x80;
    value |= PS2_STATUS_BIT_SYSFLAG;
    config_shadow = value;
    Ps2ControllerWrite2(0x60, value);
}

int Ps2DeviceRead(uint8_t* out) {
    return Ps2ControllerRead(out);
}

int Ps2DeviceWrite(uint8_t data, bool port2) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (port2) {
            Ps2ControllerWrite(0xD4);
        }
        Ps2Wait(true);
        outb(0x60, data);

        uint8_t ack = 0;
        bool got_ack = false;

        /* Skip unsolicited 0xAA (keyboard power-on self-test), but not 0x00
         * which is a legitimate device reply. */
        for (int skip = 0; skip < 3; ++skip) {
            if (Ps2DeviceRead(&ack) != 0) {
                break;
            }
            if (ack == 0xAA) {
                continue;
            }
            got_ack = true;
            break;
        }

        if (!got_ack) {
            return EIO;
        }

        if (ack == 0xFA) {
            return 0;
        } else if (ack == 0xFE) {
            continue;
        } else {
            return EIO;
        }
    }

    LogString("PS/2 device write: gave up after\n10 retries\n");
    return ETIMEDOUT;
}

void Ps2ControllerDisableDevice(bool port2) {
    Ps2ControllerWrite(port2 ? 0xA7 : 0xAD);
}

void Ps2ControllerEnableDevice(bool port2) {
    Ps2ControllerWrite(port2 ? 0xA8 : 0xAE);
}

int Ps2ControllerTestPort(bool port2) {
    uint8_t result;
    Ps2ControllerWrite(port2 ? 0xA9 : 0xAB);
    if (Ps2ControllerRead(&result) != 0) {
        return EIO;
    }
    if (result != 0x00) {
        LogStringAndHexLine("PS/2 port test fault code 0x", result);
        return EIO;
    }
    return 0;
}

void Ps2ControllerSetIrqEnable(bool enable, bool port2) {
    uint8_t config = Ps2ControllerGetConfiguration();

    if (enable) {
        config |= (port2 ? 2 : 1);
    } else {
        config &= ~(port2 ? 2 : 1);
    }

    Ps2ControllerSetConfiguration(config);
}

void InitPs2(void) {
    Ps2ControllerDisableDevice(false);
    Ps2ControllerDisableDevice(true);
    Ps2ControllerFlushOutputBuffer();

    Ps2ControllerGetConfiguration();
    Ps2ControllerSetIrqEnable(false, false);
    Ps2ControllerSetIrqEnable(false, true);

    Ps2ControllerEnableDevice(false);  // <-- ENABLE before init
    InitPs2Keyboard();
    Ps2ControllerDisableDevice(false); // Disable again

    Ps2ControllerEnableDevice(true);   // <-- ENABLE before init
    InitPs2Mouse();
    Ps2ControllerDisableDevice(true);  // Disable again

    Ps2ControllerSetIrqEnable(true, false);
    Ps2ControllerSetIrqEnable(true, true);

    Ps2ControllerEnableDevice(false);  // Final enable
    Ps2ControllerEnableDevice(true);
}
