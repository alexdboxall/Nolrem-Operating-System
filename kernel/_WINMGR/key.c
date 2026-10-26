#include <common.h>
#include <stdbool.h>

export char WmTranslateKey(uint8_t key, bool shift, bool caps) {
	if (key >= 'A' && key <= 'Z') {
		if (!(caps ^ shift)) {
			return key + 'a' - 'A';
		} else {
			return key;
		}
	}
	if (!shift) return key;
	if (key >= '0' && key <= '9') {
		char lookup[] = ")!@#$%^&*(";
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
		return key;
	}
}


