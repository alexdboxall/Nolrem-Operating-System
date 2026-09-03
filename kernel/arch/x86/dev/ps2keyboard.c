
#include <common.h>
#include <interrupt.h>
#include <log.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <machine/x86.h>
#include "ps2controller.h"
#include <keycodes.h>

static const char set1_map_lower_norm[] = 
    "  1234567890-=  qwertyuiop[]  asdfghjkl;'` \\zxcvbnm,./ *       "
    "        789-456+1230.                                           ";

static const char set1_map_upper_norm[] = 
    "  !@#$%^&*()_+  QWERTYUIOP{}  ASDFGHJKL:\"~ |ZXCVBNM<>? *       "
    "        789-456+1230.                                           ";

static const char set1_map_lower_caps[] = 
    "  1234567890-=  QWERTYUIOP[]  ASDFGHJKL;'` \\ZXCVBNM,./ *       "
    "        789-456+1230.                                           ";

static const char set1_map_upper_caps[] = 
    "  !@#$%^&*()_+  qwertyuiop{}  asdfghjkl:\"~ |zxcvbnm<>? *       "
    "        789-456+1230.                                           ";


#define SET1_ENTER          0x1C
#define SET1_BACKSPACE      0x0E
#define SET1_TAB            0x0F
#define SET1_ESCAPE         0x01
#define SET1_SHIFT          0x2A
#define SET1_SHIFT_R        0x36
#define SET1_CTRL           0x1D
#define SET1_EXTENSION      0xE0
#define SET1_CAPS_LOCK      0x3A

#define SET1_EXT_UP_ARROW       0x48
#define SET1_EXT_DOWN_ARROW     0x50
#define SET1_EXT_LEFT_ARROW     0x4B
#define SET1_EXT_RIGHT_ARROW    0x4D

static bool release_mode = false;
static bool control_held = false;
static bool shift_held = false;
static bool shift_r_held = false;
static bool caps_lock_on = false;

static void Ps2KeyboardSetLEDs(void) {
    uint8_t data = caps_lock_on << 2;

    Ps2DeviceWrite(0xED, false);
    Ps2DeviceWrite(data, false);
}

static uint8_t TranslateCharacter(uint8_t scancode, bool shift) {
    static const char* set1_tables[] = {
        set1_map_lower_norm, set1_map_upper_norm, 
        set1_map_lower_caps, set1_map_upper_caps, 
    };  
    const char* table = set1_tables[shift + caps_lock_on * 2];
    return table[scancode];
}

static void SendKeystrokeConsole(char c) {
    LogCharacter(c);
}

static bool extended = false;
static void Ps2KeyboardTranslateSet1(uint8_t scancode) {
    if (scancode == 0xFA || scancode == 0xFE || scancode == 0xAA) {
        extended = false;
        return;
    }

    if (scancode & 0x80) {
        release_mode = true;
        scancode &= 0x7F;
    }

    uint8_t c = 0;
    if (extended) {
        switch (scancode) {
        case SET1_EXT_UP_ARROW:
            c = KEYCODE_UP_ARROW;
            break;
        case SET1_EXT_DOWN_ARROW:
            c = KEYCODE_DOWN_ARROW;
            break;
        case SET1_EXT_LEFT_ARROW:
            c = KEYCODE_LEFT_ARROW;
            break;
        case SET1_EXT_RIGHT_ARROW:
            c = KEYCODE_RIGHT_ARROW;
            break;
        }

    } else {
        switch (scancode) {
        case SET1_ENTER:
            c = KEYCODE_ENTER;
            break;

        case SET1_BACKSPACE:
            c = KEYCODE_BACKSPACE;
            break;

        case SET1_TAB:
            c = KEYCODE_TAB;
            break;

        case SET1_ESCAPE:
            c = KEYCODE_ESCAPE;
            break;

        case SET1_SHIFT:
            shift_held = !release_mode;
            break;

        case SET1_SHIFT_R:
            shift_r_held = !release_mode;
            break;

        case SET1_CTRL:
            control_held = !release_mode;
            break;

        case SET1_CAPS_LOCK:
            if (!release_mode) {
                caps_lock_on = !caps_lock_on;
                Ps2KeyboardSetLEDs();
            }
            break;

        default:
            if (scancode < strlen(set1_map_lower_norm)) {
                c = TranslateCharacter(scancode, shift_held ^ shift_r_held);
            }
        }
    }

    bool send_it = c != 0 && !release_mode;
    release_mode = false;
    if (send_it) {
        if (control_held) {
            if (islower(c) || (c >= '@' && c <= '_' && !isupper(c))) {
                SendKeystrokeConsole(c - (islower(c) ? (32 + '@') : '@'));
            }
        } else if (c == KEYCODE_UP_ARROW) {
            SendKeystrokeConsole('\x1B');
            SendKeystrokeConsole('[');
            SendKeystrokeConsole('A');

        } else if (c == KEYCODE_DOWN_ARROW) {
            SendKeystrokeConsole('\x1B');
            SendKeystrokeConsole('[');
            SendKeystrokeConsole('B');

        } else if (c == KEYCODE_RIGHT_ARROW) {
            SendKeystrokeConsole('\x1B');
            SendKeystrokeConsole('[');
            SendKeystrokeConsole('C');

        } else if (c == KEYCODE_LEFT_ARROW) {
            SendKeystrokeConsole('\x1B');
            SendKeystrokeConsole('[');
            SendKeystrokeConsole('D');
        
        } else {
            SendKeystrokeConsole(c);
        }
    }
    extended = false;
}

static void Ps2KeyboardIrqHandler(struct x86_regs*) {
    uint8_t status = inb(0x64);
    if ((status & 0x01) == 0 || (status & 0x20) != 0) {
        return;
    }

	uint8_t scancode = inb(0x60);
    if (scancode == 0xE0) {
        extended = true;
    } else {
        Ps2KeyboardTranslateSet1((size_t) scancode);
    }
}

static int Ps2KeyboardGetScancodeSet(void) {
    Ps2DeviceWrite(0xF0, false);
    Ps2DeviceWrite(0, false);

    uint8_t set = Ps2DeviceRead();
    /*
     * Technically we should get 0x43, 0x41 or 0x3F, but Bochs returns 1, 2, or
     * 3 instead. There's probably some crusty USB to PS/2 emulation out there
     * that acts the same, so we'll check for both.
     */
    if (set == 0x43 || set == 1) {
        return 1;
    } else if (set == 0x41 || set == 2) {
        return 2;
    } else if (set == 0x3F || set == 3) {
        return 3;
    } else {
        return -1;
    }
}

static int Ps2KeyboardSetScancodeSet(int num) {
    Ps2DeviceWrite(0xF0, false);
    Ps2DeviceWrite(num, false);
    return num == Ps2KeyboardGetScancodeSet() ? 0 : EIO;
}

static void Ps2KeyboardSetTranslation(bool enable) {
    uint8_t config = Ps2ControllerGetConfiguration();
    if (enable) {
        config |= 1 << 6;
    } else {
        config &= ~(1 << 6);
    }
    Ps2ControllerSetConfiguration(config);
}

void InitPs2Keyboard(void) {
    Ps2KeyboardSetTranslation(true);
    Ps2KeyboardSetScancodeSet(1);

    int res = Ps2ControllerTestPort(false);
    if (res != 0) {
        LogString("PS/2 keyboard self test failed.\n");
    }

    bool translation_on = Ps2ControllerGetConfiguration() & (1 << 6);
    int set = Ps2KeyboardGetScancodeSet();

    if (set == 3 || set == -1) {
        int res = Ps2KeyboardSetScancodeSet(translation_on ? 2 : 1);
        if (res != 0) {
            LogStringAndHexLine("[keybrd]: couldn't switch out of set %d!\n", set);
        }

    } else if (set == 1 && translation_on) {
        int res = Ps2KeyboardSetScancodeSet(2);
        if (res != 0) {
            Ps2KeyboardSetScancodeSet(1);
            Ps2KeyboardSetTranslation(false);
        }

    } else if (set == 2 && !translation_on) {
        int res = Ps2KeyboardSetScancodeSet(1);
        if (res != 0) {
            Ps2KeyboardSetScancodeSet(2);
            Ps2KeyboardSetTranslation(true);
        }
    }

    while (inb(0x64) & 0x01) {
        inb(0x60);
    }

    RegisterInterruptHandler(1, Ps2KeyboardIrqHandler);
    Ps2ControllerEnableDevice(false);
}
