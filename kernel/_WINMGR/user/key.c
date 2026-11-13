#include <common.h>
#include <stdbool.h>
#include <keycodes.h>
#include "../winmgr_internal.h"

/* Raw keycode to a different keycode */
export pageableuserexec uint8_t TranslateNumpad(uint8_t key, uint16_t modifiers) {
	bool numlock = modifiers & MODIFIER_NUMLOCK;
	if (key >= KEYCODE_NUMPAD_0 && key <= KEYCODE_NUMPAD_9) {
		if (numlock) return key - KEYCODE_NUMPAD_0 + '0';
		else {
			switch (key) {
			case KEYCODE_NUMPAD_0: return KEYCODE_INSERT;
			case KEYCODE_NUMPAD_1: return KEYCODE_END;
			case KEYCODE_NUMPAD_2: return KEYCODE_DOWN_ARROW;
			case KEYCODE_NUMPAD_3: return KEYCODE_PAGEDOWN;
			case KEYCODE_NUMPAD_4: return KEYCODE_LEFT_ARROW;
			case KEYCODE_NUMPAD_5: return 0;
			case KEYCODE_NUMPAD_6: return KEYCODE_RIGHT_ARROW;
			case KEYCODE_NUMPAD_7: return KEYCODE_HOME;
			case KEYCODE_NUMPAD_8: return KEYCODE_UP_ARROW;
			case KEYCODE_NUMPAD_9: return KEYCODE_PAGEUP;
			}
		}
	} 
	switch (key) {
		case KEYCODE_NUMPAD_DOT: return numlock ? '.' : KEYCODE_DEL;
		case KEYCODE_NUMPAD_MUL: return '*';  /* not a true keycode actually - but TranslateKey passes it through ok */
		case KEYCODE_NUMPAD_ADD: return '+';  /* not a true keycode actually - but TranslateKey passes it through ok */
		case KEYCODE_NUMPAD_SUB: return '-';
		case KEYCODE_NUMPAD_DIV: return '/';
		case KEYCODE_NUMPAD_ENTER: return KEYCODE_ENTER;
	}
	return key;
}

/* Raw keycode to ASCII */
export pageableuserexec char TranslateKey(uint8_t key, uint16_t modifiers) {
	bool shift = !!(modifiers & MODIFIER_LSHIFT) || !!(modifiers & MODIFIER_RSHIFT);
	bool ctrl = !!(modifiers & MODIFIER_LCTRL) || !!(modifiers & MODIFIER_RCTRL);
	bool alt = !!(modifiers & MODIFIER_LALT) || !!(modifiers & MODIFIER_RALT);
	bool numlock = modifiers & MODIFIER_NUMLOCK;
	bool caps = modifiers & MODIFIER_CAPSLOCK;
	if (alt || ctrl) {
		return 0;
	}
	if (key >= KEYCODE_NUMPAD_0 && key <= KEYCODE_NUMPAD_9) {
		if (numlock) return key - KEYCODE_NUMPAD_0 + '0';
		else return 0;
	} 
	switch (key) {
		case KEYCODE_NUMPAD_DOT: return numlock ? '.' : 0;
		case KEYCODE_NUMPAD_MUL: return '*';
		case KEYCODE_NUMPAD_ADD: return '+';
		case KEYCODE_NUMPAD_SUB: return '-';
		case KEYCODE_NUMPAD_DIV: return '/';
		case KEYCODE_NUMPAD_ENTER: return '\n';
	}

	switch (key) {
		case KEYCODE_ENTER: return '\n';
		case KEYCODE_TAB: return '\t';
		case KEYCODE_ESCAPE: return '\x1B';
		case KEYCODE_SPACE: return ' ';
		case KEYCODE_BACKSPACE: return '\b';
	}
	
	if (key >= 'A' && key <= 'Z') {
		if (!(caps ^ shift)) {
			return key + 'a' - 'A';
		} else {
			return key;
		}
	}
	if (!shift) return key;
	if (shift && key >= '0' && key <= '9') {
		static const char lookup[] = ")!@#$%^&*(";
		return lookup[key - '0'];
	}

	switch (key) {
	case '`': return '~';
	case '-': return '_';
	case '=': return '+';
	case '[': return '{';
	case ']': return '}';
	case '\\': return '|';
	case ';': return ':';
	case '\'': return '"';
	case ',': return '<';
	case '.': return '>';
	case '/': return '?';
	default:
		return 0;
	}
}

userexec static void SetModifierIf(struct uapp* app, int keycode, int compare, bool keydown, int bit) {
    if (keycode == compare) {
        if (keydown) app->modifier_state |= bit;
        else app->modifier_state &= ~bit;
    }
}

userexec static void UpdateVirtualKeyState(struct uapp* app, struct msg* msg) {
    int keycode = msg->i_arg;
    if (keycode < 0 || keycode >= (int) sizeof(app->keystate) * 8) {
        return;
    }
    bool keydown = msg->type == WM_KEYDOWN;
    if (keydown) {
        app->keystate[keycode >> 3] |= 1 << (keycode & 7);
    } else {
        app->keystate[keycode >> 3] &= ~(1 << (keycode & 7));
    }

    SetModifierIf(app, keycode, KEYCODE_LSHIFT, keydown, MODIFIER_LSHIFT);
    SetModifierIf(app, keycode, KEYCODE_RSHIFT, keydown, MODIFIER_RSHIFT);
    SetModifierIf(app, keycode, KEYCODE_LCTRL, keydown, MODIFIER_LCTRL);
    SetModifierIf(app, keycode, KEYCODE_RCTRL, keydown, MODIFIER_RCTRL);
    SetModifierIf(app, keycode, KEYCODE_LALT, keydown, MODIFIER_LALT);
    SetModifierIf(app, keycode, KEYCODE_RALT, keydown, MODIFIER_RALT);

    if (keydown && keycode == KEYCODE_CAPSLOCK) {
        app->modifier_state ^= MODIFIER_CAPSLOCK;
        
    } else if (keydown && keycode == KEYCODE_NUMLOCK) {
        app->modifier_state ^= MODIFIER_NUMLOCK;
        
    } else if (keydown && keycode == KEYCODE_SCROLLLOCK) {
        app->modifier_state ^= MODIFIER_SCROLLLOCK;
    }
}

/* Every message handed back to the caller goes through here. */
/* TODO: MOVE TO THE USER SIDE! */
export pageableuserexec int UpdateVirtualKeystate(struct uapp* app, struct msg* msg_out, int res) {
    if (res == 0 && (msg_out->type == WM_KEYDOWN || msg_out->type == WM_KEYUP)) {
        UpdateVirtualKeyState(app, msg_out);
    }
    return res;
}

export pageableuserexec bool IsKeyDown(struct uapp* app, int keycode) {
    if (keycode < 0 || keycode >= (int) sizeof(app->keystate) * 8) {
        return false;
    }
    return (app->keystate[keycode >> 3] >> (keycode & 7)) & 1;
}

export pageableuserexec bool IsShiftHeld(struct uapp* app) {
    return !!(app->modifier_state & (MODIFIER_LSHIFT | MODIFIER_RSHIFT));
}

export pageableuserexec bool IsAltHeld(struct uapp* app) {
    return !!(app->modifier_state & (MODIFIER_LALT | MODIFIER_RALT));
}

export pageableuserexec bool IsControlHeld(struct uapp* app) {
    return !!(app->modifier_state & (MODIFIER_LCTRL | MODIFIER_RCTRL));
}

struct shortcut_entry {
    uint16_t id;
    uint8_t key;
    uint8_t flags;
};

struct shortcut_table {
    uint16_t num;
    struct shortcut_entry entries[];
};

#define SHORTCUT_KEYCODE    0
#define SHORTCUT_ASCII      1
#define SHORTCUT_CTRL       2
#define SHORTCUT_ALT        4
#define SHORTCUT_SHIFT      8
#define SHORTCUT_ASCII_USE_MOD  16

export pageableuserexec bool ProcessKeyboardShortcut(struct window* win, struct shortcut_table* st, struct msg msg) {
    /* TODO: change struct win* to the usermode ver */
    if (msg.type != WM_KEYDOWN) {
        return false;
    }

    /* TODO: probably need to allow ancestors instead of exact match?*/
    if (msg.win != win) {
        return false;
    }

    (void )st;
    return false;
    /*
    struct uapp* uapp = ...;
    bool ctrl = IsControlHeld(uapp);
    bool alt = IsAltHeld(uapp);
    bool shift = IsShiftHeld(uapp);

    for (uint16_t i = 0; i < st->num; ++i) {
        struct shortcut_entry e = st->entries[i];
        uint8_t key = msg.i_arg;
        if (e.flags & SHORTCUT_ASCII) {
            key = TranslateKey(key, (e.flags & SHORTCUT_ASCII_USE_MOD) ? uapp->modifier_state : 0);
        }
        if (!ctrl && (e.flags & SHORTCUT_CTRL)) continue;
        if (!alt && (e.flags & SHORTCUT_ALT)) continue;
        if (!shift && (e.flags & SHORTCUT_SHIFT)) continue;
        if (key == e.key) {
            // TODO: the non Wm ver
            WmDispatchMessage((struct msg) {
                .win = win,
                .type = WM_COMMAND,
                .i_arg = e.id
            });
            return true;
        }
    }
    */
}