#pragma once

#include <common.h>


#define LOCALE_UNSPEC   127

/* remember, all this is UTF_8! */
struct locale {
    char name[32];               /* "en-AU", informational only */
 
    /* ---- numeric (%'d) ---------------------------------------------- */
    char thousands_sep[8];       /* e.g. "," */
    char grouping[28];           /* bytes, NUL-terminated: "\3" = 3,3,3..;
                                       "\3\2" = 3 then 2,2,..; 127 = stop */
 
    /* ---- monetary (%$) ---------------------------------------------- */
    char currency_symbol[16];    /* "$" */
    char int_curr_symbol[16];    /* "AUD " = ISO code + separator char */
    char currency_space[16];    /* "AUD " = ISO code + separator char */
    char mon_decimal_point[8];  /* "." */
    char mon_thousands_sep[8];  /* "," */
    char mon_grouping[32];       /* as grouping */
    char positive_sign[16];      /* usually "" */
    char negative_sign[16];      /* "-" */
    int8_t frac_digits;             /* scale of the int64_t argument */
    int8_t int_frac_digits;
    int8_t p_cs_precedes, p_sep_by_space, p_sign_posn;      /* non-negative */
    int8_t n_cs_precedes, n_sep_by_space, n_sign_posn;      /* negative */
    int8_t int_p_cs_precedes, int_p_sep_by_space, int_p_sign_posn;
    int8_t int_n_cs_precedes, int_n_sep_by_space, int_n_sign_posn;
 
    char abday[7][32];          /* Sunday first */
    char day[7][64];
    char abmon[12][32];         /* January first */
    char mon[12][64];
    char am_pm[2][32];           /* may be "" */
    char d_t_fmt[64];            /* %c */
    char d_fmt[64];              /* %x */
    char t_fmt[64];              /* %X */
    char t_fmt_ampm[64];         /* %r */
};


int FormatStringGeneric(
    const struct locale *locale,
    int (*outputter)(void *ctxt, size_t index, char c),
    void *output_ctxt,
    size_t max,
    size_t *chars_out,
    const char *format_str,
    va_list ap
);

uint64_t UInt64Div10(uint64_t n, int* rem);

ssize_t FormatInt(char* out, size_t max, uint64_t v, int base);
ssize_t FormatIntLocale(const struct locale* loc, char* out, size_t max, uint64_t v, int base, bool negative, bool group);
 
#define CURRENCY_ALIGN_LEFT     0
#define CURRENCY_ALIGN_RIGHT    1
#define CURRENCY_ALIGN_DECIMAL  2
#define CURRENCY_ALIGN_NUMBER   3
#define CURRENCY_ALIGN_SIGN     4

#define CURRENCY_SPACE      (1u << 0)   /* printf ' ': reserve the sign's width on non-negative amounts */
#define CURRENCY_PARENS     (1u << 1)   /* printf '(': accounting style, negatives in parentheses */
#define CURRENCY_NO_SYMBOL  (1u << 2)   /* printf '!': digits only, no currency symbol */
#define CURRENCY_INTL       (1u << 3)   /* printf '#': ISO code and the locale's int_* layout */
#define CURRENCY_NO_GROUP   (1u << 4)   /* printf '^': no digit grouping */
 
ssize_t FormatCurrency(
    const struct locale* loc, char* out, size_t max, int64_t v,
    int width, int align, bool show_plus,
    unsigned flags, int prec, uint32_t fill
);

size_t EncodeUtf8(uint32_t cp, char out[4]);
