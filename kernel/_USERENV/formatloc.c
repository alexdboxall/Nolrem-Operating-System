#include <common.h>
#include "format.h"
#include <errno.h>

/*
 * FormatIntLocale, FormatCurrency and EncodeUtf8.
 *
 * No 64-bit division or modulo anywhere: digits come from FormatInt (which
 * uses UInt64Div10), and currency rounding is done on the decimal digit
 * string. No heap, no libc, no floating point. All stack use is small and
 * fixed (largest buffer is 66 bytes).
 *
 * Paging rules for this file (keep them when editing):
 *   - every function, exported or static, is pageableuserexec;
 *   - every constant is pageableuserrodata, including each string the code
 *     uses (kEmpty, kDot, ...). Do not write a string literal in a
 *     function body: the compiler would put it in plain .rodata;
 *   - no switch statements, which the compiler may turn into a jump table
 *     in plain .rodata.
 */

#define FMT_TRY(expr) do { int try_rc_ = (expr); if (try_rc_) return try_rc_; } while (0)

#define CURRENCY_MAX_FIELD  65536       /* largest width / prec we accept */
#define CURRENCY_MAX_SCALE  36          /* largest locale frac_digits */

/* ------------------------------------------------------------------ */
/* Constants                                                          */
/* ------------------------------------------------------------------ */

static pageableuserrodata const char kEmpty[]  = "";
static pageableuserrodata const char kSpace[]  = " ";
static pageableuserrodata const char kMinus[]  = "-";
static pageableuserrodata const char kPlus[]   = "+";
static pageableuserrodata const char kDot[]    = ".";
static pageableuserrodata const char kOpen[]   = "(";
static pageableuserrodata const char kClose[]  = ")";
static pageableuserrodata const char kSpaces[] = "        ";      /* 8, for CURRENCY_SPACE */

/*
 * A formatted amount is a sequence of tokens: S sign, Y symbol, _ space,
 * N number, ( and ). These are the ISO C localeconv() rules, indexed
 * [sign_posn][cs_precedes][sep_by_space].
 */
static pageableuserrodata const char kLayout[5][2][3][8] = {
    /* 0: parentheses around number and symbol */
    { { "(NY)",  "(N_Y)", "(N_Y)" }, { "(YN)",  "(Y_N)", "(Y_N)" } },
    /* 1: sign before everything */
    { { "SNY",   "SN_Y",  "S_NY"  }, { "SYN",   "SY_N",  "S_YN"  } },
    /* 2: sign after everything */
    { { "NYS",   "N_YS",  "NY_S"  }, { "YNS",   "Y_NS",  "YN_S"  } },
    /* 3: sign immediately before the symbol */
    { { "NSY",   "N_SY",  "NS_Y"  }, { "SYN",   "SY_N",  "S_YN"  } },
    /* 4: sign immediately after the symbol */
    { { "NYS",   "N_YS",  "NY_S"  }, { "YSN",   "YS_N",  "Y_SN"  } },
};

static pageableuserrodata const struct locale kInvariantLocale = {
    .name = "C",
    .mon_decimal_point = ".",
    .negative_sign = "-",
    .frac_digits = 2,
    .int_frac_digits = 2,
    .p_cs_precedes = 1, .p_sign_posn = 1,
    .n_cs_precedes = 1, .n_sign_posn = 1,
    .int_p_cs_precedes = 1, .int_p_sign_posn = 1,
    .int_n_cs_precedes = 1, .int_n_sign_posn = 1,
};

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

/* A string inside a fixed-size locale array. The arrays are not guaranteed
 * to be NUL-terminated, so every use is bounded by the array size. */
typedef struct {
    const char* p;
    size_t n;
} Str;

static pageableuserexec Str MakeStr(const char* p, size_t n) {
    Str s;
    s.p = p;
    s.n = n;
    return s;
}

/* A constant from the section above, without its NUL. */
#define CSTR(arr) MakeStr((arr), sizeof(arr) - 1)

static pageableuserexec Str StrOf(const char* p, size_t cap) {
    Str s;
    s.p = p;
    s.n = 0;
    while (s.n < cap && p[s.n] != 0) {
        s.n++;
    }
    return s;
}

#define LSTR(field) StrOf((field), sizeof(field))

static pageableuserexec bool IsContinuation(char c) {
    return ((unsigned char) c & 0xC0) == 0x80;
}

static pageableuserexec size_t CodePoints(Str s) {
    size_t n = 0;
    for (size_t i = 0; i < s.n; ++i) {
        if (!IsContinuation(s.p[i])) {
            n++;
        }
    }
    return n;
}

/* Output writer. In dry mode it only measures. In live mode it writes into
 * the buffer and refuses to touch the last byte (reserved for the NUL). */
struct Writer {
    char* p;
    size_t max;
    size_t n;           /* bytes so far */
    size_t cps;         /* code points so far */
    bool dry;
};

static pageableuserexec int WPut(struct Writer* w, char c) {
    w->n++;
    if (!IsContinuation(c)) {
        w->cps++;
    }
    if (w->dry) {
        return 0;
    }
    if (w->n >= w->max) {
        return -ERANGE;
    }
    w->p[w->n - 1] = c;
    return 0;
}

static pageableuserexec int WStr(struct Writer* w, Str s) {
    for (size_t i = 0; i < s.n; ++i) {
        FMT_TRY(WPut(w, s.p[i]));
    }
    return 0;
}

static pageableuserexec int WRepeat(struct Writer* w, Str unit, size_t count) {
    while (count--) {
        FMT_TRY(WStr(w, unit));
    }
    return 0;
}

/*
 * Is there a separator in front of the digit that has `p` digits (itself
 * included) to its right? `g` is a POSIX grouping string: each byte is a
 * group size counted from the right, the last byte repeats forever, and 127
 * (or 0xFF) means no further grouping.
 */
static pageableuserexec bool GroupBoundary(Str g, size_t p) {
    size_t acc = 0, i = 0;
    if (g.n == 0) {
        return false;
    }
    for (;;) {
        unsigned char b = (unsigned char) g.p[i];
        if (b == 0 || b == LOCALE_UNSPEC || b == 0xFF) {
            return false;
        }
        acc += b;
        if (acc == p) {
            return true;
        }
        if (acc > p) {
            return false;
        }
        if (i + 1 < g.n) {
            i++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* FormatIntLocale                                                    */
/* ------------------------------------------------------------------ */

export pageableuserexec ssize_t FormatIntLocale(const struct locale* loc, char* out, size_t max, uint64_t v, int base, bool negative, bool group) {
    char digits[66];            /* 64 binary digits + NUL + 1 spare */
    Str sep = CSTR(kEmpty);
    Str grouping = CSTR(kEmpty);
    struct Writer w;
    int rc;

    if (out == NULL || max < 2) {
        return -EINVAL;
    }
    ssize_t nd = FormatInt(digits, sizeof(digits), v, base);
    if (nd < 0) {
        out[0] = 0;
        return nd;
    }

    if (group && base == 10 && loc != NULL) {
        sep = LSTR(loc->thousands_sep);
        grouping = LSTR(loc->grouping);
    }
    if (sep.n == 0 || grouping.n == 0) {
        group = false;
    } else {
        group = true;
    }

    w.p = out;
    w.max = max;
    w.n = 0;
    w.cps = 0;
    w.dry = false;

    rc = 0;
    if (negative && v != 0) {
        rc = WPut(&w, '-');
    }
    for (size_t i = 0; rc == 0 && i < (size_t) nd; ++i) {
        if (group && i > 0 && GroupBoundary(grouping, (size_t) nd - i)) {
            rc = WStr(&w, sep);
        }
        if (rc == 0) {
            rc = WPut(&w, digits[i]);
        }
    }
    if (rc != 0) {
        out[0] = 0;
        return rc;
    }
    out[w.n] = 0;               /* w.n < max is guaranteed by WPut */
    return (ssize_t) w.n;
}

/* ------------------------------------------------------------------ */
/* FormatCurrency                                                     */
/* ------------------------------------------------------------------ */

struct CurCase {
    char layout[8];
    Str sign;
    Str sym;
    Str sp;
};

static pageableuserexec int8_t Pick(int8_t v, int lo, int hi, int8_t fallback) {
    return (v >= lo && v <= hi) ? v : fallback;
}

static pageableuserexec void CurBuild(struct CurCase* c, const struct locale* L, unsigned flags, bool show_plus, bool neg) {
    bool intl = (flags & CURRENCY_INTL) != 0;
    int8_t cs, sep, posn;
    Str sign, sym, sp;

    if (intl) {
        cs   = neg ? L->int_n_cs_precedes  : L->int_p_cs_precedes;
        sep  = neg ? L->int_n_sep_by_space : L->int_p_sep_by_space;
        posn = neg ? L->int_n_sign_posn    : L->int_p_sign_posn;
    } else {
        cs   = neg ? L->n_cs_precedes  : L->p_cs_precedes;
        sep  = neg ? L->n_sep_by_space : L->p_sep_by_space;
        posn = neg ? L->n_sign_posn    : L->p_sign_posn;
    }
    cs = Pick(cs, 0, 1, 1);
    sep = Pick(sep, 0, 2, 0);
    posn = Pick(posn, 0, 4, 1);
    if (neg && (flags & CURRENCY_PARENS)) {
        posn = 0;
    }

    /* sign string. A negative amount always shows something unless it uses
     * parentheses, so an empty negative_sign falls back to "-". */
    if (neg) {
        sign = LSTR(L->negative_sign);
        if (sign.n == 0) {
            sign = CSTR(kMinus);
        }
    } else if (show_plus) {
        sign = LSTR(L->positive_sign);
        if (sign.n == 0) {
            sign = CSTR(kPlus);
        }
    } else if (flags & CURRENCY_SPACE) {
        Str ns = LSTR(L->negative_sign);
        size_t n = (flags & CURRENCY_PARENS) ? 1 : (ns.n ? CodePoints(ns) : 1);
        if (n > 8) {
            n = 8;
        }
        sign = MakeStr(kSpaces + (8 - n), n);
    } else {
        sign = LSTR(L->positive_sign);
    }

    /* the symbol and the separator between symbol and number */
    sp = LSTR(L->currency_space);
    if (sp.n == 0) {
        sp = CSTR(kSpace);
    }
    sym = CSTR(kEmpty);
    if (flags & CURRENCY_NO_SYMBOL) {
        /* nothing */
    } else if (intl) {
        Str ics = LSTR(L->int_curr_symbol);
        char last = ics.n == 4 ? ics.p[3] : 'A';
        bool alnum = (last >= '0' && last <= '9') || (last >= 'A' && last <= 'Z') || (last >= 'a' && last <= 'z');
        if (ics.n == 4 && !alnum) {
            /* "USD ": the code, plus the separator that goes with it */
            sym = MakeStr(ics.p, 3);
            sp = MakeStr(ics.p + 3, 1);
            if (sep == 0) {
                sep = 1;
            }
        } else {
            sym = ics;
        }
    } else {
        sym = LSTR(L->currency_symbol);
    }

    /* no sign text: it collapses to "sign first" with no sign/symbol gap */
    if (sign.n == 0 && posn != 0) {
        posn = 1;
        if (sep == 2) {
            sep = 1;
        }
    }

    const char* t = kLayout[posn][(int) cs][(int) sep];
    size_t k = 0;
    for (; *t; ++t) {
        if (*t == 'S' && sign.n == 0 && posn != 0) {
            continue;
        }
        if ((*t == 'Y' || *t == '_') && sym.n == 0) {
            continue;
        }
        c->layout[k++] = *t;
    }
    c->layout[k] = 0;
    c->sign = sign;
    c->sym = sym;
    c->sp = sp;
}

static pageableuserexec size_t LayoutLen(const struct CurCase* c) {
    size_t n = 0;
    while (c->layout[n]) {
        n++;
    }
    return n;
}

static pageableuserexec size_t NumberIndex(const struct CurCase* c) {
    size_t i = 0;
    while (c->layout[i] && c->layout[i] != 'N') {
        i++;
    }
    return i;
}

#define NO_INDEX ((size_t) -1)

/* Index of the leading sign or opening parenthesis before the number. */
static pageableuserexec size_t LeadIndex(const struct CurCase* c) {
    size_t n = NumberIndex(c);
    for (size_t i = 0; i < n; ++i) {
        if (c->layout[i] == 'S' || c->layout[i] == '(') {
            return i;
        }
    }
    return NO_INDEX;
}

static pageableuserexec Str Token(const struct CurCase* c, char k) {
    if (k == 'S') {
        return c->sign;
    }
    if (k == 'Y') {
        return c->sym;
    }
    if (k == '_') {
        return c->sp;
    }
    if (k == '(') {
        return CSTR(kOpen);
    }
    if (k == ')') {
        return CSTR(kClose);
    }
    return CSTR(kEmpty);
}

static pageableuserexec int EmitTokens(struct Writer* w, const struct CurCase* c, size_t from, size_t to, size_t skip) {
    for (size_t i = from; i < to; ++i) {
        if (i != skip) {
            FMT_TRY(WStr(w, Token(c, c->layout[i])));
        }
    }
    return 0;
}

static pageableuserexec size_t MeasureTokens(const struct CurCase* c, size_t from, size_t to) {
    struct Writer w = { NULL, 0, 0, 0, true };
    EmitTokens(&w, c, from, to, NO_INDEX);
    return w.cps;
}

/* The digits of the amount, already rounded. all[istart..iend) is the integer
 * part, all[iend..fend) the fraction, then fz extra zeros. */
struct CurNum {
    const char* all;
    size_t istart, iend, fend, fz;
    bool group;
    Str sep, grouping, dp;
};

static pageableuserexec int EmitNumber(struct Writer* w, const struct CurNum* n) {
    size_t ni = n->iend - n->istart;
    for (size_t i = 0; i < ni; ++i) {
        if (n->group && i > 0 && GroupBoundary(n->grouping, ni - i)) {
            FMT_TRY(WStr(w, n->sep));
        }
        FMT_TRY(WPut(w, n->all[n->istart + i]));
    }
    if ((n->fend - n->iend) + n->fz > 0) {
        FMT_TRY(WStr(w, n->dp));
        for (size_t i = n->iend; i < n->fend; ++i) {
            FMT_TRY(WPut(w, n->all[i]));
        }
        for (size_t i = 0; i < n->fz; ++i) {
            FMT_TRY(WPut(w, '0'));
        }
    }
    return 0;
}

static pageableuserexec ssize_t CurrencyCore(const struct locale* L, char* out, size_t max, int64_t v, int width, int align, bool show_plus, unsigned flags, int prec, uint32_t fill) {
    char md[24];                /* decimal digits of |v| */
    char all[40];               /* zero-padded digits with a spare leading 0 */
    struct CurNum num;
    struct CurCase cases[2];

    /* ---- scale and displayed digits ---- */
    int scale_i = L->frac_digits;
    if (scale_i < 0 || scale_i == LOCALE_UNSPEC) {
        scale_i = 2;
    }
    if (scale_i > CURRENCY_MAX_SCALE) {
        return -EINVAL;
    }
    size_t s = (size_t) scale_i;
    size_t d;
    if (prec >= 0) {
        d = (size_t) prec;
    } else if ((flags & CURRENCY_INTL) && L->int_frac_digits >= 0 && L->int_frac_digits != LOCALE_UNSPEC) {
        d = (size_t) L->int_frac_digits;
    } else {
        d = s;
    }

    /* ---- digits of the magnitude, rounded on the digit string ---- */
    uint64_t mag = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    ssize_t n = FormatInt(md, sizeof(md), mag, 10);
    if (n < 0) {
        return n;
    }
    size_t T = ((size_t) n > s ? (size_t) n : s) + 1;      /* includes the spare 0 */
    for (size_t i = 0; i < T - (size_t) n; ++i) {
        all[i] = '0';
    }
    for (size_t i = 0; i < (size_t) n; ++i) {
        all[T - (size_t) n + i] = md[i];
    }

    size_t iend = T - s;
    size_t fd, fz;
    if (d < s) {
        size_t cut = iend + d;
        if (all[cut] >= '5') {                  /* half away from zero */
            size_t i = cut;
            while (all[i - 1] == '9') {         /* stops at the spare leading 0 */
                all[i - 1] = '0';
                i--;
            }
            all[i - 1]++;
        }
        fd = d;
        fz = 0;
    } else {
        fd = s;
        fz = d - s;
    }
    size_t fend = iend + fd;

    bool nonzero = false;
    for (size_t i = 0; i < fend; ++i) {
        if (all[i] != '0') {
            nonzero = true;
        }
    }
    bool neg = v < 0 && nonzero;                /* sign is decided after rounding */

    size_t istart = 0;
    while (istart + 1 < iend && all[istart] == '0') {
        istart++;
    }

    num.all = all;
    num.istart = istart;
    num.iend = iend;
    num.fend = fend;
    num.fz = fz;
    num.dp = LSTR(L->mon_decimal_point);
    if (num.dp.n == 0) {
        num.dp = CSTR(kDot);
    }
    num.sep = LSTR(L->mon_thousands_sep);
    num.grouping = LSTR(L->mon_grouping);
    num.group = !(flags & CURRENCY_NO_GROUP) && num.sep.n > 0 && num.grouping.n > 0;

    /* ---- layouts for both signs; reserved widths come from both ---- */
    CurBuild(&cases[0], L, flags, show_plus, false);
    CurBuild(&cases[1], L, flags, show_plus, true);

    size_t preW = 0, postW = 0, leadW = 0;
    for (int k = 0; k < 2; ++k) {
        size_t ni = NumberIndex(&cases[k]);
        size_t pre = MeasureTokens(&cases[k], 0, ni);
        size_t post = MeasureTokens(&cases[k], ni + 1, LayoutLen(&cases[k]));
        size_t lead = LeadIndex(&cases[k]);
        if (pre > preW) {
            preW = pre;
        }
        if (post > postW) {
            postW = post;
        }
        if (lead != NO_INDEX) {
            size_t lw = MeasureTokens(&cases[k], lead, lead + 1);
            if (lw > leadW) {
                leadW = lw;
            }
        }
    }

    /* ---- this value's pieces ---- */
    const struct CurCase* c = &cases[neg ? 1 : 0];
    size_t nidx = NumberIndex(c);
    size_t clen = LayoutLen(c);
    size_t lead = LeadIndex(c);
    size_t preLen = MeasureTokens(c, 0, nidx);
    size_t postLen = MeasureTokens(c, nidx + 1, clen);
    size_t leadLen = lead != NO_INDEX ? MeasureTokens(c, lead, lead + 1) : 0;
    struct Writer dry = { NULL, 0, 0, 0, true };
    EmitNumber(&dry, &num);
    size_t numLen = dry.cps;

    /*
     * ---- alignment ----
     * Written as independent ifs that each adjust one quantity, never as a
     * switch or an else-if chain on `mode`: compilers turn those into a jump
     * table, which would sit in plain .rodata.
     *
     *   body      width of everything that is not padding. The modes differ
     *             only in which parts are measured at their reserved width
     *             (postW, preW, leadW) instead of this value's own.
     *   padR      LEFT and NUMBER pad on the right, the rest on the left.
     *   fill*     extra gaps that line the columns up.
     */
    size_t W = (size_t) width;
    size_t padL = 0, padR = 0, fillPre = 0, fillPost = 0, fillLead = 0;
    int mode = W == 0 ? CURRENCY_ALIGN_LEFT : align;
    size_t body = preLen + numLen + postLen;                    /* LEFT, RIGHT */
    if (mode == CURRENCY_ALIGN_DECIMAL) {
        body = preLen + numLen + postW;
        fillPost = postW - postLen;
    }
    if (mode == CURRENCY_ALIGN_NUMBER) {
        body = preW + numLen + postLen;
        fillPre = preW - preLen;
    }
    if (mode == CURRENCY_ALIGN_SIGN) {
        body = leadW + (preLen - leadLen) + numLen + postW;
        fillLead = leadW - leadLen;
        fillPost = postW - postLen;
    }
    size_t pad = W > body ? W - body : 0;
    bool padOnRight = mode == CURRENCY_ALIGN_LEFT || mode == CURRENCY_ALIGN_NUMBER;
    if (padOnRight) {
        padR = pad;
    } else {
        padL = pad;
    }

    char fillbuf[4];
    Str fillstr;
    fillstr.p = fillbuf;
    fillstr.n = EncodeUtf8(fill == 0 ? ' ' : fill, fillbuf);

    /* ---- emit ---- */
    struct Writer w;
    w.p = out;
    w.max = max;
    w.n = 0;
    w.cps = 0;
    w.dry = false;

    if (mode == CURRENCY_ALIGN_SIGN) {
        if (lead != NO_INDEX) {
            FMT_TRY(EmitTokens(&w, c, lead, lead + 1, NO_INDEX));
        }
        FMT_TRY(WRepeat(&w, fillstr, fillLead));
    }
    FMT_TRY(WRepeat(&w, fillstr, padL));
    FMT_TRY(WRepeat(&w, fillstr, fillPre));
    FMT_TRY(EmitTokens(&w, c, 0, nidx, mode == CURRENCY_ALIGN_SIGN ? lead : NO_INDEX));
    FMT_TRY(EmitNumber(&w, &num));
    FMT_TRY(EmitTokens(&w, c, nidx + 1, clen, NO_INDEX));
    FMT_TRY(WRepeat(&w, fillstr, fillPost));
    FMT_TRY(WRepeat(&w, fillstr, padR));

    out[w.n] = 0;                               /* w.n < max is guaranteed by WPut */
    return (ssize_t) w.n;
}

export pageableuserexec ssize_t FormatCurrency(
    const struct locale* loc, char* out, size_t max, int64_t v,
    int width, int align, bool show_plus,
    unsigned flags, int prec, uint32_t fill)
{
    if (out == NULL || max < 2 || width < 0 || prec < -1) {
        return -EINVAL;
    }
    if (align < CURRENCY_ALIGN_LEFT || align > CURRENCY_ALIGN_SIGN) {
        return -EINVAL;
    }
    if (width > CURRENCY_MAX_FIELD || prec > CURRENCY_MAX_FIELD) {
        return -ERANGE;
    }
    if (loc == NULL) {
        loc = &kInvariantLocale;
    }
    ssize_t r = CurrencyCore(loc, out, max, v, width, align, show_plus, flags, prec, fill);
    if (r < 0) {
        out[0] = 0;
    }
    return r;
}