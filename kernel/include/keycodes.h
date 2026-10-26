#pragma once

/* Keycodes for 'normal' characters just use A-Z (uppercase), 0-9, or the "unshifted" symbol */

#define KEYCODE_ENTER           10  //'\n'
#define KEYCODE_BACKSPACE       8   //'\b'
#define KEYCODE_TAB             9   //'\t'
#define KEYCODE_ESCAPE          27  // '\x1B'
#define KEYCODE_LEFT_ARROW      128
#define KEYCODE_RIGHT_ARROW     129
#define KEYCODE_UP_ARROW        130
#define KEYCODE_DOWN_ARROW      131

#define KEYCODE_LCTRL           1
#define KEYCODE_LSHIFT          2
#define KEYCODE_LALT            3
#define KEYCODE_RCTRL           4
#define KEYCODE_RSHIFT          5
#define KEYCODE_RALT            6
#define KEYCODE_CAPSLOCK        7
#define KEYCODE_NUMLOCK         11
#define KEYCODE_SCROLLLOCK      12

#define KEYCODE_NUMPAD_0        0x90
#define KEYCODE_NUMPAD_1        0x91
#define KEYCODE_NUMPAD_2        0x92
#define KEYCODE_NUMPAD_3        0x93
#define KEYCODE_NUMPAD_4        0x94
#define KEYCODE_NUMPAD_5        0x95
#define KEYCODE_NUMPAD_6        0x96
#define KEYCODE_NUMPAD_7        0x97
#define KEYCODE_NUMPAD_8        0x98
#define KEYCODE_NUMPAD_9        0x99
#define KEYCODE_NUMPAD_MUL      0x9A
#define KEYCODE_NUMPAD_ADD      0x9B
#define KEYCODE_NUMPAD_DOT      0x9C
#define KEYCODE_NUMPAD_SUB      0x9D
#define KEYCODE_NUMPAD_DIV      0x9E
#define KEYCODE_NUMPAD_ENTER    0x9F

#define KEYCODE_INSERT			    0xB0
#define KEYCODE_END				      0xB1
#define KEYCODE_PAGEUP			    0xB2
#define KEYCODE_PAGEDOWN		    0xB3
#define KEYCODE_HOME			      0xB4
#define KEYCODE_DEL				      0xB5

#define KEYCODE_F1              0xA1
#define KEYCODE_F2              0xA2
#define KEYCODE_F3              0xA3
#define KEYCODE_F4              0xA4
#define KEYCODE_F5              0xA5
#define KEYCODE_F6              0xA6
#define KEYCODE_F7              0xA7
#define KEYCODE_F8              0xA8
#define KEYCODE_F9              0xA9
#define KEYCODE_F10             0xAA
#define KEYCODE_F11             0xAB
#define KEYCODE_F12             0xAC



#define MODIFIER_LCTRL          1
#define MODIFIER_LSHIFT         2
#define MODIFIER_LALT           4
#define MODIFIER_RCTRL          8
#define MODIFIER_RSHIFT         16
#define MODIFIER_RALT           32
#define MODIFIER_CAPSLOCK       64
#define MODIFIER_NUMLOCK        128
#define MODIFIER_SCROLLLOCK     256
