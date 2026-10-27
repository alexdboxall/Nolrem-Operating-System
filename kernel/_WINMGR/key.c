#include <common.h>
#include <stdbool.h>
#include <keycodes.h>

/* Raw keycode to a different keycode */
export uint8_t WmTranslateNumpad(uint8_t key, uint16_t modifiers) {
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
		case KEYCODE_NUMPAD_MUL: return '*';
		case KEYCODE_NUMPAD_ADD: return '+';
		case KEYCODE_NUMPAD_SUB: return '-';
		case KEYCODE_NUMPAD_DIV: return '/';
		case KEYCODE_NUMPAD_ENTER: return '\n';
	}
	return key;
}

/* Raw keycode to ASCII */
export char WmTranslateKey(uint8_t key, uint16_t modifiers) {
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


