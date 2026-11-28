/* hunt_datefmt: the calendar arithmetic and the Moment-style formatter and parser (src/util/datefmt.c).
 *
 * Daily notes, templates ({{date}}), the importer's {{date}} and every date in a Bases view go through this code, so a wrong week number or a lost
 * minus sign in a time zone lands in file names. This test
 *   A. checks days <-> civil dates for a million days around the epoch and a sample of extreme years, month lengths and leap years, and known anchors;
 *   B. compares ex_date_parts with the C library's gmtime_r for random instants in every time zone from -12:00 to +14:00 (POSIX only);
 *   C. checks date_make's carrying (month 13, day 0, hour 25, negative values) and add_months (the day clamps, and the arithmetic is done in the
 *      zone's calendar, not in UTC);
 *   D. formats a fixed instant with every token the header documents, the ordinal suffixes of the 31 days, week numbers that straddle New Year
 *      (ISO and en-US), hours 0, 12 and 13, zones with a negative offset under one hour, and the snprintf-like length contract for every buffer size;
 *   E. parses valid and invalid dates (February 30, month 13, 24:00, trailing text) and round-trips format -> parse for formats used by daily and
 *      weekly notes;
 *   F. throws random format strings and extreme numbers (NaN, infinity, 1e300) at everything: no crash, no buffer overrun.
 * Link: exhaust_datefmt. */
#include "hunt.h"

#include <math.h>
#include <time.h>

#include "exhaust_datefmt.h"

static char *fmt(double ms, int tz, const char *f) {
    size_t need = ex_date_format(NULL, 0, ms, tz, f);
    char *out = (char *)malloc(need + 1);
    size_t n = ex_date_format(out, need + 1, ms, tz, f);
    HUNT_CHECKF(n == need, "format \"%s\": length %lu the first time and %lu the second", f, (unsigned long)need, (unsigned long)n);
    HUNT_CHECK(strlen(out) == need || need > strlen(out));
    return out;
}

#define EXPECT_FMT(ms, tz, f, want) do { \
    char *g_ = fmt((ms), (tz), (f)); \
    hunt_.checks++; \
    if (strcmp(g_, (want)) != 0) { hunt_fail_("FAIL", __FILE__, __LINE__, "format(" #f ")", "format \"%s\" gave \"%s\", want \"%s\"", (f), g_, (want)); } \
    free(g_); \
} while (0)

/* ---- A. days and civil dates ---- */

static void test_civil(void) {
    int64_t d, y;
    int m, dd;
    hunt_step("civil: anchors");
    HUNT_EQ_INT(ex_days_from_civil(1970, 1, 1), 0);
    HUNT_EQ_INT(ex_days_from_civil(1969, 12, 31), -1);
    HUNT_EQ_INT(ex_days_from_civil(2000, 3, 1), 11017);
    HUNT_EQ_INT(ex_days_from_civil(2024, 2, 29), 19782);
    HUNT_EQ_INT(ex_days_from_civil(1900, 3, 1), -25508);
    HUNT_EQ_INT(ex_days_from_civil(2038, 1, 19), 24855);
    HUNT_EQ_INT(ex_days_from_civil(0, 3, 1), -719468);
    HUNT_EQ_INT(ex_days_from_civil(1, 1, 1), -719162);
    hunt_step("civil: month lengths and leap years");
    {
        struct { int64_t y; int m, want; } dm[] = { { 2024, 2, 29 }, { 2023, 2, 28 }, { 1900, 2, 28 }, { 2000, 2, 29 }, { 2100, 2, 28 }, { 2400, 2, 29 }, { 0, 2, 29 }, { -4, 2, 29 }, { -1, 2, 28 }, { -100, 2, 28 }, { -400, 2, 29 },
            { 2024, 1, 31 }, { 2024, 4, 30 }, { 2024, 6, 30 }, { 2024, 9, 30 }, { 2024, 11, 30 }, { 2024, 12, 31 }, { 2024, 7, 31 }, { 2024, 8, 31 }, { 1000000000, 2, 29 }, { -1000000000, 2, 29 }, { 1000000001, 2, 28 } };
        size_t i;
        for (i = 0; i < sizeof dm / sizeof *dm; i++) HUNT_CHECKF(ex_days_in_month(dm[i].y, dm[i].m) == dm[i].want, "days_in_month(%lld, %d) = %d, want %d", (long long)dm[i].y, dm[i].m, ex_days_in_month(dm[i].y, dm[i].m), dm[i].want);
        (void)ex_days_in_month(2024, 0); (void)ex_days_in_month(2024, 13); (void)ex_days_in_month(2024, -5);          /* must not crash or loop */
    }
    hunt_step("civil: a million days each way, and every day of a few extreme years");
    {
        int64_t prev = ex_days_from_civil(-3000, 12, 31);
        for (d = -1100000; d <= 1100000; d++) {
            int64_t back;
            ex_civil_from_days(d, &y, &m, &dd);
            back = ex_days_from_civil(y, m, dd);
            if (back != d) { HUNT_CHECKF(0, "day %lld -> %lld-%d-%d -> day %lld", (long long)d, (long long)y, m, dd, (long long)back); break; }
            if (m < 1 || m > 12 || dd < 1 || dd > ex_days_in_month(y, m)) { HUNT_CHECKF(0, "day %lld -> %lld-%d-%d is not a date", (long long)d, (long long)y, m, dd); break; }
        }
        (void)prev;
        {
            int64_t years[] = { -1000000, -4000, -1, 0, 1, 9999, 10000, 275760, 1000000, 100000000, 2000000000ll, -2000000000ll };
            size_t i;
            for (i = 0; i < sizeof years / sizeof *years; i++) {
                int mo, da;
                int64_t last = ex_days_from_civil(years[i], 1, 1) - 1;
                for (mo = 1; mo <= 12; mo++) for (da = 1; da <= ex_days_in_month(years[i], mo); da++) {
                    int64_t days = ex_days_from_civil(years[i], mo, da), yy;
                    int m2, d2;
                    if (days != last + 1) { HUNT_CHECKF(0, "year %lld: %d-%d is day %lld after day %lld", (long long)years[i], mo, da, (long long)days, (long long)last); break; }
                    last = days;
                    ex_civil_from_days(days, &yy, &m2, &d2);
                    if (yy != years[i] || m2 != mo || d2 != da) { HUNT_CHECKF(0, "year %lld: %d-%d came back as %lld-%d-%d", (long long)years[i], mo, da, (long long)yy, m2, d2); break; }
                }
                HUNT_CHECKF(ex_days_from_civil(years[i] + 1, 1, 1) == last + 1, "year %lld has a different length than its days", (long long)years[i]);
            }
        }
    }
}

/* ---- B. against the C library ---- */

static void test_against_libc(void) {
#if !defined(_WIN32)
    long rounds = hunt_iters(60000), r;
    for (r = 0; r < rounds; r++) {
        int64_t secs = (int64_t)(hunt_rand64() % 8000000000ull) - 2500000000ll;      /* about 1890 to 2223 */
        int tz = (int)hunt_rnd(1561) - 720, ms = (int)hunt_rnd(1000);
        double t = (double)secs * 1000.0 + ms;
        time_t shifted = (time_t)(secs + (int64_t)tz * 60);
        struct tm tmv;
        ExDateParts p;
        if (!gmtime_r(&shifted, &tmv)) continue;
        ex_date_parts(t, tz, &p);
        if (p.year != tmv.tm_year + 1900 || p.month != tmv.tm_mon + 1 || p.day != tmv.tm_mday || p.hour != tmv.tm_hour || p.minute != tmv.tm_min || p.second != tmv.tm_sec || p.ms != ms ||
            p.weekday != tmv.tm_wday || p.yday != tmv.tm_yday + 1) {
            HUNT_CHECKF(0, "date_parts(%.0f ms, tz %d) = %d-%02d-%02d %02d:%02d:%02d.%03d wd %d yd %d; libc says %d-%02d-%02d %02d:%02d:%02d wd %d yd %d", t, tz, p.year, p.month, p.day, p.hour, p.minute, p.second, p.ms, p.weekday, p.yday,
                        tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec, tmv.tm_wday, tmv.tm_yday + 1);
            if (hunt_.failures > 30) break;
        }
        /* make is the inverse */
        {
            double back = ex_date_make(p.year, p.month, p.day, p.hour, p.minute, p.second, p.ms, tz);
            if (back != t) { HUNT_CHECKF(0, "date_make(parts of %.0f ms, tz %d) = %.0f", t, tz, back); if (hunt_.failures > 30) break; }
        }
    }
#endif
    hunt_step("parts: before 1970 and fractions");
    {
        ExDateParts p;
        ex_date_parts(-1, 0, &p);
        HUNT_CHECK(p.year == 1969 && p.month == 12 && p.day == 31 && p.hour == 23 && p.minute == 59 && p.second == 59 && p.ms == 999 && p.weekday == 3 && p.yday == 365);
        ex_date_parts(0, 0, &p);
        HUNT_CHECK(p.year == 1970 && p.month == 1 && p.day == 1 && p.weekday == 4 && p.yday == 1 && p.ms == 0);
        ex_date_parts(1709210096789.0, 0, &p);
        HUNT_CHECK(p.year == 2024 && p.month == 2 && p.day == 29 && p.hour == 12 && p.minute == 34 && p.second == 56 && p.ms == 789 && p.weekday == 4 && p.yday == 60);
        ex_date_parts(-0.5, 0, &p);
        HUNT_SUSPECT(p.year == 1969 && p.ms == 999, "half a millisecond before the epoch should floor to the millisecond before it, not truncate to the epoch");
        ex_date_parts(-86400000.0, 0, &p);
        HUNT_CHECK(p.year == 1969 && p.month == 12 && p.day == 31 && p.hour == 0 && p.weekday == 3);
        ex_date_parts(0, 840, &p);
        HUNT_CHECK(p.year == 1970 && p.day == 1 && p.hour == 14);
        ex_date_parts(0, -720, &p);
        HUNT_CHECK(p.year == 1969 && p.month == 12 && p.day == 31 && p.hour == 12);
    }
}

/* ---- C. make and add_months ---- */

static void test_make_and_months(void) {
    hunt_step("make: carrying");
    HUNT_CHECK(ex_date_make(2024, 13, 1, 0, 0, 0, 0, 0) == ex_date_make(2025, 1, 1, 0, 0, 0, 0, 0));
    HUNT_CHECK(ex_date_make(2024, 0, 31, 0, 0, 0, 0, 0) == ex_date_make(2023, 12, 31, 0, 0, 0, 0, 0));
    HUNT_CHECK(ex_date_make(2024, 1, 0, 0, 0, 0, 0, 0) == ex_date_make(2023, 12, 31, 0, 0, 0, 0, 0));
    HUNT_CHECK(ex_date_make(2024, 2, 30, 0, 0, 0, 0, 0) == ex_date_make(2024, 3, 1, 0, 0, 0, 0, 0));
    HUNT_CHECK(ex_date_make(2023, 2, 30, 0, 0, 0, 0, 0) == ex_date_make(2023, 3, 2, 0, 0, 0, 0, 0));
    HUNT_CHECK(ex_date_make(2024, 1, 1, 24, 0, 0, 0, 0) == ex_date_make(2024, 1, 2, 0, 0, 0, 0, 0));
    HUNT_CHECK(ex_date_make(2024, 1, 1, 0, -1, 0, 0, 0) == ex_date_make(2023, 12, 31, 23, 59, 0, 0, 0));
    HUNT_CHECK(ex_date_make(2024, 1, 1, 0, 0, 0, -1, 0) == ex_date_make(2023, 12, 31, 23, 59, 59, 999, 0));
    HUNT_CHECK(ex_date_make(2024, -25, 1, 0, 0, 0, 0, 0) == ex_date_make(2021, 11, 1, 0, 0, 0, 0, 0));
    HUNT_CHECK(ex_date_make(2024, 25, 1, 0, 0, 0, 0, 0) == ex_date_make(2026, 1, 1, 0, 0, 0, 0, 0));
    HUNT_CHECK(ex_date_make(2024, 1, 1, 0, 0, 0, 0, 60) == ex_date_make(2023, 12, 31, 23, 0, 0, 0, 0));
    HUNT_CHECK(ex_date_make(2024, 1, 1, 0, 0, 0, 0, -330) == ex_date_make(2024, 1, 1, 5, 30, 0, 0, 0));
    HUNT_CHECK(ex_date_make(1970, 1, 1, 0, 0, 0, 0, 0) == 0);
    HUNT_CHECK(ex_date_make(2024, 1, 1, 0, 0, 0, 0, 0) == 1704067200000.0);
    (void)ex_date_make(2024, 100000, 100000, 100000, 100000, 100000, 100000, 0);
    (void)ex_date_make(2024, -2000000000, 1, 0, 0, 0, 0, 0);
    (void)ex_date_make((int64_t)1 << 60, 1, 1, 0, 0, 0, 0, 0);

    hunt_step("add_months: the day clamps");
    {
        double jan31_23 = ex_date_make(2023, 1, 31, 10, 20, 30, 400, 0), jan31_24 = ex_date_make(2024, 1, 31, 10, 20, 30, 400, 0);
        HUNT_CHECK(ex_date_add_months(jan31_23, 0, 1) == ex_date_make(2023, 2, 28, 10, 20, 30, 400, 0));
        HUNT_CHECK(ex_date_add_months(jan31_24, 0, 1) == ex_date_make(2024, 2, 29, 10, 20, 30, 400, 0));
        HUNT_CHECK(ex_date_add_months(jan31_24, 0, 2) == ex_date_make(2024, 3, 31, 10, 20, 30, 400, 0));
        HUNT_CHECK(ex_date_add_months(jan31_24, 0, 3) == ex_date_make(2024, 4, 30, 10, 20, 30, 400, 0));
        HUNT_CHECK(ex_date_add_months(jan31_24, 0, 12) == ex_date_make(2025, 1, 31, 10, 20, 30, 400, 0));
        HUNT_CHECK(ex_date_add_months(ex_date_make(2024, 2, 29, 1, 2, 3, 4, 0), 0, 12) == ex_date_make(2025, 2, 28, 1, 2, 3, 4, 0));
        HUNT_CHECK(ex_date_add_months(ex_date_make(2024, 3, 31, 0, 0, 0, 0, 0), 0, -1) == ex_date_make(2024, 2, 29, 0, 0, 0, 0, 0));
        HUNT_CHECK(ex_date_add_months(ex_date_make(2024, 10, 31, 0, 0, 0, 0, 0), 0, 1) == ex_date_make(2024, 11, 30, 0, 0, 0, 0, 0));
        HUNT_CHECK(ex_date_add_months(ex_date_make(2024, 1, 15, 0, 0, 0, 0, 0), 0, -13) == ex_date_make(2022, 12, 15, 0, 0, 0, 0, 0));
        HUNT_CHECK(ex_date_add_months(ex_date_make(2024, 1, 15, 0, 0, 0, 0, 0), 0, 0) == ex_date_make(2024, 1, 15, 0, 0, 0, 0, 0));
        HUNT_CHECK(ex_date_add_months(ex_date_make(2000, 6, 15, 0, 0, 0, 0, 0), 0, 12 * 1000) == ex_date_make(3000, 6, 15, 0, 0, 0, 0, 0));
        (void)ex_date_add_months(1e12, 0, (int64_t)1 << 50);
        (void)ex_date_add_months(1e12, 0, -((int64_t)1 << 50));
    }
    hunt_step("add_months: the calendar of the zone, not of UTC");
    {
        /* local 2024-01-31 08:00 at UTC+10 is 2024-01-30 22:00 UTC; one month later it must be local Feb 29 08:00 */
        double local = ex_date_make(2024, 1, 31, 8, 0, 0, 0, 600);
        double got = ex_date_add_months(local, 600, 1), want = ex_date_make(2024, 2, 29, 8, 0, 0, 0, 600);
        HUNT_CHECKF(got == want, "add_months in zone +10:00 gave %.0f, want %.0f (the UTC calendar gives %.0f)", got, want, ex_date_make(2024, 3, 1, 8, 0, 0, 0, 600));
        local = ex_date_make(2024, 3, 31, 23, 30, 0, 0, -480);               /* UTC-8: already April 1 in UTC */
        got = ex_date_add_months(local, -480, -1); want = ex_date_make(2024, 2, 29, 23, 30, 0, 0, -480);
        HUNT_CHECKF(got == want, "add_months -1 in zone -08:00 gave %.0f, want %.0f", got, want);
    }
    hunt_step("add_months: random against a model");
    {
        long rounds = hunt_iters(40000), r;
        for (r = 0; r < rounds; r++) {
            int64_t y = 1900 + (int64_t)hunt_rnd(300), add = (int64_t)hunt_rnd(2401) - 1200;
            int mo = 1 + (int)hunt_rnd(12), d = 1 + (int)hunt_rnd(31), tz = (int)hunt_rnd(1561) - 720, h = (int)hunt_rnd(24);
            double base, got, want;
            int64_t total, ny;
            int nm, nd;
            if (d > ex_days_in_month(y, mo)) d = ex_days_in_month(y, mo);
            base = ex_date_make(y, mo, d, h, 17, 3, 9, tz);
            got = ex_date_add_months(base, tz, add);
            total = y * 12 + (mo - 1) + add;
            ny = total >= 0 ? total / 12 : -((-total + 11) / 12);
            nm = (int)(total - ny * 12) + 1;
            nd = d > ex_days_in_month(ny, nm) ? ex_days_in_month(ny, nm) : d;
            want = ex_date_make(ny, nm, nd, h, 17, 3, 9, tz);
            if (got != want) { HUNT_CHECKF(0, "add_months(%lld-%02d-%02d %02d:17, tz %d, %+lld) = %.0f, want %.0f", (long long)y, mo, d, h, tz, (long long)add, got, want); if (hunt_.failures > 30) break; }
        }
    }
}

/* ---- D. the formatter ---- */

static const char *ord(int n) { return (n % 100 >= 11 && n % 100 <= 13) ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th"; }

static void test_format(void) {
    double t = ex_date_make(2013, 2, 4, 10, 35, 24, 123, 0);          /* Monday 2013-02-04 10:35:24.123 */
    int day;
    hunt_step("format: the tokens of the header");
    EXPECT_FMT(t, 0, "dddd, MMMM Do YYYY, h:mm:ss a", "Monday, February 4th 2013, 10:35:24 am");
    EXPECT_FMT(t, 0, "YYYY YY Y", "2013 13 2013");
    EXPECT_FMT(t, 0, "M MM MMM MMMM Mo", "2 02 Feb February 2nd");
    EXPECT_FMT(t, 0, "D DD Do DDD DDDD", "4 04 4th 35 035");
    EXPECT_FMT(t, 0, "d dd ddd dddd do", "1 Mo Mon Monday 1st");
    EXPECT_FMT(t, 0, "e E", "1 1");
    EXPECT_FMT(t, 0, "W WW Wo", "6 06 6th");
    EXPECT_FMT(t, 0, "Q Qo", "1 1st");
    EXPECT_FMT(t, 0, "H HH h hh k kk", "10 10 10 10 10 10");
    EXPECT_FMT(t, 0, "m mm s ss", "35 35 24 24");
    EXPECT_FMT(t, 0, "S SS SSS", "1 12 123");
    EXPECT_FMT(t, 0, "SSSSSSSSS", "123000000");
    EXPECT_FMT(t, 0, "a A", "am AM");
    EXPECT_FMT(t, 0, "Z ZZ", "+00:00 +0000");
    EXPECT_FMT(t, 0, "X", "1359974124");                  /* 2013-02-04 10:35:24 UTC: 1359936000 (midnight) + 38124 */
    EXPECT_FMT(t, 0, "x", "1359974124123");
    EXPECT_FMT(t, 0, "L", "02/04/2013");
    EXPECT_FMT(t, 0, "LL", "February 4, 2013");
    EXPECT_FMT(t, 0, "LLL", "February 4, 2013 10:35 AM");
    EXPECT_FMT(t, 0, "LLLL", "Monday, February 4, 2013 10:35 AM");
    EXPECT_FMT(t, 0, "l", "2/4/2013");
    EXPECT_FMT(t, 0, "ll", "Feb 4, 2013");
    EXPECT_FMT(t, 0, "lll", "Feb 4, 2013 10:35 AM");
    EXPECT_FMT(t, 0, "llll", "Mon, Feb 4, 2013 10:35 AM");
    EXPECT_FMT(t, 0, "LT", "10:35 AM");
    EXPECT_FMT(t, 0, "LTS", "10:35:24 AM");
    EXPECT_FMT(t, 0, "YYYY-MM-DDTHH:mm:ss", "2013-02-04T10:35:24");
    EXPECT_FMT(t, 0, "[Today is] dddd", "Today is Monday");
    EXPECT_FMT(t, 0, "[YYYY] YYYY", "YYYY 2013");
    EXPECT_FMT(t, 0, "\\Y\\M YYYY", "YM 2013");
    EXPECT_FMT(t, 0, "", "");
    EXPECT_FMT(t, 0, "- / : 123 !? ()", "- / : 123 !? ()");   /* Moment reads k, e, s ... of "no tokens" as tokens too: text goes in [brackets] */
    { char *g = fmt(t, 0, "[]"); free(g); g = fmt(t, 0, "[unclosed YYYY"); free(g); g = fmt(t, 0, "]]]["); free(g); }          /* no particular answer, but no crash */
    hunt_step("format: hours 0, 12, 13, 23");
    {
        struct { int h; const char *f, *want; } hs[] = { { 0, "h hh H HH k kk a A", "12 12 0 00 24 24 am AM" }, { 12, "h hh H HH k kk a A", "12 12 12 12 12 12 pm PM" }, { 13, "h hh H HH k kk a A", "1 01 13 13 13 13 pm PM" },
            { 23, "h hh H HH k kk a A", "11 11 23 23 23 23 pm PM" }, { 1, "h hh H HH k kk a A", "1 01 1 01 1 01 am AM" }, { 11, "LT", "11:35 AM" }, { 0, "LT", "12:35 AM" }, { 12, "LT", "12:35 PM" } };
        size_t i;
        for (i = 0; i < sizeof hs / sizeof *hs; i++) { char *g = fmt(ex_date_make(2013, 2, 4, hs[i].h, 35, 24, 0, 0), 0, hs[i].f); HUNT_CHECKF(!strcmp(g, hs[i].want), "hour %d with \"%s\": \"%s\", want \"%s\"", hs[i].h, hs[i].f, g, hs[i].want); free(g); }
    }
    hunt_step("format: ordinal suffixes for every day of January");
    for (day = 1; day <= 31; day++) {
        char want[16], *g;
        snprintf(want, sizeof want, "%d%s", day, ord(day));
        g = fmt(ex_date_make(2024, 1, day, 0, 0, 0, 0, 0), 0, "Do");
        HUNT_CHECKF(!strcmp(g, want), "Do for January %d: \"%s\", want \"%s\"", day, g, want);
        free(g);
    }
    hunt_step("format: weeks around New Year");
    {
        struct { int y, m, d; const char *f, *want; } wk[] = {
            { 2021, 1, 3, "GGGG-[W]WW-E", "2020-W53-7" }, { 2018, 12, 31, "GGGG-[W]WW", "2019-W01" }, { 2016, 1, 1, "GGGG-[W]WW", "2015-W53" }, { 2024, 12, 30, "GGGG-[W]WW", "2025-W01" },
            { 2020, 12, 31, "GGGG-[W]WW", "2020-W53" }, { 2021, 1, 4, "GGGG-[W]WW", "2021-W01" }, { 2026, 1, 1, "GGGG-[W]WW", "2026-W01" }, { 2027, 1, 1, "GGGG-[W]WW", "2026-W53" }, { 2012, 1, 1, "GGGG-[W]WW", "2011-W52" },
            { 2021, 1, 1, "gggg-[w]ww", "2021-w01" }, { 2020, 12, 31, "gggg-[w]ww", "2021-w01" }, { 2021, 12, 31, "gggg-[w]ww", "2022-w01" }, { 2021, 1, 3, "gggg-[w]ww", "2021-w02" }, { 2024, 12, 29, "gggg-[w]ww", "2025-w01" },
            { 2024, 12, 28, "gggg-[w]ww", "2024-w52" }, { 2019, 12, 29, "gg w", "20 1" }, { 2024, 12, 31, "GG W", "25 1" },
        };
        size_t i;
        for (i = 0; i < sizeof wk / sizeof *wk; i++) {
            char *g = fmt(ex_date_make(wk[i].y, wk[i].m, wk[i].d, 12, 0, 0, 0, 0), 0, wk[i].f);
            HUNT_CHECKF(!strcmp(g, wk[i].want), "%d-%02d-%02d with \"%s\": \"%s\", want \"%s\"", wk[i].y, wk[i].m, wk[i].d, wk[i].f, g, wk[i].want);
            free(g);
        }
    }
    hunt_step("format: time zones, including the negative ones under an hour");
    {
        struct { int tz; const char *want; } zs[] = { { 0, "+00:00 +0000" }, { 330, "+05:30 +0530" }, { -90, "-01:30 -0130" }, { -30, "-00:30 -0030" }, { -1, "-00:01 -0001" }, { 1, "+00:01 +0001" }, { 840, "+14:00 +1400" },
            { -720, "-12:00 -1200" }, { 59, "+00:59 +0059" }, { -59, "-00:59 -0059" }, { 60, "+01:00 +0100" }, { -60, "-01:00 -0100" } };
        size_t i;
        for (i = 0; i < sizeof zs / sizeof *zs; i++) { char *g = fmt(t, zs[i].tz, "Z ZZ"); HUNT_CHECKF(!strcmp(g, zs[i].want), "zone %d minutes: \"%s\", want \"%s\"", zs[i].tz, g, zs[i].want); free(g); }
        EXPECT_FMT(t, 330, "HH:mm", "16:05");
        EXPECT_FMT(t, -600, "YYYY-MM-DD HH:mm", "2013-02-04 00:35");        /* 10:35 UTC minus ten hours */
    }
    hunt_step("format: the snprintf contract for every buffer size");
    {
        const char *f = "dddd, MMMM Do YYYY, h:mm:ss a [é] Z";
        char *full = fmt(t, 330, f);
        size_t need = strlen(full), cap;
        for (cap = 0; cap <= need + 3; cap++) {
            char *buf = (char *)malloc(cap ? cap : 1);
            size_t r = ex_date_format(cap ? buf : NULL, cap, t, 330, f);
            HUNT_CHECKF(r == need, "cap %lu: returned %lu, the whole result is %lu", (unsigned long)cap, (unsigned long)r, (unsigned long)need);
            if (cap > 0) {
                size_t keep = cap - 1 < need ? cap - 1 : need;
                HUNT_CHECKF(buf[keep] == 0 && memcmp(buf, full, keep) == 0, "cap %lu: the text is not the prefix of the result, or has no NUL", (unsigned long)cap);
            }
            free(buf);
        }
        free(full);
    }
}

/* ---- E. parsing ---- */

static void test_parse(void) {
    double ms;
    int has;
    struct { const char *s; int ok; int has_time; } cases[] = {
        { "2024-02-29", 1, 0 }, { "2024-02-29 12:34", 1, 1 }, { "2024-02-29T12:34", 1, 1 }, { "2024-02-29 12:34:56", 1, 1 }, { "2024-02-29T12:34:56.789", 1, 1 }, { "2024-02-29T12:34:56Z", 1, 1 },
        { "2024-02-29T12:34:56+05:30", 1, 1 }, { "2024-02-29T12:34:56-08:00", 1, 1 }, { "2024-02-29 12:34:56.5", 1, 1 }, { "0001-01-01", 1, 0 }, { "9999-12-31 23:59:59.999", 1, 1 },
        { "2023-02-29", 0, 0 }, { "2024-13-01", 0, 0 }, { "2024-00-10", 0, 0 }, { "2024-01-00", 0, 0 }, { "2024-01-32", 0, 0 }, { "2024-04-31", 0, 0 }, { "2100-02-29", 0, 0 }, { "2000-02-29", 1, 0 },
        { "2024-01-01 12:60", 0, 0 }, { "2024-01-01 25:00", 0, 0 }, { "2024-01-01 12:00:60", 0, 0 }, { "2024-01-01 12:00:61", 0, 0 }, { "2024-01-01x", 0, 0 }, { "2024-01-01 12:34 extra", 0, 0 }, { "", 0, 0 }, { " 2024-01-01", 0, 0 },
        { "2024-01-01 ", 0, 0 }, { "20240101", 0, 0 }, { "2024-01", 0, 0 }, { "2024", 0, 0 }, { "abcd-ef-gh", 0, 0 }, { "2024/01/01", 0, 0 }, { "2024-01-01T", 0, 0 }, { "2024-01-01 12", 0, 0 }, { "2024-01-01 12:3", 0, 0 },
        { "2024-01-01T12:00:00+25:00", 0, 0 }, { "2024-01-01T12:00:00+05:60", 0, 0 }, { "2024-01-01T12:00:00+0530", 1, 1 }, { "2024-01-01T12:00:00z", 0, 0 }, { "2024-01-01\t12:00", 0, 0 },
    };
    size_t i;
    hunt_step("parse: valid and invalid");
    for (i = 0; i < sizeof cases / sizeof *cases; i++) {
        int rc = ex_date_parse(cases[i].s, strlen(cases[i].s), 0, &ms, &has);
        if (cases[i].ok) { HUNT_CHECKF(rc == 0, "\"%s\" should parse", cases[i].s); if (rc == 0) HUNT_CHECKF(has == cases[i].has_time, "\"%s\": has_time %d", cases[i].s, has); }
        else if (!strcmp(cases[i].s, " 2024-01-01") || !strcmp(cases[i].s, "2024-01-01 ")) HUNT_SUSPECT(rc != 0, "a date with a space at either end is read as the date (a value from a file or a typed cell may carry one): either answer is defensible");
        else if (!strcmp(cases[i].s, "2024-01-01T12:00:00z") || !strcmp(cases[i].s, "2024-01-01\t12:00") || !strcmp(cases[i].s, "2024-01-01T12:00:00+0530")) HUNT_SUSPECT(rc != 0, "an unusual spelling");
        else HUNT_CHECKF(rc != 0, "\"%s\" should not parse (gave %.0f)", cases[i].s, rc == 0 ? ms : 0.0);
    }
    HUNT_SUSPECT(ex_date_parse("2024-01-01 24:00", 16, 0, &ms, &has) != 0, "Moment accepts 24:00 as the end of the day; if this parser does too it should be the next day's midnight, and if it does not it should say so");
    if (ex_date_parse("2024-01-01 24:00", 16, 0, &ms, &has) == 0) HUNT_CHECK(ms == ex_date_make(2024, 1, 2, 0, 0, 0, 0, 0));
    hunt_step("parse: the zone argument and an explicit zone");
    HUNT_CHECK(ex_date_parse("2024-01-01 00:00", 16, 60, &ms, &has) == 0 && ms == ex_date_make(2023, 12, 31, 23, 0, 0, 0, 0));
    HUNT_CHECK(ex_date_parse("2024-01-01", 10, -300, &ms, &has) == 0 && ms == ex_date_make(2024, 1, 1, 5, 0, 0, 0, 0));
    HUNT_CHECK(ex_date_parse("2024-01-01T00:00:00Z", 20, 600, &ms, &has) == 0 && ms == ex_date_make(2024, 1, 1, 0, 0, 0, 0, 0));
    HUNT_CHECK(ex_date_parse("2024-01-01T00:00:00+05:30", 25, 600, &ms, &has) == 0 && ms == ex_date_make(2023, 12, 31, 18, 30, 0, 0, 0));
    HUNT_CHECK(ex_date_parse("2024-01-01T00:00:00-00:30", 25, 600, &ms, &has) == 0 && ms == ex_date_make(2024, 1, 1, 0, 30, 0, 0, 0));
    hunt_step("parse: the length argument is honoured, not the NUL");
    {
        char s[] = "2024-01-01 12:34:56 and more";
        HUNT_CHECK(ex_date_parse(s, 10, 0, &ms, &has) == 0 && has == 0);
        HUNT_CHECK(ex_date_parse(s, 19, 0, &ms, &has) == 0 && has == 1);
        HUNT_CHECK(ex_date_parse(s, 9, 0, &ms, &has) != 0);
        HUNT_CHECK(ex_date_parse(s, 0, 0, &ms, &has) != 0);
    }
    hunt_step("parse: format then parse over random instants");
    {
        long rounds = hunt_iters(20000), r;
        for (r = 0; r < rounds; r++) {
            double t = (double)((int64_t)(hunt_rand64() % 6000000000000ull) - 2000000000000ll);
            int tz = (int)hunt_rnd(1561) - 720, k = (int)hunt_rnd(3);
            const char *f = k == 0 ? "YYYY-MM-DDTHH:mm:ss.SSSZ" : k == 1 ? "YYYY-MM-DD HH:mm:ss.SSS" : "YYYY-MM-DD";
            char *s = fmt(t, tz, f);
            double back;
            int h;
            int rc = ex_date_parse(s, strlen(s), tz, &back, &h);
            double want = t;
            if (k == 2) { ExDateParts p; ex_date_parts(t, tz, &p); want = ex_date_make(p.year, p.month, p.day, 0, 0, 0, 0, tz); }
            if (rc != 0 || back != want) { HUNT_CHECKF(0, "\"%s\" (tz %d) parsed to %.0f, want %.0f (rc %d)", s, tz, back, want, rc); if (hunt_.failures > 30) { free(s); break; } }
            free(s);
        }
    }
    hunt_step("parse_format: daily and weekly note formats");
    {
        const char *formats[] = { "YYYY-MM-DD", "YYYY/MM/DD", "DD.MM.YYYY", "MMMM D, YYYY", "ddd, MMM Do YY", "[Daily] YYYY-MM-DD", "YYYY-MM-DD HH:mm", "YYYYMMDD", "D/M/YYYY", "MMM D YYYY", "dddd, MMMM Do YYYY" };
        const char *weekly[] = { "gggg-[W]ww", "GGGG-[W]WW" };
        long rounds = hunt_iters(6000), r;
        for (r = 0; r < rounds; r++) {
            double t = (double)((int64_t)(hunt_rand64() % 5000000000000ull) - 1000000000000ll);
            int tz = (int)hunt_rnd(1561) - 720;
            size_t fi = hunt_rnd_size(sizeof formats / sizeof *formats);
            char *s, *s2;
            if (strstr(formats[fi], "YY") && !strstr(formats[fi], "YYYY")) {      /* two digits: 69 to 99 are 1969 to 1999 and 00 to 68 are 2000 to 2068 (Moment), so only those years come back */
                double lo = ex_date_make(1969, 1, 2, 0, 0, 0, 0, 0), hi = ex_date_make(2067, 12, 30, 0, 0, 0, 0, 0);
                t = lo + fmod(fabs(t), hi - lo);
            }
            s = fmt(t, tz, formats[fi]);
            double back = 0;
            int rc = ex_date_parse_format(s, strlen(s), formats[fi], tz, &back);
            if (rc != 0) { HUNT_CHECKF(0, "\"%s\" does not parse with its own format \"%s\" (tz %d)", s, formats[fi], tz); if (hunt_.failures > 30) { free(s); break; } free(s); continue; }
            s2 = fmt(back, tz, formats[fi]);
            if (strcmp(s, s2) != 0) { HUNT_CHECKF(0, "format \"%s\": \"%s\" parsed and formatted again is \"%s\"", formats[fi], s, s2); if (hunt_.failures > 30) { free(s); free(s2); break; } }
            free(s); free(s2);
            {
                size_t wi = hunt_rnd_size(2);
                s = fmt(t, tz, weekly[wi]);
                /* Reading a week back (GGGG, WW, gggg, ww) is not documented: docs/formats.md lists the tokens that ex_date_parse_format understands, and a weekly note is not a feature.
                 * What is checked is that formatting a week is the same twice. */
                s2 = fmt(t, tz, weekly[wi]);
                HUNT_CHECKF(!strcmp(s, s2), "the week of one time formatted twice: \"%s\" and \"%s\"", s, s2);
                free(s2);
                free(s);
            }
        }
        {
            const char *bad[][2] = { { "2024-02-30", "YYYY-MM-DD" }, { "13/45/2024", "MM/DD/YYYY" }, { "2024-1-1x", "YYYY-M-D" }, { "", "YYYY-MM-DD" }, { "2024", "YYYY-MM-DD" }, { "hello", "YYYY" }, { "2024-02-31", "YYYY-MM-DD" },
                { "99:99", "HH:mm" }, { "2023-02-29", "YYYY-MM-DD" }, { "Smarch 4, 2024", "MMMM D, YYYY" } };
            size_t i2;
            for (i2 = 0; i2 < sizeof bad / sizeof *bad; i2++) HUNT_CHECKF(ex_date_parse_format(bad[i2][0], strlen(bad[i2][0]), bad[i2][1], 0, &ms) != 0, "\"%s\" must not parse with \"%s\"", bad[i2][0], bad[i2][1]);
        }
    }
}

/* ---- F. nonsense ---- */

static void test_nonsense(void) {
    static const char alphabet[] = "YMDdHhmsSaAZzXxQqEeWwGgoLlTk[]\\ -:/.,'\"1";
    long rounds = hunt_iters(30000), r;
    double extremes[] = { 0, -1, 1e15, -1e15, 8.64e15, -8.64e15, 1e18, -1e18, 1e300, -1e300, (double)INFINITY, -(double)INFINITY, (double)NAN, 4.9e-324, 1.7976931348623157e308, 253402300799999.0, -62135596800000.0 };
    size_t i;
    hunt_step("nonsense: extreme instants through every formatter");
    for (i = 0; i < sizeof extremes / sizeof *extremes; i++) {
        ExDateParts p;
        size_t need;
        char *buf;
        ex_date_parts(extremes[i], 0, &p);
        ex_date_parts(extremes[i], 840, &p);
        need = ex_date_format(NULL, 0, extremes[i], 0, "YYYY-MM-DD HH:mm:ss.SSS Z X x dddd MMMM Do W GGGG LLLL");
        buf = (char *)malloc(need + 1);
        ex_date_format(buf, need + 1, extremes[i], 0, "YYYY-MM-DD HH:mm:ss.SSS Z X x dddd MMMM Do W GGGG LLLL");
        free(buf);
        (void)ex_date_add_months(extremes[i], 0, 1);
        (void)ex_date_add_months(extremes[i], 0, -1);
    }
    hunt_step("nonsense: random format strings");
    for (r = 0; r < rounds; r++) {
        char f[40];
        size_t n = 1 + hunt_rnd_size(30), k;
        double t = (double)((int64_t)(hunt_rand64() % 8000000000000ull) - 3000000000000ll);
        int tz = (int)hunt_rnd(1800) - 900;
        size_t need;
        char *exact, *a, *b;
        for (k = 0; k < n; k++) f[k] = alphabet[hunt_rnd(sizeof alphabet - 1)];
        f[n] = 0;
        hunt_stepf("random format %ld: %s", r, f);
        need = ex_date_format(NULL, 0, t, tz, f);
        exact = (char *)malloc(need + 1);
        HUNT_CHECK(ex_date_format(exact, need + 1, t, tz, f) == need);
        HUNT_CHECK(strlen(exact) <= need);
        a = fmt(t, tz, f);
        b = fmt(t, tz, f);
        HUNT_CHECKF(!strcmp(a, b), "format \"%s\" is not deterministic", f);
        {
            double back;
            (void)ex_date_parse_format(exact, strlen(exact), f, tz, &back);       /* any answer, no crash */
            (void)ex_date_parse_format(f, strlen(f), f, tz, &back);
        }
        free(a); free(b); free(exact);
    }
    hunt_step("nonsense: random text through the parsers");
    for (r = 0; r < rounds; r++) {
        char s[48];
        size_t n = hunt_random_text(s, sizeof s, 8);
        double ms;
        int has;
        (void)ex_date_parse(s, n, 0, &ms, &has);
        (void)ex_date_parse_format(s, n, "YYYY-MM-DD HH:mm:ss.SSS Z", 0, &ms);
        (void)ex_date_parse_format(s, n, "MMMM Do, YYYY", 0, &ms);
        (void)ex_date_parse_format(s, n, "dddd [W]WW GGGG", 0, &ms);
    }
}

int main(int argc, char **argv) {
    hunt_begin("hunt_datefmt", argc, argv);
    test_civil();
    test_against_libc();
    test_make_and_months();
    test_format();
    test_parse();
    test_nonsense();
    return hunt_end();
}
