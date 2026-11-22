#include <machine/x86.h>
#include <errno.h>
#include <timeconv.h>
#include <common.h>
#include <log.h>

#define RTC_SECOND  0x00
#define RTC_MINUTE  0x02
#define RTC_HOUR    0x04
#define RTC_DAY     0x07
#define RTC_MONTH   0x08
#define RTC_YEAR    0x09
#define RTC_WEEKDAY 0x06

#define CURRENT_YEAR 2026

static bool x86IsUpdateInProgress(void) {
    return x86ReadCmos(0x0A) & 0x80;
}

static uint8_t x86BcdToBinary(uint8_t val) {
    return (val & 0xF) + (val >> 4) * 10;
}

static uint8_t x86BinaryToBcd(uint8_t val) {
    return (val % 10) + ((val / 10) << 4);
}

static int x86Convert12HourTo24(int hour, bool pm) {
    if (hour == 12) {
        hour = 0;
    }
    if (pm) {
        hour += 12;
    }
    return hour;
}

static int x86Convert24HourTo12(int hour, bool* pm) {
    *pm = hour >= 12;
    hour %= 12;
    if (hour == 0) {
        hour = 12;
    }
    return hour;
}

/*
 * ACPI.SYS will set this with the correct value if one exists.
 */
export int x86_rtc_century_register = -1;
static int x86GetCenturyRegister(void) {
    return x86_rtc_century_register;
}

static int x86GetCenturyGuess(int low_year) {
    int year = low_year + (CURRENT_YEAR / 100) * 100;
    if (year < CURRENT_YEAR) year += 100;
    return year / 100;
}

static int x86GetCentury(int low_year, bool rtc_in_bcd_mode) {
    int guess = x86GetCenturyGuess(low_year);

    int century_reg = x86GetCenturyRegister();
    if (century_reg == -1) {
        return guess;        

    } else {
        uint8_t century = x86ReadCmos(century_reg);
        if (rtc_in_bcd_mode) {
            century = x86BcdToBinary(century);
        }
        return century;
    }
}

static void x86SetCentury(int century, bool rtc_in_bcd_mode) {
    int century_reg = x86GetCenturyRegister();
    if (century_reg != -1) {
        if (rtc_in_bcd_mode) {
            century = x86BinaryToBcd(century);
        }
        x86WriteCmos(century_reg, century);
    }
}

static bool x86AreTimesEqual(struct ostime a, struct ostime b) {
    return a.sec == b.sec && a.min == b.min && a.hour == b.hour &&
           a.day == b.day && a.month == b.month && a.year == b.year;
}

static void x86ReadTimeState(struct ostime* t)
{
    t->microsec = 0;
    t->sec = x86ReadCmos(RTC_SECOND);
    t->min = x86ReadCmos(RTC_MINUTE);
    t->hour = x86ReadCmos(RTC_HOUR);
    t->day = x86ReadCmos(RTC_DAY);
    t->month = x86ReadCmos(RTC_MONTH);
    t->year = x86ReadCmos(RTC_YEAR);

    uint8_t reg_b = x86ReadCmos(0x0B);
    bool bcd = !(reg_b & 0x04);
    bool twelve_hour = !(reg_b & 0x02);
    if (bcd) {
        t->sec = x86BcdToBinary(t->sec);
        t->min = x86BcdToBinary(t->min);
        t->hour = x86BcdToBinary(t->hour & 0x7F);
        t->day = x86BcdToBinary(t->day);
        t->month = x86BcdToBinary(t->month);
        t->year = x86BcdToBinary(t->year);
    }
    if (twelve_hour) {
        t->hour = x86Convert12HourTo24(t->hour, t->hour & 0x80);
    }

    t->year += x86GetCentury(t->year, bcd) * 100;
}

static void x86WriteTimeState(struct ostime t)
{
    uint8_t reg_b = x86ReadCmos(0x0B);
    bool bcd = !(reg_b & 0x04);
    bool twelve_hour = !(reg_b & 0x02);
    bool pm = false;
    if (twelve_hour) {
        t.hour = x86Convert24HourTo12(t.hour, &pm);
    }
    x86SetCentury(t.year / 100, bcd);
    t.year %= 100;
    if (bcd) {
        t.sec = x86BinaryToBcd(t.sec);
        t.min = x86BinaryToBcd(t.min);
        t.hour = x86BinaryToBcd(t.hour);
        t.day = x86BinaryToBcd(t.day);
        t.month = x86BinaryToBcd(t.month);
        t.year = x86BinaryToBcd(t.year);
    }
    if (pm) {
        t.hour |= 0x80;
    }
    
    x86WriteCmos(RTC_SECOND, t.sec);
    x86WriteCmos(RTC_MINUTE, t.min);
    x86WriteCmos(RTC_HOUR, t.hour);
    x86WriteCmos(RTC_DAY, t.day);
    x86WriteCmos(RTC_MONTH, t.month);
    x86WriteCmos(RTC_YEAR, t.year);
}

export uint64_t ArchGetUtcTime(int64_t timezone_offset) {    
    struct ostime time;
    struct ostime prev;

    while (x86IsUpdateInProgress()) {
        ;
    }
    x86ReadTimeState(&time);
    
    do {
        prev = time;
        while (x86IsUpdateInProgress()) {
            ;
        }
        x86ReadTimeState(&time);

    } while (!x86AreTimesEqual(time, prev));

    return TimeStructToValue(time) - timezone_offset;
}

export int ArchSetUtcTime(uint64_t time, int64_t timezone_offset) {
    time += timezone_offset;
    
    struct ostime rtime = TimeValueToStruct(time);
    struct ostime readback;

    do {
        x86WriteTimeState(rtime);
        x86ReadTimeState(&readback);
    } while (!x86AreTimesEqual(rtime, readback));

    x86WriteCmos(RTC_WEEKDAY, GetWeekday(time));

    return 0;
}