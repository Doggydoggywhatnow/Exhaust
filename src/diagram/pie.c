/* Mermaid pie charts (6d-iii): `pie`, optional `showData` and `title`, then `"label" : value` lines. Slices run clockwise from the top, largest first,
 * in the callout colors; percentages inside the slices, a legend on the right (with the values when showData is set). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diagram_internal.h"

typedef struct Slice { char *label; double v; } Slice;

int dg_pie(ExDiagram *d, DgLines *L, const char *first, size_t flen) {
    Slice *s = NULL;
    int ns = 0, cap = 0, show = 0, i, j;
    char *title = NULL;
    const char *line;
    size_t ln, p = 0;
    double total = 0;
    /* the type line: pie [showData] [title text] */
    while (p < flen && first[p] != ' ') p++;
    while (p < flen) {
        while (p < flen && first[p] == ' ') p++;
        if (flen - p >= 8 && !strncmp(first + p, "showData", 8)) { show = 1; p += 8; }
        else if (flen - p >= 5 && !strncmp(first + p, "title", 5)) { free(title); title = dg_clean_label(first + p + 5, flen - p - 5); p = flen; }
        else if (p < flen) { dg_fail(d, "unexpected text on the pie line: %.*s", (int)(flen - p > 20 ? 20 : flen - p), first + p); goto done; }
    }
    while (!d->has_err && dg_next_line(L, &line, &ln)) {
        size_t q = 0;
        while (q < ln && (line[q] == ' ' || line[q] == '\t')) q++;
        if (ln - q >= 8 && !strncmp(line + q, "showData", 8)) { show = 1; continue; }
        if (ln - q >= 5 && !strncmp(line + q, "title", 5) && (ln - q == 5 || line[q + 5] == ' ')) { free(title); title = dg_clean_label(line + q + 5, ln - q - 5); continue; }
        if (ln - q >= 8 && (!strncmp(line + q, "accTitle", 8) || !strncmp(line + q, "accDescr", 8))) continue;
        if (q < ln && line[q] == '"') {
            size_t e = q + 1;
            double v;
            const char *num;
            char *lab;
            while (e < ln && line[e] != '"') e++;
            if (e >= ln) { dg_fail(d, "a slice name is not closed"); goto done; }
            lab = dg_strndup(line + q + 1, e - q - 1);
            e++;
            while (e < ln && line[e] == ' ') e++;
            if (e >= ln || line[e] != ':') { dg_fail(d, "a slice needs a colon and a value: %.*s", (int)(ln > 30 ? 30 : ln), line); free(lab); goto done; }
            {
                char *tmp = dg_strndup(line + e + 1, ln - e - 1);
                num = tmp ? dg_parse_number(tmp, &v) : NULL;
                if (!num) { dg_fail(d, "a slice needs a number: %.*s", (int)(ln > 30 ? 30 : ln), line); free(tmp); free(lab); goto done; }
                while (*num == ' ') num++;
                if (*num) { dg_fail(d, "unexpected text after the value: %s", num); free(tmp); free(lab); goto done; }
                free(tmp);
            }
            if (v < 0) { dg_fail(d, "a slice cannot be negative"); free(lab); goto done; }
            if (ns >= 100) { dg_fail(d, "more than 100 slices"); free(lab); goto done; }
            if (ns == cap) { int nc = cap ? cap * 2 : 8; Slice *t = realloc(s, (size_t)nc * sizeof *t); if (!t) { dg_fail(d, "out of memory"); free(lab); goto done; } s = t; cap = nc; }
            s[ns].label = dg_clean_label(lab, strlen(lab)); s[ns].v = v; ns++;
            free(lab);
            continue;
        }
        dg_fail(d, "unknown line: %.*s", (int)(ln > 30 ? 30 : ln), line);
    }
    if (d->has_err) goto done;
    for (i = 0; i < ns; i++) total += s[i].v;
    if (ns == 0 || total <= 0) { dg_fail(d, "a pie chart needs slices with a total above zero"); goto done; }
    for (i = 1; i < ns; i++) {   /* largest first, stable */
        Slice t = s[i];
        j = i - 1;
        while (j >= 0 && s[j].v < t.v) { s[j + 1] = s[j]; j--; }
        s[j + 1] = t;
    }
    {
        const float R = 7.5f;
        float cx, cy, top = 0.0f, lx, ly, legend_w = 0;
        double a = 0;
        if (title) { float tw, th; dg_text_size(d, title, strlen(title), 1.2f, &tw, &th); top = th + 1.0f; }
        cx = R + 1.0f; cy = top + R + 1.0f;
        if (title) {
            float tw;
            dg_text_size(d, title, strlen(title), 1.2f, &tw, NULL);
            dg_label(d, cx, 0.9f, title, strlen(title), 0, 1.2f, EXD_C_TEXT, 1);
            (void)tw;
        }
        for (i = 0; i < ns; i++) {
            ExDItem *w = dg_add(d, EXD_WEDGE);
            double span = s[i].v / total * 6.283185307179586;
            if (!w) goto done;
            w->x = cx; w->y = cy; w->w = R; w->a0 = (float)a; w->a1 = (float)(a + span); w->fill = (uint8_t)(EXD_C_CAT + i % 13); w->stroke = EXD_C_LABEL_BG; w->width = 0.1f;
            if (s[i].v / total >= 0.03) {
                char pct[16];
                double mid = a + span / 2.0;
                snprintf(pct, sizeof pct, "%.0f%%", s[i].v / total * 100.0);
                { ExDItem *pt = dg_label(d, cx + (float)sin(mid) * R * 0.65f, cy - (float)cos(mid) * R * 0.65f, pct, strlen(pct), 0, 0.9f, EXD_C_BEST, 1); if (pt) pt->fill = (uint8_t)(EXD_C_CAT + i % 13); }
            }
            a += span;
        }
        lx = cx + R + 2.0f; ly = cy - ((float)ns * 1.7f) / 2.0f + 0.85f;
        for (i = 0; i < ns; i++) {
            char buf[256];
            float tw;
            if (show) snprintf(buf, sizeof buf, "%s [%g]", s[i].label, s[i].v); else snprintf(buf, sizeof buf, "%s", s[i].label);
            dg_box(d, EXD_RECT, lx, ly - 0.55f + (float)i * 1.7f, 1.1f, 1.1f, EXD_C_CAT + i % 13, EXD_C_CAT + i % 13);
            dg_label(d, lx + 1.7f, ly + (float)i * 1.7f, buf, strlen(buf), 1, 1.0f, EXD_C_TEXT, 0);
            dg_text_size(d, buf, strlen(buf), 1.0f, &tw, NULL);
            if (tw + 1.7f > legend_w) legend_w = tw + 1.7f;
        }
        (void)legend_w;
    }
    dg_finalize(d, 0.8f);
done:
    for (i = 0; i < ns; i++) free(s[i].label);
    free(s); free(title);
    return d->has_err ? -1 : 0;
}
