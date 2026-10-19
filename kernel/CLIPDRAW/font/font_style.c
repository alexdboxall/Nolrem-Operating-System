#include "font_structures.h"

export pageableuserexec font_style_id MakeFontStyle(int points, int points_20ths, bool bold, bool italic) {
    if (points_20ths < 0 || points_20ths >= 20) {
        return INVALID_FONT_STYLE;
    }
    if (points <= 0 || points > (0xFFFF - 19) / 20) {
        return INVALID_FONT_STYLE;
    }
    return points * 20 + points_20ths + (bold ? (1 << 31ULL) : 0) + (italic ? (1 << 30ULL) : 0);
}

export pageableuserexec bool IsFontStyleBold(font_style_id id) {
    if (id == INVALID_FONT_STYLE) {
        return false;
    }
    return (id >> 31) & 1;
}

export pageableuserexec bool IsFontStyleItalic(font_style_id id) {
    if (id == INVALID_FONT_STYLE) {
        return false;
    }
    return (id >> 30) & 1;
}

export int GetFontStylePointsIn20ths(font_style_id id) {
    return id & 0xFFFF;
}
