/* Mermaid gantt charts (6d-iii): `title`, `dateFormat YYYY-MM-DD` (the only format), `axisFormat` (the %Y %y %m %d %e %b %B %a %A %H %M %j codes), `section`,
 * and tasks `name : [done|active|crit|milestone,] [id,] [start,] end-or-duration` where the start is a date, `after id [id...]` or the end of the task before,
 * and the end is a date, a duration (30d 2w 6h 90m 1M 1y) or `until id`. Bars in the accent color (grey when done, red outline when critical), milestones
 * as diamonds, sections as bands, a date axis along the top. Excluded days (`excludes`) are read and ignored. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diagram_internal.h"

typedef struct Task { char *name, *id; int section; double start, end; int done, active, crit, milestone; } Task;
typedef struct GC { ExDiagram *d; Task *t; int nt, cap; char **sec; int nsec; char *title, *axis; double last_end; int has_last; } GC;

/* days since 1970-01-01 of a civil date, and back (Howard Hinnant's algorithms) */
static long days_from_civil(long y, unsigned m, unsigned d) {
    long era;
    unsigned yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (153 * (m + (m > 2 ? (unsigned)-3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}
static void civil_from_days(long z, long *y, unsigned *m, unsigned *d) {
    long era;
    unsigned doe, yoe, doy, mp;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (unsigned)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    *y = (long)yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y += *m <= 2;
}

static int parse_date(const char *s, size_t n, double *out) {
    int y, m, dd;
    char buf[16];
    if (n != 10 || s[4] != '-' || s[7] != '-') return 0;
    memcpy(buf, s, 10); buf[10] = 0;
    if (sscanf(buf, "%4d-%2d-%2d", &y, &m, &dd) != 3 || m < 1 || m > 12 || dd < 1 || dd > 31 || y < 1000 || y > 9999) return 0;
    *out = (double)days_from_civil(y, (unsigned)m, (unsigned)dd);
    return 1;
}

static int parse_duration(const char *s, size_t n, double *out) {
    char buf[32];
    char *e;
    double v;
    if (n < 2 || n >= sizeof buf) return 0;
    memcpy(buf, s, n); buf[n] = 0;
    v = strtod(buf, &e);
    if (e == buf || v < 0) return 0;
    if (!strcmp(e, "d")) *out = v;
    else if (!strcmp(e, "w")) *out = v * 7.0;
    else if (!strcmp(e, "h")) *out = v / 24.0;
    else if (!strcmp(e, "m")) *out = v / 1440.0;
    else if (!strcmp(e, "s")) *out = v / 86400.0;
    else if (!strcmp(e, "ms")) *out = v / 86400000.0;
    else if (!strcmp(e, "M")) *out = v * 30.0;
    else if (!strcmp(e, "y")) *out = v * 365.0;
    else return 0;
    return 1;
}

static Task *find_id(GC *c, const char *s, size_t n) {
    int i;
    for (i = 0; i < c->nt; i++) if (c->t[i].id && strlen(c->t[i].id) == n && !memcmp(c->t[i].id, s, n)) return &c->t[i];
    return NULL;
}

static void trim(const char **s, size_t *n) {
    while (*n && (**s == ' ' || **s == '\t')) { (*s)++; (*n)--; }
    while (*n && ((*s)[*n - 1] == ' ' || (*s)[*n - 1] == '\t')) (*n)--;
}

static void add_task(GC *c, const char *s, size_t n) {
    const char *colon = memchr(s, ':', n), *parts[8];
    size_t plen[8], np = 0, i, k;
    Task t;
    const char *meta;
    size_t mlen;
    int has_start = 0, has_end = 0;
    if (!colon) { dg_fail(c->d, "a task needs a colon: %.*s", (int)(n > 30 ? 30 : n), s); return; }
    memset(&t, 0, sizeof t);
    {
        const char *nm = s; size_t nl = (size_t)(colon - s);
        trim(&nm, &nl);
        t.name = dg_clean_label(nm, nl);
    }
    meta = colon + 1; mlen = n - (size_t)(colon + 1 - s);
    {   /* split the meta text at commas */
        size_t st = 0;
        for (i = 0; i <= mlen; i++) {
            if (i == mlen || meta[i] == ',') {
                if (np >= 8) { dg_fail(c->d, "too many parts in a task: %.*s", (int)(n > 30 ? 30 : n), s); free(t.name); return; }
                parts[np] = meta + st; plen[np] = i - st; trim(&parts[np], &plen[np]); np++;
                st = i + 1;
            }
        }
    }
    k = 0;
    for (; k < np; k++) {   /* tags */
        if (plen[k] == 4 && !strncmp(parts[k], "done", 4)) t.done = 1;
        else if (plen[k] == 6 && !strncmp(parts[k], "active", 6)) t.active = 1;
        else if (plen[k] == 4 && !strncmp(parts[k], "crit", 4)) t.crit = 1;
        else if (plen[k] == 9 && !strncmp(parts[k], "milestone", 9)) t.milestone = 1;
        else break;
    }
    {
        size_t left = np - k, z;
        double dur = 0, v;
        for (z = k; z < np; z++)   /* something that looks like a date must be one */
            if (plen[z] == 10 && parts[z][4] == '-' && parts[z][7] == '-' && !parse_date(parts[z], plen[z], &v)) { dg_fail(c->d, "not a valid date: %.10s", parts[z]); free(t.name); return; }
        size_t ei = np;   /* the end part */
        if (left == 0 || left > 3) { dg_fail(c->d, "a task needs a start or an end: %.*s", (int)(n > 40 ? 40 : n), s); free(t.name); return; }
        if (left == 3) { t.id = dg_strndup(parts[k], plen[k]); k++; }
        else if (left == 2 && !(parse_date(parts[k], plen[k], &v) || (plen[k] > 5 && !strncmp(parts[k], "after ", 6)))) { t.id = dg_strndup(parts[k], plen[k]); k++; }
        left = np - k;
        if (left == 2) {   /* start, end */
            const char *sp = parts[k]; size_t sl = plen[k];
            if (parse_date(sp, sl, &v)) { t.start = v; has_start = 1; }
            else if (sl > 6 && !strncmp(sp, "after ", 6)) {
                const char *q = sp + 6, *end = sp + sl;
                double best = -1e18;
                int any = 0;
                while (q < end) {
                    const char *e = q;
                    Task *ref;
                    while (q < end && *q == ' ') q++;
                    e = q;
                    while (e < end && *e != ' ') e++;
                    if (e == q) break;
                    ref = find_id(c, q, (size_t)(e - q));
                    if (!ref) { dg_fail(c->d, "unknown task after: %.*s", (int)(e - q), q); free(t.name); free(t.id); return; }
                    if (ref->end > best) best = ref->end;
                    any = 1;
                    q = e;
                }
                if (!any) { dg_fail(c->d, "after needs a task id"); free(t.name); free(t.id); return; }
                t.start = best; has_start = 1;
            } else { dg_fail(c->d, "unknown start: %.*s", (int)(sl > 20 ? 20 : sl), sp); free(t.name); free(t.id); return; }
            k++;
        }
        if (k < np) ei = k;
        if (ei >= np) { dg_fail(c->d, "a task needs an end or a duration: %.*s", (int)(n > 40 ? 40 : n), s); free(t.name); free(t.id); return; }
        if (!has_start) {
            if (!c->has_last) { dg_fail(c->d, "the first task needs a start date: %.*s", (int)(n > 40 ? 40 : n), s); free(t.name); free(t.id); return; }
            t.start = c->last_end;
        }
        if (parse_date(parts[ei], plen[ei], &v)) { t.end = v; has_end = 1; }
        else if (parse_duration(parts[ei], plen[ei], &dur)) { t.end = t.start + dur; has_end = 1; }
        else if (plen[ei] > 6 && !strncmp(parts[ei], "until ", 6)) {
            Task *ref = find_id(c, parts[ei] + 6, plen[ei] - 6);
            if (!ref) { dg_fail(c->d, "unknown task in until: %.*s", (int)(plen[ei] - 6), parts[ei] + 6); free(t.name); free(t.id); return; }
            t.end = ref->start; has_end = 1;
        }
        if (!has_end) { dg_fail(c->d, "unknown end or duration: %.*s", (int)(plen[ei] > 20 ? 20 : plen[ei]), parts[ei]); free(t.name); free(t.id); return; }
        if (t.end < t.start) { dg_fail(c->d, "a task ends before it starts: %s", t.name); free(t.name); free(t.id); return; }
    }
    if (t.milestone) t.end = t.start;
    t.section = c->nsec - 1;
    if (c->nt >= 300) { dg_fail(c->d, "more than 300 tasks"); free(t.name); free(t.id); return; }
    if (c->nt == c->cap) { int nc = c->cap ? c->cap * 2 : 16; Task *tt = realloc(c->t, (size_t)nc * sizeof *tt); if (!tt) { dg_fail(c->d, "out of memory"); free(t.name); free(t.id); return; } c->t = tt; c->cap = nc; }
    c->t[c->nt++] = t;
    c->last_end = t.end; c->has_last = 1;
}

static void format_date(const char *fmt, double day, char *out, size_t cap) {
    long y;
    unsigned m, d;
    static const char *const mon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    static const char *const monl[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
    static const char *const wd[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *const wdl[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
    long whole = (long)floor(day);
    double frac = day - (double)whole;
    int minutes = (int)(frac * 1440.0 + 0.5), dow = (int)(((whole % 7) + 11) % 7);   /* 1970-01-01 was a Thursday */
    size_t o = 0;
    civil_from_days(whole, &y, &m, &d);
    while (*fmt && o + 20 < cap) {
        if (*fmt == '%' && fmt[1]) {
            char tmp[32];
            fmt++;
            switch (*fmt) {
            case 'Y': snprintf(tmp, sizeof tmp, "%ld", y); break;
            case 'y': snprintf(tmp, sizeof tmp, "%02ld", y % 100); break;
            case 'm': snprintf(tmp, sizeof tmp, "%02u", m); break;
            case 'd': snprintf(tmp, sizeof tmp, "%02u", d); break;
            case 'e': snprintf(tmp, sizeof tmp, "%u", d); break;
            case 'b': snprintf(tmp, sizeof tmp, "%s", mon[m - 1]); break;
            case 'B': snprintf(tmp, sizeof tmp, "%s", monl[m - 1]); break;
            case 'a': snprintf(tmp, sizeof tmp, "%s", wd[dow]); break;
            case 'A': snprintf(tmp, sizeof tmp, "%s", wdl[dow]); break;
            case 'H': snprintf(tmp, sizeof tmp, "%02d", (minutes / 60) % 24); break;
            case 'M': snprintf(tmp, sizeof tmp, "%02d", minutes % 60); break;
            case 'j': snprintf(tmp, sizeof tmp, "%ld", whole - days_from_civil(y, 1, 1) + 1); break;
            case '%': snprintf(tmp, sizeof tmp, "%%"); break;
            default: snprintf(tmp, sizeof tmp, "%%%c", *fmt); break;
            }
            memcpy(out + o, tmp, strlen(tmp)); o += strlen(tmp);
            fmt++;
        } else out[o++] = *fmt++;
    }
    out[o] = 0;
}

int dg_gantt(ExDiagram *d, DgLines *L) {
    GC c;
    const char *line;
    size_t ln;
    int i, s;
    memset(&c, 0, sizeof c);
    c.d = d;
    while (!d->has_err && dg_next_line(L, &line, &ln)) {
        size_t q = 0;
        while (q < ln && (line[q] == ' ' || line[q] == '\t')) q++;
        line += q; ln -= q;
#define KW(w) (ln >= strlen(w) && !strncmp(line, w, strlen(w)) && (ln == strlen(w) || line[strlen(w)] == ' ' || line[strlen(w)] == '\t'))
        if (KW("title")) { free(c.title); c.title = dg_clean_label(line + 5, ln - 5); }
        else if (KW("dateFormat")) {
            const char *a = line + 10; size_t al = ln - 10;
            trim(&a, &al);
            if (!(al == 10 && !strncmp(a, "YYYY-MM-DD", 10))) { dg_fail(d, "dateFormat %.*s is not supported (only YYYY-MM-DD)", (int)(al > 20 ? 20 : al), a); break; }
        }
        else if (KW("axisFormat")) { const char *a = line + 10; size_t al = ln - 10; trim(&a, &al); free(c.axis); c.axis = dg_strndup(a, al); }
        else if (KW("section")) {
            char **t = realloc(c.sec, (size_t)(c.nsec + 1) * sizeof *t);
            if (!t) { dg_fail(d, "out of memory"); break; }
            c.sec = t;
            c.sec[c.nsec++] = dg_clean_label(line + 7, ln - 7);
        }
        else if (KW("excludes") || KW("includes") || KW("todayMarker") || KW("tickInterval") || KW("weekday") || KW("inclusiveEndDates") || KW("topAxis") || KW("accTitle") || KW("accDescr") || KW("click")) {}
        else {
            if (c.nsec == 0) { char **t = malloc(sizeof *t); if (!t) { dg_fail(d, "out of memory"); break; } c.sec = t; c.sec[0] = dg_strndup("", 0); c.nsec = 1; }
            add_task(&c, line, ln);
        }
#undef KW
    }
    if (d->has_err) goto done;
    if (c.nt == 0) { dg_fail(d, "a gantt chart needs tasks"); goto done; }
    {
        double t0 = 1e18, t1 = -1e18, span, step = 1.0;
        float W = 44.0f, left = 0, top, rowh = 1.9f, scale, y;
        static const double steps[] = { 1, 2, 5, 7, 14, 30, 60, 91, 182, 365, 730, 1825, 3650 };
        size_t k;
        const char *fmt = c.axis && *c.axis ? c.axis : "%Y-%m-%d";
        char lab[64];
        float tick_y;
        for (i = 0; i < c.nt; i++) { if (c.t[i].start < t0) t0 = c.t[i].start; if (c.t[i].end > t1) t1 = c.t[i].end; }
        span = t1 - t0;
        if (span < 1.0) span = 1.0;
        scale = W / (float)span;
        for (s = 0; s < c.nsec; s++) { float tw; if (!*c.sec[s]) continue; dg_text_size(d, c.sec[s], strlen(c.sec[s]), 1.0f, &tw, NULL); if (tw + 1.5f > left) left = tw + 1.5f; }
        for (k = 0; k < sizeof steps / sizeof steps[0]; k++) { step = steps[k]; if (span / step <= 11.0) break; }
        top = c.title ? 3.2f : 0.5f;
        tick_y = top + 0.6f;
        if (c.title) dg_label(d, left + W / 2.0f, 0.9f, c.title, strlen(c.title), 0, 1.2f, EXD_C_TEXT, 1);
        y = top + 1.8f;
        {   /* section bands, rows, bars */
            int row = 0;
            float total_h = 0;
            for (s = 0; s < c.nsec; s++) {
                int first = -1, last = -1;
                for (i = 0; i < c.nt; i++) if (c.t[i].section == s) { if (first < 0) first = i; last = i; }
                if (first < 0) continue;
                {
                    int rows = 0;
                    for (i = first; i <= last; i++) if (c.t[i].section == s) rows++;
                    if ((s % 2) == 0) dg_box(d, EXD_RECT, 0, y + (float)row * rowh, left + W + 1.0f, (float)rows * rowh, EXD_C_PANEL, 0);
                    if (*c.sec[s]) dg_label(d, 0.6f, y + ((float)row + (float)rows / 2.0f) * rowh, c.sec[s], strlen(c.sec[s]), 1, 1.0f, EXD_C_TEXT, 1);
                    row += rows;
                }
            }
            total_h = (float)row * rowh;
            /* ticks and their grid lines */
            {
                double day = floor(t0 / step) * step;
                int guard = 0;
                for (; day <= t1 + 1e-9 && guard < 60; day += step, guard++) {
                    float x;
                    if (day < t0 - 1e-9) continue;
                    x = left + (float)(day - t0) * scale;
                    {
                        float ln2[4]; ln2[0] = x; ln2[1] = tick_y + 0.6f; ln2[2] = x; ln2[3] = y + total_h;
                        dg_line(d, ln2, 2, EXD_C_NODE_STROKE, 2, 0, 0);
                    }
                    format_date(fmt, day, lab, sizeof lab);
                    dg_label(d, x, tick_y, lab, strlen(lab), 0, 0.85f, EXD_C_MUTED, 0);
                }
            }
            row = 0;
            for (s = 0; s < c.nsec; s++) {
                for (i = 0; i < c.nt; i++) {
                    float x0, x1, by, bw, tw;
                    const Task *t = &c.t[i];
                    if (t->section != s) continue;
                    x0 = left + (float)(t->start - t0) * scale; x1 = left + (float)(t->end - t0) * scale;
                    by = y + (float)row * rowh + 0.35f;
                    bw = x1 - x0;
                    dg_text_size(d, t->name, strlen(t->name), 0.95f, &tw, NULL);
                    if (t->milestone) {
                        dg_box(d, EXD_DIAMOND, x0 - 0.6f, by - 0.0f, 1.2f, 1.2f, t->crit ? EXD_C_ERROR : EXD_C_ACCENT, t->crit ? EXD_C_ERROR : EXD_C_NODE_STROKE);
                        dg_label(d, x0 + 1.0f, by + 0.6f, t->name, strlen(t->name), 1, 0.95f, EXD_C_TEXT, 0);
                    } else {
                        int fill = t->done ? EXD_C_NODE_FILL : (t->active ? EXD_C_ACCENT : EXD_C_ACCENT_FILL);
                        if (t->crit && !t->done) fill = t->active ? EXD_C_ERROR : EXD_C_NODE_FILL;
                        if (bw < 0.2f) bw = 0.2f;
                        dg_box(d, EXD_ROUND, x0, by, bw, 1.2f, fill, t->crit ? EXD_C_ERROR : EXD_C_NODE_STROKE);
                        if (tw + 0.8f <= bw) { int solid = fill == EXD_C_ACCENT || fill == EXD_C_ERROR; ExDItem *lt = dg_label(d, x0 + bw / 2.0f, by + 0.6f, t->name, strlen(t->name), 0, 0.95f, solid ? EXD_C_BEST : EXD_C_TEXT, 0); if (lt && solid) lt->fill = (uint8_t)fill; }
                        else dg_label(d, x0 + bw + 0.5f, by + 0.6f, t->name, strlen(t->name), 1, 0.95f, EXD_C_TEXT, 0);
                    }
                    row++;
                }
            }
        }
    }
    dg_finalize(d, 0.8f);
done:
    for (i = 0; i < c.nt; i++) { free(c.t[i].name); free(c.t[i].id); }
    for (s = 0; s < c.nsec; s++) free(c.sec[s]);
    free(c.t); free(c.sec); free(c.title); free(c.axis);
    return d->has_err ? -1 : 0;
}
