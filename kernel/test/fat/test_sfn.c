/*
 * Tests for GetShortFilename (FAT 8.3 short name generation).
 *
 * Call RunSfnTests(). It is a pure function, so there is no threading or
 * setup. A failing CHECK() halts (assert). The "suspected bug" group at the
 * end uses soft checks that log "SUSPECTED BUG:" and carry on, so one run
 * lists all of them.
 *
 * Contract assumed (from how the driver uses it):
 *   - sfn_out receives the 11 bytes of a FAT directory entry name: 8 name
 *     bytes then 3 extension bytes, uppercase, space padded, no dot.
 *   - clash_num == 0 is the plain conversion (no ~N).
 *     1..9 -> "<up to 6 chars>~N", 10..99 -> "<up to 5 chars>~NN",
 *     100.. -> "<up to 3 chars>" + 5 hex digits of (clash_num - 100), least
 *     significant digit first. Past 0xFFFFF it fails with EINVAL.
 *   - It never checks for existing files, so every clash_num just yields
 *     another name, and different clash_nums should yield different names.
 *   - Returns 0, or EINVAL when no name can be made.
 *
 * OPTIONAL BUILD FLAGS
 *   SFNTEST_NONASCII   run the tests that pass bytes >= 0x80. They call
 *                      isalnum()/toupper() with a negative char (undefined
 *                      behaviour; may crash with a table-based ctype).
 */

#include <common.h>
#include <string.h>
#include <errno.h>
#include <_test.h>

int GetShortFilename(const char* lfn, int clash_num, char* sfn_out);

#define POISON 0x7F
#define SFN_LEN 11

static volatile int g_suspected;
#define SUSPECT(cond, what) do { \
    if (!(cond)) { g_suspected++; LogPrintf("SUSPECTED BUG: %s\n", what); } \
} while (0)

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static char g_buf[16];

static int Run(const char* lfn, int clash) {
    memset(g_buf, POISON, sizeof(g_buf));
    return GetShortFilename(lfn, clash, g_buf);
}

/* Makes the 11-byte expected name from an unpadded name and extension. */
static void MakeExpected(char* out, const char* name, const char* ext) {
    CHECK(strlen(name) <= 8 && strlen(ext) <= 3);
    memset(out, ' ', SFN_LEN);
    memcpy(out, name, strlen(name));
    memcpy(out + 8, ext, strlen(ext));
}

static void Show(char* out, const char* s) {
    for (int i = 0; i < SFN_LEN; ++i) {
        out[i] = (s[i] >= 0x20 && s[i] < 0x7F) ? s[i] : '?';
    }
    out[SFN_LEN] = 0;
}

static void ExpectName(const char* lfn, int clash, const char* name, const char* ext) {
    char want[SFN_LEN + 1];
    MakeExpected(want, name, ext);
    int r = Run(lfn, clash);

    if (r != 0 || memcmp(g_buf, want, SFN_LEN) != 0) {
        char got[SFN_LEN + 1], exp[SFN_LEN + 1];
        Show(got, g_buf);
        Show(exp, want);
        LogPrintf("sfn FAIL: lfn \"%s\" clash %d: returned %d, got [%s], expected [%s]\n",
                  lfn, clash, r, got, exp);
    }
    CHECK(r == 0);
    CHECK(memcmp(g_buf, want, SFN_LEN) == 0);
    for (int i = 12; i < 16; ++i) CHECK(g_buf[i] == POISON);   /* nothing past byte 11 */
}

static void ExpectError(const char* lfn, int clash) {
    int r = Run(lfn, clash);
    if (r != EINVAL) LogPrintf("sfn FAIL: lfn \"%s\" clash %d: returned %d, expected EINVAL\n", lfn, clash, r);
    CHECK(r == EINVAL);
    for (int i = 12; i < 16; ++i) CHECK(g_buf[i] == POISON);
}

/* ------------------------------------------------------------------------ */
/* Plain conversion (clash_num == 0)                                         */
/* ------------------------------------------------------------------------ */

static void TestSimple(void) {
    ExpectName("readme.txt", 0, "README", "TXT");
    ExpectName("a.b", 0, "A", "B");
    ExpectName("abc.de", 0, "ABC", "DE");
    ExpectName("12345.678", 0, "12345", "678");
}

static void TestCaseIsFolded(void) {
    ExpectName("FILE.TXT", 0, "FILE", "TXT");
    ExpectName("FiLe.TxT", 0, "FILE", "TXT");
    ExpectName("file.txt", 0, "FILE", "TXT");
}

static void TestNoExtension(void) {
    ExpectName("makefile", 0, "MAKEFILE", "");
    ExpectName("x", 0, "X", "");
}

static void TestTrailingDot(void) {
    ExpectName("file.", 0, "FILE", "");
}

static void TestFullWidth(void) {
    ExpectName("12345678.abc", 0, "12345678", "ABC");
}

static void TestLongNameIsTruncated(void) {
    ExpectName("LongFileName.text", 0, "LONGFILE", "TEX");
    ExpectName("abcdefghijklmnopqrstuvwxyz", 0, "ABCDEFGH", "");
    ExpectName("123456789.x", 0, "12345678", "X");
}

static void TestLongExtensionIsTruncated(void) {
    ExpectName("index.html", 0, "INDEX", "HTM");
    ExpectName("a.abcdefghij", 0, "A", "ABC");
}

/* The extension comes after the LAST dot. Dots in the name are dropped. */
static void TestMultipleDots(void) {
    ExpectName("archive.tar.gz", 0, "ARCHIVET", "GZ");
    ExpectName("a.b.c", 0, "AB", "C");
    ExpectName("a..b", 0, "A", "B");
}

static void TestSpacesAreDropped(void) {
    ExpectName("my file.txt", 0, "MYFILE", "TXT");
    ExpectName("  a  b  .t x t", 0, "AB", "TXT");
}

/* Characters that are legal in a long name but not in an 8.3 name. */
static void TestInvalidCharactersAreDropped(void) {
    ExpectName("a+b,c.txt", 0, "ABC", "TXT");
    ExpectName("x;y=z[1].dat", 0, "XYZ1", "DAT");
    ExpectName("what?.txt", 0, "WHAT", "TXT");
    ExpectName("a\"b*c|d<e>f.t", 0, "ABCDEF", "T");
    ExpectName("a/b\\c:d.txt", 0, "ABCD", "TXT");
}

static void TestValidSpecialCharactersAreKept(void) {
    ExpectName("!#$%&'()", 0, "!#$%&'()", "");
    ExpectName("a.-@^", 0, "A", "-@^");
    ExpectName("_`{}~.x", 0, "_`{}~", "X");
}

static void TestExtensionSkipsInvalidCharacters(void) {
    ExpectName("f.t+x", 0, "F", "TX");
    ExpectName("f.+++", 0, "F", "");
    ExpectName("f.a b c", 0, "F", "ABC");
}

/*
 * Invalid characters at the front of the extension must be skipped, however
 * many there are. (A loop condition like `i < ext[i]` stops at the first
 * character whose value is <= its index, so a run of tabs or spaces ends the
 * scan early and the extension comes out blank.)
 */
static void TestExtensionAfterLongRunOfInvalidCharacters(void) {
    ExpectName("x.\t\t\t\t\t\t\t\t\t\t\t\tabc", 0, "X", "ABC");
    ExpectName("x.                                          abc", 0, "X", "ABC");
}

static void TestNoUsableName(void) {
    ExpectError("", 0);
    ExpectError(".", 0);
    ExpectError("..", 0);
    ExpectError("...", 0);
}

/* ------------------------------------------------------------------------ */
/* Clash numbers                                                             */
/* ------------------------------------------------------------------------ */

static void TestClashZeroHasNoTilde(void) {
    ExpectName("longfilename.txt", 0, "LONGFILE", "TXT");
    ExpectName("ab.c", 0, "AB", "C");
}

static void TestClashSingleDigit(void) {
    ExpectName("longfilename.txt", 1, "LONGFI~1", "TXT");
    ExpectName("longfilename.txt", 9, "LONGFI~9", "TXT");
    ExpectName("ab.c", 1, "AB~1", "C");
    ExpectName("a", 5, "A~5", "");
}

static void TestClashSingleDigitNameLengths(void) {
    ExpectName("abcdef", 3, "ABCDEF~3", "");        /* 6: exactly fits */
    ExpectName("abcdefg", 3, "ABCDEF~3", "");       /* 7: cut to 6 */
    ExpectName("abcde", 3, "ABCDE~3", "");          /* 5: fits with room */
    ExpectName("abcdefgh", 3, "ABCDEF~3", "");      /* 8: cut to 6 */
}

static void TestClashTwoDigits(void) {
    ExpectName("longfilename.txt", 10, "LONGF~10", "TXT");
    ExpectName("longfilename.txt", 42, "LONGF~42", "TXT");
    ExpectName("longfilename.txt", 99, "LONGF~99", "TXT");
    ExpectName("ab.c", 10, "AB~10", "C");
    ExpectName("a", 99, "A~99", "");
}

static void TestClashTwoDigitNameLengths(void) {
    ExpectName("abcde", 12, "ABCDE~12", "");        /* 5: exactly fits */
    ExpectName("abcdef", 12, "ABCDE~12", "");       /* 6: cut to 5 */
    ExpectName("abcd", 12, "ABCD~12", "");          /* 4: fits with room */
}

/* 100 and up: up to 3 chars, then (clash_num - 100) as 5 hex digits, low digit first. */
static void TestClashHex(void) {
    ExpectName("longfilename.txt", 100, "LON00000", "TXT");
    ExpectName("longfilename.txt", 101, "LON10000", "TXT");
    ExpectName("longfilename.txt", 109, "LON90000", "TXT");
    ExpectName("longfilename.txt", 110, "LONA0000", "TXT");
    ExpectName("longfilename.txt", 115, "LONF0000", "TXT");
    ExpectName("longfilename.txt", 116, "LON01000", "TXT");     /* val 0x10 */
    ExpectName("longfilename.txt", 100 + 0x123, "LON32100", "TXT");
    ExpectName("longfilename.txt", 100 + 0xABCDE, "LONEDCBA", "TXT");
}

static void TestClashHexShortNames(void) {
    ExpectName("ab.c", 101, "AB10000", "C");       /* 2 chars + 5 digits = 7 */
    ExpectName("a", 101, "A10000", "");            /* 1 char + 5 digits = 6 */
    ExpectName("abc", 101, "ABC10000", "");        /* 3 chars + 5 digits = 8 */
}

static void TestClashHexLimits(void) {
    ExpectName("longfilename.txt", 100 + 0xFFFFF, "LONFFFFF", "TXT");
    ExpectError("longfilename.txt", 100 + 0xFFFFF + 1);
    ExpectError("longfilename.txt", 100 + 0x200000);
    ExpectError("ab.c", 100 + 0xFFFFF + 1);
}

static void TestClashNeverChangesExtension(void) {
    int clashes[] = { 0, 1, 9, 10, 99, 100, 101, 5000 };
    for (unsigned i = 0; i < sizeof(clashes) / sizeof(clashes[0]); ++i) {
        CHECK(Run("name.abc", clashes[i]) == 0);
        CHECK(memcmp(g_buf + 8, "ABC", 3) == 0);
        CHECK(Run("name", clashes[i]) == 0);
        CHECK(memcmp(g_buf + 8, "   ", 3) == 0);
    }
}

/* ------------------------------------------------------------------------ */
/* Properties                                                                */
/* ------------------------------------------------------------------------ */

static int IsSfnChar(char c) {
    if (c >= '0' && c <= '9') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    return strchr("!#$%&'()-@^_`{}~", c) != NULL && c != 0;
}

/* A well-formed 8.3 field: valid characters, spaces only as trailing padding. */
static void CheckWellFormed(const char* lfn, int clash, const char* f) {
    int ok = 1;
    for (int part = 0; part < 2; ++part) {
        const char* p = part == 0 ? f : f + 8;
        int len = part == 0 ? 8 : 3;
        int seen_space = 0;
        for (int i = 0; i < len; ++i) {
            if (p[i] == ' ') seen_space = 1;
            else if (seen_space || !IsSfnChar(p[i])) ok = 0;
        }
    }
    if (f[0] == ' ') ok = 0;
    if (!ok) {
        char got[SFN_LEN + 1];
        Show(got, f);
        LogPrintf("sfn FAIL: lfn \"%s\" clash %d gave malformed [%s]\n", lfn, clash, got);
    }
    CHECK(ok);
}

/* Every byte of the 11 is written, whatever was in the buffer before. */
static void TestEveryByteIsWritten(void) {
    const char* names[] = { "a", "ab.c", "longfilename.txt", "x.y" };
    int clashes[] = { 0, 1, 12, 100, 4096 };
    for (unsigned n = 0; n < 4; ++n) {
        for (unsigned c = 0; c < 5; ++c) {
            CHECK(Run(names[n], clashes[c]) == 0);
            for (int i = 0; i < SFN_LEN; ++i) CHECK(g_buf[i] != POISON);
            CheckWellFormed(names[n], clashes[c], g_buf);
        }
    }
}

static void TestDeterministic(void) {
    char first[16];
    for (int clash = 0; clash < 300; clash += 7) {
        CHECK(Run("Some Long File.name", clash) == 0);
        memcpy(first, g_buf, 12);
        CHECK(Run("Some Long File.name", clash) == 0);
        CHECK(memcmp(first, g_buf, SFN_LEN) == 0);
    }
}

/*
 * The whole point of clash_num: successive numbers must give different names,
 * otherwise a caller retrying after EEXIST would loop on the same name.
 */
#define UNIQUE_N 1200
static char g_seen[UNIQUE_N][SFN_LEN];

static void CheckAllDistinct(const char* lfn) {
    for (int c = 0; c < UNIQUE_N; ++c) {
        CHECK(Run(lfn, c) == 0);
        memcpy(g_seen[c], g_buf, SFN_LEN);
        CheckWellFormed(lfn, c, g_buf);
    }
    for (int a = 0; a < UNIQUE_N; ++a) {
        for (int b = a + 1; b < UNIQUE_N; ++b) {
            if (memcmp(g_seen[a], g_seen[b], SFN_LEN) == 0) {
                LogPrintf("sfn FAIL: lfn \"%s\": clash %d and %d both give the same name\n", lfn, a, b);
                CHECK(0);
            }
        }
    }
}

static void TestClashNamesAreDistinct(void) {
    CheckAllDistinct("longfilename.txt");
    CheckAllDistinct("a.b");
    CheckAllDistinct("ab");
    CheckAllDistinct("abcdefgh.ijk");
    CheckAllDistinct("My Document (copy).docx");
}

/* Random names: output is always a well-formed field, with the right ext. */
static uint32_t g_rng;
static uint32_t Rand(void) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng >> 8;
}

static void TestRandomNames(void) {
    static const char alphabet[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        "!#$%&'()-@^_`{}~ +,;=[].....";
    int clashes[] = { 0, 1, 7, 10, 55, 99, 100, 300, 70000 };
    char lfn[24];
    g_rng = 12345;
    for (int iter = 0; iter < 300; ++iter) {
        int len = 1 + Rand() % 20;
        for (int i = 0; i < len; ++i) lfn[i] = alphabet[Rand() % (sizeof(alphabet) - 1)];
        lfn[len] = 0;
        for (unsigned c = 0; c < sizeof(clashes) / sizeof(clashes[0]); ++c) {
            int r = Run(lfn, clashes[c]);
            CHECK(r == 0 || r == EINVAL);
            if (r == 0) CheckWellFormed(lfn, clashes[c], g_buf);
            for (int i = 12; i < 16; ++i) CHECK(g_buf[i] == POISON);
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Suspected bugs (soft checks)                                              */
/* ------------------------------------------------------------------------ */

/* A negative clash number is nonsense and should be refused. */
static void TestNegativeClashNumber(void) {
    int r = Run("file.txt", -1);
    SUSPECT(r == EINVAL, "clash_num -1 is accepted (should return EINVAL)");
    if (r == 0) {
        char got[SFN_LEN + 1];
        Show(got, g_buf);
        LogPrintf("  (-1 produced [%s])\n", got);
    }
    r = Run("file.txt", -100);
    SUSPECT(r == EINVAL, "clash_num -100 is accepted (should return EINVAL)");
}

/*
 * The function writes 12 bytes: strcpy(ext_out, "   ") also stores a NUL at
 * sfn_out[11]. A caller passing a pointer to the 11 byte name field of a
 * directory entry would have its attribute byte zeroed.
 */
static void TestByte11IsUntouched(void) {
    Run("file.txt", 0);
    SUSPECT(g_buf[11] == POISON, "writes a NUL to sfn_out[11], one past the 11 byte name");
}

/* Names that are legal long names but have no valid 8.3 characters. */
static void TestNamesWithNoValidCharacters(void) {
    SUSPECT(Run("+.txt", 0) == 0, "\"+.txt\" gives EINVAL (invalid chars are dropped, not replaced with '_')");
    SUSPECT(Run("[].txt", 0) == 0, "\"[].txt\" gives EINVAL");
    SUSPECT(Run("+++", 0) == 0, "\"+++\" gives EINVAL");
    SUSPECT(Run(".bashrc", 0) == 0, "\".bashrc\" gives EINVAL (leading-dot file; Windows makes BASHRC~1)");
    SUSPECT(Run(".gitignore", 3) == 0, "\".gitignore\" gives EINVAL");
}

/* The first hex-style name can equal the plain name (clash 0). */
static void TestHexNameCanEqualPlainName(void) {
    Run("abc00000.txt", 0);
    char plain[16];
    memcpy(plain, g_buf, SFN_LEN);
    Run("abc00000.txt", 100);
    SUSPECT(memcmp(plain, g_buf, SFN_LEN) != 0,
            "\"abc00000.txt\": clash 100 gives the same name as clash 0");
}

#ifdef SFNTEST_NONASCII
/* Bytes >= 0x80 are not valid in a short name: skip them, keep the rest. */
static void TestNonAsciiBytes(void) {
    ExpectName("caf\xC3\xA9.txt", 0, "CAF", "TXT");
    ExpectName("file.\xC3\xA9x", 0, "FILE", "X");
    ExpectName("\xE2\x82\xAC" "abc", 0, "ABC", "");
}
#endif

void RunSfnTests(void) {
    g_suspected = 0;

    RUN_TEST(TestSimple);
    RUN_TEST(TestCaseIsFolded);
    RUN_TEST(TestNoExtension);
    RUN_TEST(TestTrailingDot);
    RUN_TEST(TestFullWidth);
    RUN_TEST(TestLongNameIsTruncated);
    RUN_TEST(TestLongExtensionIsTruncated);
    RUN_TEST(TestMultipleDots);
    RUN_TEST(TestSpacesAreDropped);
    RUN_TEST(TestInvalidCharactersAreDropped);
    RUN_TEST(TestValidSpecialCharactersAreKept);
    RUN_TEST(TestExtensionSkipsInvalidCharacters);
    RUN_TEST(TestExtensionAfterLongRunOfInvalidCharacters);
    RUN_TEST(TestNoUsableName);

    RUN_TEST(TestClashZeroHasNoTilde);
    RUN_TEST(TestClashSingleDigit);
    RUN_TEST(TestClashSingleDigitNameLengths);
    RUN_TEST(TestClashTwoDigits);
    RUN_TEST(TestClashTwoDigitNameLengths);
    RUN_TEST(TestClashHex);
    RUN_TEST(TestClashHexShortNames);
    RUN_TEST(TestClashHexLimits);
    RUN_TEST(TestClashNeverChangesExtension);

    RUN_TEST(TestEveryByteIsWritten);
    RUN_TEST(TestDeterministic);
    RUN_TEST(TestClashNamesAreDistinct);
    RUN_TEST(TestRandomNames);

    RUN_TEST(TestNegativeClashNumber);
    RUN_TEST(TestByte11IsUntouched);
    RUN_TEST(TestNamesWithNoValidCharacters);
    RUN_TEST(TestHexNameCanEqualPlainName);
#ifdef SFNTEST_NONASCII
    RUN_TEST(TestNonAsciiBytes);
#endif

    if (g_suspected) LogPrintf("sfn tests: %d SUSPECTED BUG(S)\n", g_suspected);
    else             LogPrintf("sfn tests: ALL PASSED\n");
}