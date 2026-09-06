#include <common.h>
#include <interrupt.h>
#include <log.h>
#include <string.h>
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
static bool extended = false;

static void Ps2KeyboardSetLEDs(void) {
    uint8_t data = caps_lock_on << 2;
    if (Ps2DeviceWrite(0xED, false) != 0) return;
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
    LogPrintf("%c", c);
}

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
        Ps2KeyboardTranslateSet1(scancode);
    }
}

static int Ps2KeyboardGetScancodeSet(void) {
    if (Ps2DeviceWrite(0xF0, false) != 0) return -1;
    if (Ps2DeviceWrite(0x00, false) != 0) return -1;

    uint8_t set;
    if (Ps2DeviceRead(&set) != 0) return -1;

    if (set == 0x43 || set == 1) return 1;
    if (set == 0x41 || set == 2) return 2;
    if (set == 0x3F || set == 3) return 3;
    return -1;
}

static int Ps2KeyboardSetScancodeSet(int num) {
    if (Ps2DeviceWrite(0xF0, false) != 0) return -1;
    if (Ps2DeviceWrite((uint8_t)num, false) != 0) return -1;
    
    int result = Ps2KeyboardGetScancodeSet();
    return (result == num) ? 0 : -1;
}

void InitPs2Keyboard(void) {
    LogString("[keyboard] detecting translation...\n");
    
    /* Read controller config to check translation bit. */
    uint8_t config = Ps2ControllerGetConfiguration();
    bool translation_on = (config & (1 << 6)) != 0;
    
    LogString(translation_on ? "[keyboard] translation ON\n" : "[keyboard] translation OFF\n");

    /* If translation is on, disable it. We want set 1 scancodes from the keyboard
     * directly, not set 2 converted by the controller. */
    if (translation_on) {
        LogString("[keyboard] disabling translation...\n");
        config &= ~(1 << 6);
        Ps2ControllerSetConfiguration(config);
    }

    /* Read current scancode set. */
    int current_set = Ps2KeyboardGetScancodeSet();
    LogPrintf("[keyboard] current scancode set: %d\n", current_set);

    /* Make sure we're in set 1. */
    if (current_set != 1) {
        LogString("[keyboard] switching to set 1...\n");
        Ps2KeyboardSetScancodeSet(1);
    }

    /* Enable scanning. */
    if (Ps2DeviceWrite(0xF4, false) != 0) {
        LogString("[keyboard] WARNING: enable-scanning not ACKed\n");
    }

    /* Flush stray bytes. */
    Ps2ControllerFlushOutputBuffer();

    /* Register handler. */
    RegisterInterruptHandler(1, Ps2KeyboardIrqHandler);
    
    LogString("[keyboard] init done\n");
}