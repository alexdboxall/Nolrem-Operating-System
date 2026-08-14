#include <timeconv.h>
#include <common.h>

/*
 * Note that these time conversions are not particually quick.
 */

static const int userrodata cumulative_days_in_months[13] = {
	0,															// 1 Jan
	31,															// 1 Feb
	31 + 28,													// 1 March
	31 + 28 + 31,												// 1 April
	31 + 28 + 31 + 30,											// 1 May
	31 + 28 + 31 + 30 + 31,										// 1 June
	31 + 28 + 31 + 30 + 31 + 30,								// 1 July
	31 + 28 + 31 + 30 + 31 + 30 + 31,							// 1 August
	31 + 28 + 31 + 30 + 31 + 30 + 31 + 31,						// 1 September
	31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30,					// 1 October
	31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31,			// 1 November
	31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31 + 30,		// 1 December
	31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31 + 30 + 31,	// Next year
};

static userexec bool IsLeapYear(int year) {
	return (year % 4 == 0) && ((year % 100) != 0 || (year % 400) == 0);
}

static userexec int GetFullLeapYearsSince1601(int year) {
	int leaps = 0;
	int i = 1604;

	while (i < year) {
		if (IsLeapYear(i)) {
			++leaps;
		}
		i += 4;
	}
	return leaps;
}

export userexec uint64_t TimeStructToValue(struct ostime t) {
	if (t.year < 1601 || t.year > 2400) {
		return 0;
	}
	if (t.month < 1 || t.month > 12) {
		return 0;
	}
	if (t.day < 1 || t.day > 31) {
		return 0;
	}

	uint64_t total_secs = t.sec;
	total_secs += t.min * 60;
	total_secs += t.hour * 3600;

	uint64_t days = t.day - 1;
	days += cumulative_days_in_months[t.month - 1];
	if (IsLeapYear(t.year) && t.month >= 3) {
		++days;
	}

	days += 365 * (t.year - 1601);
	days += GetFullLeapYearsSince1601(t.year);
	total_secs += days * SECS_PER_DAY;
	return total_secs * 1000000 + t.microsec;
}

/* Prevent this function from generating `__udivmoddi4` - which is big and slow.
 * Because it would be the only user of that __udivmoddi4, it actually reduces
 * the executable size by optimising less (i.e. not moving it all up into
 * division) as it means we can prevent that func being added altogether.
 */
__attribute__((optimize("O1"))) export userexec struct ostime TimeValueToStruct(uint64_t t) {
	// 1000000 * SECS_PER_DAY = 86,400,000,000 = 21,093,750 * 4096
	// so t to `days left` = t / 4096 / 21093750
	uint32_t days_left = (t >> 12) / 21093750U;
	uint64_t microsec_in_day = t - days_left * (1000000U * SECS_PER_DAY);
	uint32_t sec_in_day = microsec_in_day / 1000000U;
	struct ostime res;
	res.microsec = microsec_in_day % 1000000U;
	res.sec = sec_in_day % 60U;
	res.min = (sec_in_day / 60U) % 60U;
	res.hour = (sec_in_day / 3600U) % 24U;
	res.year = 1601;

	while (true) {
		if (res.year > 2400) {
			break;
		}
		res.month = 1;
		for (int i = 1; i <= 12; ++i) {
			int days = cumulative_days_in_months[i];
			int leap = i >= 2 && IsLeapYear(res.year);
			if ((int) days_left >= days + leap) {
				res.month++;
			} else {
				int prior = cumulative_days_in_months[i - 1] + (i - 1 >= 2 && IsLeapYear(res.year));
				days_left -= prior;
				break;
			}
		}
		if (res.month == 13) {
			days_left -= IsLeapYear(res.year) ? 366 : 365; 
			res.year++;
		} else {
			break;
		}
	}

	res.day = days_left + 1;
	return res;
}

/**
 * Sunday = 1, Monday = 2, ..., Saturday = 6.
 */
export userexec int GetWeekday(uint64_t t) {
	int days_since_1601 = (t / 1000000) / SECS_PER_DAY;

	/*
	 * 1 Janurary 1601 was a Monday.
	 */
	return (1 + days_since_1601) % 7 + 1;
}

#define SECS_BETWEEN_1970_AND_1601 11644473600ULL

export userexec uint64_t TimeValueToUnixTime(uint64_t t) {
	return (t / 1000000U) - SECS_BETWEEN_1970_AND_1601;
}

export userexec uint64_t UnixTimeToTimeValue(uint64_t t) {
	return (t + SECS_BETWEEN_1970_AND_1601) * 1000000U;
}
