/* Dates (sub-slice 6b-i): the calendar arithmetic and the Moment-style formatter that the Bases expressions and (6e-i) the daily notes use. A date is a
 * number of milliseconds since 1970-01-01 00:00 UTC (a double, so it can be fractional and far outside any real date); the time zone is a number of
 * minutes east of UTC given with every call, so a test can use 0 and the program can use the system's. English names only (the program has one language). */
#ifndef EXHAUST_DATEFMT_H
#define EXHAUST_DATEFMT_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExDateParts {
    int year, month, day;       /* month 1 to 12, day 1 to 31 */
    int hour, minute, second, ms;
    int weekday;                /* 0 Sunday .. 6 Saturday */
    int yday;                   /* 1 to 366 */
} ExDateParts;

int64_t ex_days_from_civil(int64_t y, int m, int d);                  /* days since 1970-01-01 of a calendar day (proleptic Gregorian) */
void ex_civil_from_days(int64_t days, int64_t *y, int *m, int *d);
int ex_days_in_month(int64_t y, int m);

void ex_date_parts(double ms, int tz_min, ExDateParts *p);
/* The date of calendar parts (any month, day, hour... out of range carries over, as Moment does for set values). */
double ex_date_make(int64_t y, int mo, int d, int h, int mi, int s, int ms, int tz_min);
/* Adds calendar months and years: the day of the month is kept or, if the month is shorter, becomes its last day (Jan 31 + 1 month is Feb 28 or 29). */
double ex_date_add_months(double ms, int tz_min, int64_t months);
/* Moment.js format tokens: YYYY YY Y, M MM MMM MMMM Mo, D DD Do DDD DDDD, d dd ddd dddd do e E, w ww wo W WW Wo (en-US weeks start on Sunday, ISO weeks on Monday),
 * gggg gg GGGG GG, Q Qo, H HH h hh k kk, m mm, s ss, S SS SSS (up to nine digits: the rest are zeros), a A, Z ZZ, z zz (the offset as +hh:mm style names), X x,
 * the localized L LL LLL LLLL l ll lll llll LT LTS, and text in [square brackets] as it is; `\\` also escapes the next character. Writes at most cap - 1 bytes
 * and a NUL, and returns the length the whole result needs (like snprintf). */
size_t ex_date_format(char *out, size_t cap, double ms, int tz_min, const char *fmt);
/* Reads `YYYY-MM-DD`, `YYYY-MM-DD HH:mm`, `YYYY-MM-DD HH:mm:ss`, with `.SSS` after the seconds, `T` in place of the space and `Z` or `+hh:mm` at the end (without one the time zone
 * is tz_min). Returns 0 and sets *ms and *has_time (1 when there was a time), or -1. Impossible dates (February 30) are refused. */
int ex_date_parse(const char *s, size_t n, int tz_min, double *ms, int *has_time);
/* Reads a text written with Moment tokens (the same list, numbers and names, not the localized ones) back into a date: for daily notes. Returns 0 or -1. */
int ex_date_parse_format(const char *s, size_t n, const char *fmt, int tz_min, double *ms);

#ifdef __cplusplus
}
#endif
#endif
