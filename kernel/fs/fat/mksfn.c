#include <common.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

static bool FatIsValidSFNCharacter(char c) {
    if (isalnum(c)) return true;
    const char* other_valid = "!#$%&'()-@^_`{}~";
    for (int i = 0; other_valid[i]; ++i) {
        if (c == other_valid[i]) {
            return true;
        }
    }
    return false;
}

static const char* FatGetShortFileExtensionStart(const char* lfn) {
    const char* long_ext_start = NULL;
    for (int i = 0; lfn[i]; ++i) {
        if (lfn[i] == '.') {
            long_ext_start = lfn + i + 1;
        }
    }
    if (long_ext_start == NULL) {
        return lfn + strlen(lfn);
    }
    return long_ext_start;
}

static void FatGetShortFileExtension(const char* long_ext_start, char* ext_out) {
    strcpy(ext_out, "   ");
    if (long_ext_start == NULL) {
        return;
    }

    int ext_written = 0;
    for (int i = 0; long_ext_start[i] && ext_written < 3; ++i) {
        char c = long_ext_start[i];
        if (FatIsValidSFNCharacter(c)) {
            ext_out[ext_written++] = toupper(c);
        }
    }
}

static int FatGetShortFileMainName(const char* lfn, const char* ext_start, char* name_out) {
    memset(name_out, ' ', 8);

    int name_written = 0;
    for (int i = 0; lfn[i] && (lfn + i) != ext_start && name_written < 8; ++i) {
        char c = lfn[i];
        if (FatIsValidSFNCharacter(c)) {
            name_out[name_written++] = toupper(c);
        }
    }
    return name_written;
}

export int FatGetShortFilename(const char* lfn, int clash_num, char* sfn_out) {
    const char* long_ext_start = FatGetShortFileExtensionStart(lfn);
    FatGetShortFileExtension(long_ext_start, sfn_out + 8);
    int main_len = FatGetShortFileMainName(lfn, long_ext_start, sfn_out);
    if (main_len == 0) {
        return EINVAL;
    }
    
    if (clash_num == 0) {
        return 0;

    } else if (clash_num < 10) {
        if (main_len > 6) {
            main_len = 6;
        }
        sfn_out[main_len++] = '~';
        sfn_out[main_len] = clash_num + '0';

    } else if (clash_num < 100) {
        if (main_len > 5) {
            main_len = 5;
        }
        sfn_out[main_len++] = '~';
        sfn_out[main_len++] = (clash_num / 10) + '0';
        sfn_out[main_len] = (clash_num % 10) + '0';

    } else {
        if (main_len > 3) {
            main_len = 3;
        }
        const char* set = "0123456789ABCDEF";
        int val = clash_num - 100;
        sfn_out[main_len++] = set[val & 0xF];
        val >>= 4;
        sfn_out[main_len++] = set[val & 0xF];
        val >>= 4;
        sfn_out[main_len++] = set[val & 0xF];
        val >>= 4;
        sfn_out[main_len++] = set[val & 0xF];
        val >>= 4;
        sfn_out[main_len] = set[val & 0xF];
        val >>= 4;
        if (val != 0) {
            return EINVAL;
        }
    }

    return 0;
}