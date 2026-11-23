/* datefmt (6b-i): the calendar arithmetic, the Moment-style formatter and the parsers behind the Bases date functions and the daily notes. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_datefmt.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static const double T0 = 1748354709123.0;      /* 2025-05-27 14:05:09.123 UTC, a Tuesday, day 147, ISO week 22 */

static void fmt(double ms, int tz, const char *f, const char *want, int line) {
    char out[400];
    size_t n = ex_date_format(out, sizeof out, ms, tz, f);
    if (strcmp(out, want) || n != strlen(want)) { fprintf(stderr, "line %d: format \"%s\" gave \"%s\", wanted \"%s\"\n", line, f, out, want); failures++; }
}
#define FMT(f, want) fmt(T0, 0, f, want, __LINE__)

int main(void) {
    ExDateParts p;
    int64_t y;
    int m, d, has;
    double ms;
    char small[8];
    /* the calendar */
    CHECK(ex_days_from_civil(1970, 1, 1) == 0 && ex_days_from_civil(2000, 3, 1) == 11017 && ex_days_from_civil(1969, 12, 31) == -1);
    { int64_t z; for (z = -800000; z < 800000; z += 997) { ex_civil_from_days(z, &y, &m, &d); CHECK(ex_days_from_civil(y, m, d) == z); } }
    CHECK(ex_days_in_month(2024, 2) == 29 && ex_days_in_month(2023, 2) == 28 && ex_days_in_month(1900, 2) == 28 && ex_days_in_month(2000, 2) == 29 && ex_days_in_month(2025, 4) == 30);
    ex_date_parts(T0, 0, &p);
    CHECK(p.year == 2025 && p.month == 5 && p.day == 27 && p.hour == 14 && p.minute == 5 && p.second == 9 && p.ms == 123 && p.weekday == 2 && p.yday == 147);
    ex_date_parts(T0, 330, &p);                                                      /* India: 19:35 the same day */
    CHECK(p.hour == 19 && p.minute == 35 && p.day == 27);
    ex_date_parts(T0, -15 * 60, &p);                                                 /* 23:05 the day before */
    CHECK(p.hour == 23 && p.minute == 5 && p.day == 26);
    ex_date_parts(-1.0, 0, &p);                                                      /* a moment before the epoch */
    CHECK(p.year == 1969 && p.month == 12 && p.day == 31 && p.hour == 23 && p.minute == 59 && p.second == 59 && p.ms == 999 && p.weekday == 3);
    CHECK(ex_date_make(2025, 5, 27, 14, 5, 9, 123, 0) == T0);
    CHECK(ex_date_make(2025, 14, 1, 0, 0, 0, 0, 0) == ex_date_make(2026, 2, 1, 0, 0, 0, 0, 0));      /* months and days carry over */
    CHECK(ex_date_make(2025, 1, 32, 0, 0, 0, 0, 0) == ex_date_make(2025, 2, 1, 0, 0, 0, 0, 0));
    CHECK(ex_date_make(2025, 0, 1, 0, 0, 0, 0, 0) == ex_date_make(2024, 12, 1, 0, 0, 0, 0, 0));
    /* months added to a date: the day is kept or becomes the last day of a shorter month */
    CHECK(ex_date_add_months(ex_date_make(2025, 1, 31, 10, 0, 0, 0, 0), 0, 1) == ex_date_make(2025, 2, 28, 10, 0, 0, 0, 0));
    CHECK(ex_date_add_months(ex_date_make(2024, 1, 31, 10, 0, 0, 0, 0), 0, 1) == ex_date_make(2024, 2, 29, 10, 0, 0, 0, 0));
    CHECK(ex_date_add_months(ex_date_make(2024, 12, 1, 4, 3, 0, 0, 0), 0, 1) == ex_date_make(2025, 1, 1, 4, 3, 0, 0, 0));
    CHECK(ex_date_add_months(ex_date_make(2025, 3, 31, 0, 0, 0, 0, 0), 0, -1) == ex_date_make(2025, 2, 28, 0, 0, 0, 0, 0));
    CHECK(ex_date_add_months(ex_date_make(2025, 1, 15, 0, 0, 0, 0, 0), 0, -13) == ex_date_make(2023, 12, 15, 0, 0, 0, 0, 0));
    CHECK(ex_date_add_months(ex_date_make(2025, 1, 15, 0, 0, 0, 0, 0), 0, 24) == ex_date_make(2027, 1, 15, 0, 0, 0, 0, 0));
    /* the tokens */
    FMT("YYYY-MM-DD HH:mm:ss", "2025-05-27 14:05:09");
    FMT("YYYY-MM-DD", "2025-05-27");
    FMT("dddd, MMMM Do YYYY, h:mm:ss a", "Tuesday, May 27th 2025, 2:05:09 pm");
    FMT("ddd D MMM YY", "Tue 27 May 25");
    FMT("M/D/YYYY h:mm A", "5/27/2025 2:05 PM");
    FMT("Mo Do do", "5th 27th 2nd");
    FMT("Q Qo", "2 2nd");
    FMT("DDD DDDD", "147 147");
    FMT("W WW Wo GGGG GG", "22 22 22nd 2025 25");
    FMT("w ww wo gggg gg", "22 22 22nd 2025 25");
    FMT("d dd e E", "2 Tu 2 2");
    FMT("H HH h hh k kk", "14 14 2 02 14 14");
    FMT("S SS SSS SSSS", "1 12 123 1230");
    FMT("X x", "1748354709 1748354709123");
    FMT("Z ZZ", "+00:00 +0000");
    FMT("[Today is] dddd", "Today is Tuesday");
    FMT("YYYY[W]WW", "2025W22");
    FMT("\\Y YYYY", "Y 2025");
    FMT("L | LL | LLL", "05/27/2025 | May 27, 2025 | May 27, 2025 2:05 PM");
    FMT("LLLL", "Tuesday, May 27, 2025 2:05 PM");
    FMT("LT LTS l ll", "2:05 PM 2:05:09 PM 5/27/2025 May 27, 2025");
    FMT("", "");
    FMT("- / . : ,", "- / . : ,");
    FMT("[no tokens here] -/.", "no tokens here -/.");
    /* the ordinal rules */
    fmt(ex_date_make(2025, 1, 1, 0, 0, 0, 0, 0), 0, "Do", "1st", __LINE__);
    fmt(ex_date_make(2025, 1, 2, 0, 0, 0, 0, 0), 0, "Do", "2nd", __LINE__);
    fmt(ex_date_make(2025, 1, 3, 0, 0, 0, 0, 0), 0, "Do", "3rd", __LINE__);
    fmt(ex_date_make(2025, 1, 11, 0, 0, 0, 0, 0), 0, "Do", "11th", __LINE__);
    fmt(ex_date_make(2025, 1, 12, 0, 0, 0, 0, 0), 0, "Do", "12th", __LINE__);
    fmt(ex_date_make(2025, 1, 13, 0, 0, 0, 0, 0), 0, "Do", "13th", __LINE__);
    fmt(ex_date_make(2025, 1, 21, 0, 0, 0, 0, 0), 0, "Do", "21st", __LINE__);
    fmt(ex_date_make(2025, 1, 22, 0, 0, 0, 0, 0), 0, "Do", "22nd", __LINE__);
    fmt(ex_date_make(2025, 1, 23, 0, 0, 0, 0, 0), 0, "Do", "23rd", __LINE__);
    /* midnight and noon in the 12-hour clock, the 24-hour clock's k */
    fmt(ex_date_make(2025, 1, 1, 0, 7, 0, 0, 0), 0, "h:mm A | k", "12:07 AM | 24", __LINE__);
    fmt(ex_date_make(2025, 1, 1, 12, 7, 0, 0, 0), 0, "h:mm a", "12:07 pm", __LINE__);
    /* weeks at the turn of the year (checked against Python's isocalendar) */
    fmt(ex_date_make(2024, 12, 30, 0, 0, 0, 0, 0), 0, "GGGG-[W]WW-E", "2025-W01-1", __LINE__);
    fmt(ex_date_make(2021, 1, 3, 0, 0, 0, 0, 0), 0, "GGGG-[W]WW-E", "2020-W53-7", __LINE__);
    fmt(ex_date_make(2020, 12, 31, 0, 0, 0, 0, 0), 0, "GGGG-[W]WW-E", "2020-W53-4", __LINE__);
    fmt(ex_date_make(2016, 1, 1, 0, 0, 0, 0, 0), 0, "GGGG-[W]WW-E", "2015-W53-5", __LINE__);
    fmt(ex_date_make(2026, 1, 1, 0, 0, 0, 0, 0), 0, "GGGG-[W]WW-E", "2026-W01-4", __LINE__);
    fmt(ex_date_make(2024, 12, 29, 0, 0, 0, 0, 0), 0, "gggg [w]ww", "2025 w01", __LINE__);       /* en-US: the week that holds January 1 starts on Sunday */
    fmt(ex_date_make(2025, 12, 28, 0, 0, 0, 0, 0), 0, "gggg [w]ww", "2026 w01", __LINE__);
    /* other time zones, negative years, a short buffer */
    fmt(T0, 330, "YYYY-MM-DD HH:mm Z", "2025-05-27 19:35 +05:30", __LINE__);
    fmt(T0, -300, "HH:mm ZZ", "09:05 -0500", __LINE__);
    fmt(ex_date_make(-44, 3, 15, 0, 0, 0, 0, 0), 0, "YYYY-MM-DD", "-0044-03-15", __LINE__);
    CHECK(ex_date_format(small, sizeof small, T0, 0, "YYYY-MM-DD HH:mm:ss") == 19 && !strcmp(small, "2025-05"));      /* cut, and the length it needs is returned */
    CHECK(ex_date_format(NULL, 0, T0, 0, "YYYY") == 4);
    /* parsing */
    CHECK(ex_date_parse("2025-05-27", 10, 0, &ms, &has) == 0 && ms == ex_date_make(2025, 5, 27, 0, 0, 0, 0, 0) && !has);
    CHECK(ex_date_parse("2025-05-27 14:05:09", 19, 0, &ms, &has) == 0 && ms == ex_date_make(2025, 5, 27, 14, 5, 9, 0, 0) && has);
    CHECK(ex_date_parse("2025-05-27 14:05", 16, 0, &ms, &has) == 0 && ms == ex_date_make(2025, 5, 27, 14, 5, 0, 0, 0) && has);
    CHECK(ex_date_parse("2025-05-27T14:05:09.123Z", 24, 330, &ms, &has) == 0 && ms == T0);                  /* Z: the zone given is not used */
    CHECK(ex_date_parse("2025-05-27T19:35:09.123+05:30", 29, 0, &ms, &has) == 0 && ms == T0);
    CHECK(ex_date_parse("2025-05-27 14:05:09.1", 21, 0, &ms, &has) == 0 && ms == ex_date_make(2025, 5, 27, 14, 5, 9, 100, 0));
    CHECK(ex_date_parse("2025-05-27 19:35:09", 19, 330, &ms, &has) == 0 && ms == ex_date_make(2025, 5, 27, 14, 5, 9, 0, 0));    /* no zone in the text: the given one */
    CHECK(ex_date_parse(" 2025-05-27 ", 12, 0, &ms, &has) == 0);
    CHECK(ex_date_parse("2025-02-30", 10, 0, &ms, &has) == -1 && ex_date_parse("2025-13-01", 10, 0, &ms, &has) == -1 && ex_date_parse("2025-5-27", 9, 0, &ms, &has) == -1);
    CHECK(ex_date_parse("2025-05-27 25:00", 16, 0, &ms, &has) == -1 && ex_date_parse("2025-05-27x", 11, 0, &ms, &has) == -1 && ex_date_parse("", 0, 0, &ms, &has) == -1);
    CHECK(ex_date_parse("2025-05-27 14:05:09 junk", 24, 0, &ms, &has) == -1 && ex_date_parse("not a date", 10, 0, &ms, &has) == -1);
    CHECK(ex_date_parse("2024-02-29", 10, 0, &ms, &has) == 0 && ex_date_parse("2023-02-29", 10, 0, &ms, &has) == -1);
    /* formatted text back to a date */
    CHECK(ex_date_parse_format("2025-05-27", 10, "YYYY-MM-DD", 0, &ms) == 0 && ms == ex_date_make(2025, 5, 27, 0, 0, 0, 0, 0));
    CHECK(ex_date_parse_format("Tuesday, May 27th 2025", 22, "dddd, MMMM Do YYYY", 0, &ms) == -1);     /* Do is not read back */
    CHECK(ex_date_parse_format("Tuesday, May 27 2025", 20, "dddd, MMMM D YYYY", 0, &ms) == 0 && ms == ex_date_make(2025, 5, 27, 0, 0, 0, 0, 0));
    CHECK(ex_date_parse_format("27 Mar 25 2:05 PM", 17, "D MMM YY h:mm A", 0, &ms) == 0 && ms == ex_date_make(2025, 3, 27, 14, 5, 0, 0, 0));
    CHECK(ex_date_parse_format("12:00 AM", 8, "hh:mm A", 0, &ms) == 0 && ex_date_parse_format("12:30 PM", 8, "hh:mm A", 0, &ms) == 0);
    CHECK(ex_date_parse_format("2025-05-27 note", 15, "YYYY-MM-DD [note]", 0, &ms) == 0 && ex_date_parse_format("2025-05-27 nope", 15, "YYYY-MM-DD [note]", 0, &ms) == -1);
    CHECK(ex_date_parse_format("2025-147", 8, "YYYY-DDDD", 0, &ms) == 0 && ms == ex_date_make(2025, 5, 27, 0, 0, 0, 0, 0));
    CHECK(ex_date_parse_format("2025-5-7", 8, "YYYY-M-D", 0, &ms) == 0 && ms == ex_date_make(2025, 5, 7, 0, 0, 0, 0, 0));
    CHECK(ex_date_parse_format("2025-02-30", 10, "YYYY-MM-DD", 0, &ms) == -1 && ex_date_parse_format("2025-05-27x", 11, "YYYY-MM-DD", 0, &ms) == -1 && ex_date_parse_format("2025-05", 7, "YYYY-MM-DD", 0, &ms) == -1);
    CHECK(ex_date_parse_format("20250527", 8, "YYYYMMDD", 0, &ms) == 0 && ms == ex_date_make(2025, 5, 27, 0, 0, 0, 0, 0));
    /* a format and its own result agree */
    {
        double t;
        int ok = 1;
        for (t = ex_date_make(1999, 12, 31, 23, 59, 59, 0, 0); t < ex_date_make(2030, 1, 1, 0, 0, 0, 0, 0); t += 86400000.0 * 37.0 + 1234567.0) {
            char buf[80];
            double back;
            ex_date_format(buf, sizeof buf, t, 0, "YYYY-MM-DD HH:mm:ss");
            if (ex_date_parse_format(buf, strlen(buf), "YYYY-MM-DD HH:mm:ss", 0, &back) != 0 || back != floor(t / 1000.0) * 1000.0) ok = 0;
        }
        CHECK(ok);
    }
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("ok\n");
    return 0;
}
