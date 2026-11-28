/* Dates: see include/exhaust_datefmt.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_datefmt.h"

static const char *const MONTHS[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
static const char *const DAYS[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

int64_t ex_days_from_civil(int64_t y, int m, int d) {          /* Howard Hinnant's algorithm */
    int64_t era, yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void ex_civil_from_days(int64_t z, int64_t *y, int *m, int *d) {
    int64_t era, doe, yoe, yy, doy, mp;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    yy = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = yy + (*m <= 2);
}

static int leap(int64_t y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
int ex_days_in_month(int64_t y, int m) { static const int dm[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }; return m == 2 && leap(y) ? 29 : dm[(m - 1) % 12]; }

static double floor_div(double a, double b) { return floor(a / b); }

void ex_date_parts(double ms, int tz_min, ExDateParts *p) {
    double local = ms + (double)tz_min * 60000.0, days, rem;
    int64_t y;
    int m, d;
    memset(p, 0, sizeof *p);
    if (!isfinite(local)) { p->year = 1970; p->month = 1; p->day = 1; return; }
    days = floor_div(local, 86400000.0);
    rem = local - days * 86400000.0;
    ex_civil_from_days((int64_t)days, &y, &m, &d);
    p->year = (int)y; p->month = m; p->day = d;
    p->hour = (int)(rem / 3600000.0); rem -= p->hour * 3600000.0;
    p->minute = (int)(rem / 60000.0); rem -= p->minute * 60000.0;
    p->second = (int)(rem / 1000.0); rem -= p->second * 1000.0;
    p->ms = (int)rem;
    p->weekday = (int)(((int64_t)days % 7 + 11) % 7);                    /* 1970-01-01 was a Thursday (4) */
    p->yday = (int)(days - (double)ex_days_from_civil(y, 1, 1)) + 1;
}

double ex_date_make(int64_t y, int mo, int d, int h, int mi, int s, int ms, int tz_min) {
    int64_t yy = y + (mo - 1 >= 0 ? (mo - 1) / 12 : -((12 - mo) / 12));
    int mm = (int)(((mo - 1) % 12 + 12) % 12) + 1;
    double days = (double)ex_days_from_civil(yy, mm, 1) + (double)(d - 1);
    return days * 86400000.0 + (double)h * 3600000.0 + (double)mi * 60000.0 + (double)s * 1000.0 + (double)ms - (double)tz_min * 60000.0;
}

double ex_date_add_months(double ms, int tz_min, int64_t months) {
    ExDateParts p;
    int64_t total, y;
    int m, d;
    ex_date_parts(ms, tz_min, &p);
    total = (int64_t)p.year * 12 + (p.month - 1) + months;
    y = total >= 0 ? total / 12 : -((11 - total) / 12);
    m = (int)(total - y * 12) + 1;
    d = p.day;
    if (d > ex_days_in_month(y, m)) d = ex_days_in_month(y, m);
    return ex_date_make(y, m, d, p.hour, p.minute, p.second, p.ms, tz_min);
}

/* ---- ISO and locale weeks ---- */

/* The week of the year for a week that starts on dow (0 Sunday, 1 Monday) and holds the day doy of January (Moment: ISO is 1,4; en-US is 0,6). Returns the week and its year. */
static int week_of_year(const ExDateParts *p, int dow, int doy, int *wyear) {
    int64_t y = p->year, jan1 = ex_days_from_civil(y, 1, 1), day = jan1 + p->yday - 1;
    int64_t start;
    for (;;) {
        int64_t first = ex_days_from_civil(y, 1, doy), fdow = (((first % 7) + 11) % 7);
        start = first - ((fdow - dow + 7) % 7);                       /* the first day of week 1 of year y */
        if (day < start) { y--; continue; }
        {
            int64_t nfirst = ex_days_from_civil(y + 1, 1, doy), nfdow = (((nfirst % 7) + 11) % 7), nstart = nfirst - ((nfdow - dow + 7) % 7);
            if (day >= nstart) { y++; start = nstart; }
        }
        break;
    }
    *wyear = (int)y;
    return (int)((day - start) / 7) + 1;
}

/* ---- the formatter ---- */

typedef struct Out { char *p; size_t cap, n; } Out;
static void put(Out *o, const char *s, size_t n) {
    if (o->cap && o->n < o->cap - 1) { size_t room = o->cap - 1 - o->n; memcpy(o->p + o->n, s, n < room ? n : room); }
    o->n += n;
}
static void puts_(Out *o, const char *s) { put(o, s, strlen(s)); }
static void put_int(Out *o, long long v, int width, int pad_char) {
    char b[40];
    int neg = v < 0;
    size_t len;
    if (neg) v = -v;
    len = (size_t)snprintf(b, sizeof b, "%lld", v);
    if (neg) put(o, "-", 1);
    while (width > 0 && (int)len < width) { put(o, pad_char ? "0" : " ", 1); width--; }
    put(o, b, len);
}
static void ordinal(Out *o, long long v) {
    long long m100 = v % 100, m10 = v % 10;
    put_int(o, v, 0, 0);
    puts_(o, (m100 >= 11 && m100 <= 13) ? "th" : m10 == 1 ? "st" : m10 == 2 ? "nd" : m10 == 3 ? "rd" : "th");
}
static void offset(Out *o, int tz_min, int colon) {
    int a = tz_min < 0 ? -tz_min : tz_min;
    put(o, tz_min < 0 ? "-" : "+", 1);
    put_int(o, a / 60, 2, 1);
    if (colon) put(o, ":", 1);
    put_int(o, a % 60, 2, 1);
}

static int token_is(const char *f, const char *t) { return !strncmp(f, t, strlen(t)); }
static int strncasecmp_ascii(const char *a, const char *b, size_t n) { size_t i; for (i = 0; i < n; i++) { char x = a[i], y = b[i]; if (x >= 'a' && x <= 'z') x = (char)(x - 32); if (y >= 'a' && y <= 'z') y = (char)(y - 32); if (x != y) return 1; } return 0; }

static size_t format_into(Out *o, double ms, int tz_min, const char *fmt, int depth);

static size_t format_into(Out *o, double ms, int tz_min, const char *fmt, int depth) {
    ExDateParts p;
    const char *f = fmt;
    int hour12;
    ex_date_parts(ms, tz_min, &p);
    hour12 = p.hour % 12 == 0 ? 12 : p.hour % 12;
    while (*f) {
        if (*f == '[') {                                                       /* text as it is */
            const char *e = strchr(f + 1, ']');
            if (!e) { put(o, f, strlen(f)); break; }
            put(o, f + 1, (size_t)(e - f - 1));
            f = e + 1;
            continue;
        }
        if (*f == '\\' && f[1]) { put(o, f + 1, 1); f += 2; continue; }
        if (depth < 2 && token_is(f, "LLLL")) { format_into(o, ms, tz_min, "dddd, MMMM D, YYYY h:mm A", depth + 1); f += 4; continue; }
        if (depth < 2 && token_is(f, "llll")) { format_into(o, ms, tz_min, "ddd, MMM D, YYYY h:mm A", depth + 1); f += 4; continue; }     /* Moment's English (US): the short names */
        if (depth < 2 && token_is(f, "LLL")) { format_into(o, ms, tz_min, "MMMM D, YYYY h:mm A", depth + 1); f += 3; continue; }
        if (depth < 2 && token_is(f, "lll")) { format_into(o, ms, tz_min, "MMM D, YYYY h:mm A", depth + 1); f += 3; continue; }
        if (depth < 2 && token_is(f, "LTS")) { format_into(o, ms, tz_min, "h:mm:ss A", depth + 1); f += 3; continue; }
        if (depth < 2 && token_is(f, "LT")) { format_into(o, ms, tz_min, "h:mm A", depth + 1); f += 2; continue; }
        if (depth < 2 && token_is(f, "LL")) { format_into(o, ms, tz_min, "MMMM D, YYYY", depth + 1); f += 2; continue; }
        if (depth < 2 && token_is(f, "ll")) { format_into(o, ms, tz_min, "MMM D, YYYY", depth + 1); f += 2; continue; }
        if (depth < 2 && token_is(f, "L") ) { format_into(o, ms, tz_min, "MM/DD/YYYY", depth + 1); f += 1; continue; }
        if (depth < 2 && token_is(f, "l")) { format_into(o, ms, tz_min, "M/D/YYYY", depth + 1); f += 1; continue; }
        if (token_is(f, "YYYY")) { put_int(o, p.year, 4, 1); f += 4; }
        else if (token_is(f, "YY")) { put_int(o, ((p.year % 100) + 100) % 100, 2, 1); f += 2; }
        else if (*f == 'Y') { put_int(o, p.year, 0, 0); f += 1; }
        else if (token_is(f, "MMMM")) { puts_(o, MONTHS[p.month - 1]); f += 4; }
        else if (token_is(f, "MMM")) { put(o, MONTHS[p.month - 1], 3); f += 3; }
        else if (token_is(f, "MM")) { put_int(o, p.month, 2, 1); f += 2; }
        else if (token_is(f, "Mo")) { ordinal(o, p.month); f += 2; }
        else if (*f == 'M') { put_int(o, p.month, 0, 0); f += 1; }
        else if (token_is(f, "Qo")) { ordinal(o, (p.month + 2) / 3); f += 2; }
        else if (*f == 'Q') { put_int(o, (p.month + 2) / 3, 0, 0); f += 1; }
        else if (token_is(f, "DDDD")) { put_int(o, p.yday, 3, 1); f += 4; }
        else if (token_is(f, "DDD") && !token_is(f, "DDDD")) { put_int(o, p.yday, 0, 0); f += 3; }
        else if (token_is(f, "DD")) { put_int(o, p.day, 2, 1); f += 2; }
        else if (token_is(f, "Do")) { ordinal(o, p.day); f += 2; }
        else if (*f == 'D') { put_int(o, p.day, 0, 0); f += 1; }
        else if (token_is(f, "dddd")) { puts_(o, DAYS[p.weekday]); f += 4; }
        else if (token_is(f, "ddd")) { put(o, DAYS[p.weekday], 3); f += 3; }
        else if (token_is(f, "dd")) { put(o, DAYS[p.weekday], 2); f += 2; }
        else if (token_is(f, "do")) { ordinal(o, p.weekday); f += 2; }
        else if (*f == 'd') { put_int(o, p.weekday, 0, 0); f += 1; }
        else if (*f == 'e') { put_int(o, p.weekday, 0, 0); f += 1; }                                  /* the locale's day of the week: Sunday first */
        else if (*f == 'E') { put_int(o, p.weekday == 0 ? 7 : p.weekday, 0, 0); f += 1; }            /* ISO: Monday is 1 */
        else if (token_is(f, "ww")) { int y; put_int(o, week_of_year(&p, 0, 1, &y), 2, 1); f += 2; }
        else if (token_is(f, "wo")) { int y; ordinal(o, week_of_year(&p, 0, 1, &y)); f += 2; }
        else if (*f == 'w') { int y; put_int(o, week_of_year(&p, 0, 1, &y), 0, 0); f += 1; }
        else if (token_is(f, "WW")) { int y; put_int(o, week_of_year(&p, 1, 4, &y), 2, 1); f += 2; }
        else if (token_is(f, "Wo")) { int y; ordinal(o, week_of_year(&p, 1, 4, &y)); f += 2; }
        else if (*f == 'W') { int y; put_int(o, week_of_year(&p, 1, 4, &y), 0, 0); f += 1; }
        else if (token_is(f, "gggg")) { int y; week_of_year(&p, 0, 1, &y); put_int(o, y, 4, 1); f += 4; }
        else if (token_is(f, "gg")) { int y; week_of_year(&p, 0, 1, &y); put_int(o, ((y % 100) + 100) % 100, 2, 1); f += 2; }
        else if (token_is(f, "GGGG")) { int y; week_of_year(&p, 1, 4, &y); put_int(o, y, 4, 1); f += 4; }
        else if (token_is(f, "GG")) { int y; week_of_year(&p, 1, 4, &y); put_int(o, ((y % 100) + 100) % 100, 2, 1); f += 2; }
        else if (token_is(f, "HH")) { put_int(o, p.hour, 2, 1); f += 2; }
        else if (*f == 'H') { put_int(o, p.hour, 0, 0); f += 1; }
        else if (token_is(f, "hh")) { put_int(o, hour12, 2, 1); f += 2; }
        else if (*f == 'h') { put_int(o, hour12, 0, 0); f += 1; }
        else if (token_is(f, "kk")) { put_int(o, p.hour == 0 ? 24 : p.hour, 2, 1); f += 2; }
        else if (*f == 'k') { put_int(o, p.hour == 0 ? 24 : p.hour, 0, 0); f += 1; }
        else if (token_is(f, "mm")) { put_int(o, p.minute, 2, 1); f += 2; }
        else if (*f == 'm') { put_int(o, p.minute, 0, 0); f += 1; }
        else if (token_is(f, "ss")) { put_int(o, p.second, 2, 1); f += 2; }
        else if (*f == 's') { put_int(o, p.second, 0, 0); f += 1; }
        else if (*f == 'S') {                                                                          /* S to SSSSSSSSS: fractions of a second */
            int n = 0, k;
            char digits[10];
            while (f[n] == 'S' && n < 9) n++;
            snprintf(digits, sizeof digits, "%03d", p.ms);
            for (k = 0; k < n; k++) put(o, k < 3 ? digits + k : "0", 1);
            f += n;
        }
        else if (*f == 'A') { puts_(o, p.hour < 12 ? "AM" : "PM"); f += 1; }
        else if (*f == 'a') { puts_(o, p.hour < 12 ? "am" : "pm"); f += 1; }
        else if (token_is(f, "ZZ")) { offset(o, tz_min, 0); f += 2; }
        else if (*f == 'Z') { offset(o, tz_min, 1); f += 1; }
        else if (token_is(f, "zz")) { puts_(o, tz_min == 0 ? "UTC" : ""); f += 2; }
        else if (*f == 'z') { puts_(o, tz_min == 0 ? "UTC" : ""); f += 1; }
        else if (*f == 'X') { put_int(o, (long long)floor(ms / 1000.0), 0, 0); f += 1; }
        else if (*f == 'x') { put_int(o, (long long)floor(ms), 0, 0); f += 1; }
        else { put(o, f, 1); f += 1; }
    }
    return o->n;
}

size_t ex_date_format(char *out, size_t cap, double ms, int tz_min, const char *fmt) {
    Out o;
    o.p = out; o.cap = cap; o.n = 0;
    format_into(&o, ms, tz_min, fmt ? fmt : "", 0);
    if (cap) out[o.n < cap ? o.n : cap - 1] = 0;
    return o.n;
}

/* ---- parsing ---- */

static int digits(const char *s, size_t n, size_t *pos, int count, int *out) {
    int v = 0, i;
    if (*pos + (size_t)count > n) return -1;
    for (i = 0; i < count; i++) { char c = s[*pos + (size_t)i]; if (c < '0' || c > '9') return -1; v = v * 10 + (c - '0'); }
    *pos += (size_t)count;
    *out = v;
    return 0;
}

int ex_date_parse(const char *s, size_t n, int tz_min, double *ms, int *has_time) {
    size_t pos = 0;
    int y, mo, d, h = 0, mi = 0, sec = 0, frac = 0, tz = tz_min, time = 0;
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    while (pos < n && (s[pos] == ' ' || s[pos] == '\t')) pos++;
    if (digits(s, n, &pos, 4, &y) || pos >= n || s[pos++] != '-' || digits(s, n, &pos, 2, &mo) || pos >= n || s[pos++] != '-' || digits(s, n, &pos, 2, &d)) return -1;
    if (mo < 1 || mo > 12 || d < 1 || d > ex_days_in_month(y, mo)) return -1;
    if (pos < n) {
        if (s[pos] != ' ' && s[pos] != 'T') return -1;
        pos++;
        if (digits(s, n, &pos, 2, &h) || pos >= n || s[pos++] != ':' || digits(s, n, &pos, 2, &mi)) return -1;
        time = 1;
        if (pos < n && s[pos] == ':') {
            pos++;
            if (digits(s, n, &pos, 2, &sec)) return -1;
            if (pos < n && s[pos] == '.') {
                int digs = 0, v = 0;
                pos++;
                while (pos < n && s[pos] >= '0' && s[pos] <= '9') { if (digs < 3) { v = v * 10 + (s[pos] - '0'); digs++; } pos++; }
                if (!digs) return -1;
                while (digs++ < 3) v *= 10;
                frac = v;
            }
        }
        if (h > 23 || mi > 59 || sec > 59) return -1;
        if (pos < n) {
            if (s[pos] == 'Z') { tz = 0; pos++; }
            else if (s[pos] == '+' || s[pos] == '-') {
                int sign = s[pos] == '-' ? -1 : 1, th, tm = 0;
                pos++;
                if (digits(s, n, &pos, 2, &th)) return -1;
                if (pos < n && s[pos] == ':') pos++;
                if (pos < n && digits(s, n, &pos, 2, &tm)) return -1;
                if (th > 23 || tm > 59) return -1;                 /* +25:00 and +05:60 are not zones */
                tz = sign * (th * 60 + tm);
            }
            if (pos != n) return -1;
        }
    }
    *ms = ex_date_make(y, mo, d, h, mi, sec, frac, tz);
    if (has_time) *has_time = time;
    return 0;
}

/* The formatted text back into a date: the tokens a daily note's name is made of. */
int ex_date_parse_format(const char *s, size_t n, const char *fmt, int tz_min, double *ms) {
    size_t pos = 0;
    const char *f = fmt;
    int y = 1970, mo = 1, d = 1, h = 0, mi = 0, sec = 0, frac = 0, pm = -1, h12 = -1, yday = 0, seen_date = 0;
    while (*f) {
        int v;
        if (*f == '[') {
            const char *e = strchr(f + 1, ']');
            size_t len = e ? (size_t)(e - f - 1) : strlen(f + 1);
            if (pos + len > n || strncmp(s + pos, f + 1, len)) return -1;
            pos += len; f = e ? e + 1 : f + 1 + len;
            continue;
        }
        if (*f == '\\' && f[1]) { if (pos >= n || s[pos] != f[1]) return -1; pos++; f += 2; continue; }
        if (token_is(f, "YYYY")) { if (digits(s, n, &pos, 4, &y)) return -1; f += 4; seen_date = 1; }
        else if (token_is(f, "YY")) { if (digits(s, n, &pos, 2, &v)) return -1; y = v + (v > 68 ? 1900 : 2000); f += 2; seen_date = 1; }
        else if (token_is(f, "MMMM") || token_is(f, "MMM")) {
            int full = token_is(f, "MMMM"), k, found = 0;
            for (k = 0; k < 12 && !found; k++) {
                size_t len = full ? strlen(MONTHS[k]) : 3;
                if (pos + len <= n && !strncmp(s + pos, MONTHS[k], len)) { mo = k + 1; pos += len; found = 1; }
            }
            if (!found) return -1;
            f += full ? 4 : 3; seen_date = 1;
        }
        else if (token_is(f, "MM")) { if (digits(s, n, &pos, 2, &mo)) return -1; f += 2; seen_date = 1; }
        else if (*f == 'M') { int c = 0; v = 0; while (pos < n && s[pos] >= '0' && s[pos] <= '9' && c < 2) { v = v * 10 + (s[pos] - '0'); pos++; c++; } if (!c) return -1; mo = v; f += 1; seen_date = 1; }
        else if (token_is(f, "DDDD")) { if (digits(s, n, &pos, 3, &yday)) return -1; f += 4; seen_date = 1; }
        else if (token_is(f, "DDD")) { int c = 0; v = 0; while (pos < n && s[pos] >= '0' && s[pos] <= '9' && c < 3) { v = v * 10 + (s[pos] - '0'); pos++; c++; } if (!c) return -1; yday = v; f += 3; seen_date = 1; }
        else if (token_is(f, "DD")) { if (digits(s, n, &pos, 2, &d)) return -1; f += 2; seen_date = 1; }
        else if (f[0] == 'D' && f[1] == 'o') {                                                      /* 1st, 2nd, 23rd, 30th */
            int c = 0; v = 0;
            while (pos < n && s[pos] >= '0' && s[pos] <= '9' && c < 2) { v = v * 10 + (s[pos] - '0'); pos++; c++; }
            if (!c || pos + 2 > n || !(!strncmp(s + pos, "st", 2) || !strncmp(s + pos, "nd", 2) || !strncmp(s + pos, "rd", 2) || !strncmp(s + pos, "th", 2))) return -1;
            pos += 2; d = v; f += 2; seen_date = 1;
        }
        else if (*f == 'D') { int c = 0; v = 0; while (pos < n && s[pos] >= '0' && s[pos] <= '9' && c < 2) { v = v * 10 + (s[pos] - '0'); pos++; c++; } if (!c) return -1; d = v; f += 1; seen_date = 1; }
        else if (token_is(f, "dddd") || token_is(f, "ddd")) {                                     /* a weekday name: read and not used */
            int full = token_is(f, "dddd"), k, found = 0;
            for (k = 0; k < 7 && !found; k++) { size_t len = full ? strlen(DAYS[k]) : 3; if (pos + len <= n && !strncmp(s + pos, DAYS[k], len)) { pos += len; found = 1; } }
            if (!found) return -1;
            f += full ? 4 : 3;
        }
        else if (token_is(f, "HH")) { if (digits(s, n, &pos, 2, &h)) return -1; f += 2; }
        else if (*f == 'H') { int c = 0; v = 0; while (pos < n && s[pos] >= '0' && s[pos] <= '9' && c < 2) { v = v * 10 + (s[pos] - '0'); pos++; c++; } if (!c) return -1; h = v; f += 1; }
        else if (token_is(f, "hh")) { if (digits(s, n, &pos, 2, &h12)) return -1; f += 2; }
        else if (*f == 'h') { int c = 0; v = 0; while (pos < n && s[pos] >= '0' && s[pos] <= '9' && c < 2) { v = v * 10 + (s[pos] - '0'); pos++; c++; } if (!c) return -1; h12 = v; f += 1; }
        else if (token_is(f, "mm")) { if (digits(s, n, &pos, 2, &mi)) return -1; f += 2; }
        else if (token_is(f, "ss")) { if (digits(s, n, &pos, 2, &sec)) return -1; f += 2; }
        else if (*f == 'S') { int k = 0, c = 0, val = 0; while (f[k] == 'S') k++; while (c < k && pos < n && s[pos] >= '0' && s[pos] <= '9') { if (c < 3) val = val * 10 + (s[pos] - '0'); pos++; c++; } if (c != k) return -1; { int dg = c < 3 ? c : 3; while (dg++ < 3) val *= 10; } frac = val; f += k; }
        else if (*f == 'A' || *f == 'a') {
            if (pos + 2 > n) return -1;
            if (!strncasecmp_ascii(s + pos, "AM", 2)) pm = 0; else if (!strncasecmp_ascii(s + pos, "PM", 2)) pm = 1; else return -1;
            pos += 2; f += 1;
        }
        else {                                                                                      /* a separator or any other character: it has to be there */
            if (pos >= n || s[pos] != *f) return -1;
            pos++; f++;
        }
    }
    if (pos != n) return -1;
    if (h12 >= 0) { if (h12 < 1 || h12 > 12) return -1; h = h12 % 12 + (pm == 1 ? 12 : 0); }
    if (mo < 1 || mo > 12 || d < 1 || d > ex_days_in_month(y, mo) || h > 23 || mi > 59 || sec > 59) return -1;
    if (yday > 0 && seen_date) { if (yday > (leap(y) ? 366 : 365)) return -1; *ms = ex_date_make(y, 1, yday, h, mi, sec, frac, tz_min); return 0; }
    *ms = ex_date_make(y, mo, d, h, mi, sec, frac, tz_min);
    return 0;
}
