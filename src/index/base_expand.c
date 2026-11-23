/* A note's bases as Markdown (sub-slice 6b-ii). */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_base_index.h"
#include "exhaust_links.h"
#include "store_internal.h"

typedef struct Sb { char *p; size_t n, cap; } Sb;
static void sb_put(Sb *s, const char *t, size_t n) {
    if (s->n + n + 1 > s->cap) { size_t nc = s->cap ? s->cap * 2 : 1024; char *np; while (nc < s->n + n + 1) nc *= 2; np = realloc(s->p, nc); if (!np) return; s->p = np; s->cap = nc; }
    memcpy(s->p + s->n, t, n); s->n += n; s->p[s->n] = 0;
}
static void sb_str(Sb *s, const char *t) { sb_put(s, t, strlen(t)); }

/* One view of a base as Markdown. */
static void view_markdown(Sb *o, ExIndex *r, ExBaseData *data, const ExBase *b, size_t view, const char *this_path, int tz_min, double now_ms) {
    ExBaseResult res;
    char *md = NULL;
    size_t i, j;
    if (ex_base_run(b, view, data, this_path, now_ms, tz_min, &res) != 0) { sb_str(o, "*This base could not be run.*\n"); return; }
    (void)r;
    if (!res.nrows) sb_str(o, "*No results.*\n");
    else if (!strcmp(ex_base_view_type(b, view), "list")) {
        for (i = 0; i < res.nrows; i++) {
            sb_str(o, "- ");
            for (j = 0; j < res.ncols; j++) { if (j) sb_str(o, ", "); sb_str(o, ex_base_cell_text(&res, i, j)); }
            sb_str(o, "\n");
        }
    } else { md = ex_base_result_markdown(&res, NULL); if (md) sb_str(o, md); }
    free(md);
    for (i = 0; i < res.nerrors; i++) { sb_str(o, "\n*"); sb_str(o, res.errors[i]); sb_str(o, "*\n"); }
    ex_base_result_free(&res);
}

static void embed_view(Sb *o, ExIndex *r, ExBaseData *data, const char *text, size_t len, const char *view_name, const char *this_path, int tz_min, double now_ms) {
    char err[200];
    ExBase *b = ex_base_parse(text, len, err, sizeof err);
    int view = 0;
    if (!b) { sb_str(o, "> [!failure] This base could not be read\n> "); sb_str(o, err); sb_str(o, "\n"); return; }
    if (view_name && *view_name) { view = ex_base_find_view(b, view_name); if (view < 0) { sb_str(o, "*This base has no view called "); sb_str(o, view_name); sb_str(o, ".*\n"); ex_base_free(b); return; } }
    if (!ex_base_view_count(b)) sb_str(o, "*This base has no views.*\n");
    else view_markdown(o, r, data, b, (size_t)view, this_path, tz_min, now_ms);
    ex_base_free(b);
}

static int ends_base(const char *s, size_t n) { return n > 5 && !strncmp(s + n - 5, ".base", 5); }

char *ex_base_expand(ExIndex *r, const char *root, const char *this_path, const char *text, size_t len, int tz_min, double now_ms, size_t *out_len) {
    Sb o = {0};
    size_t pos = 0;
    int changed = 0;
    ExBaseData data;
    int loaded = 0;
    while (pos < len) {
        size_t eol = pos, next;
        const char *line = text + pos;
        size_t ll;
        while (eol < len && text[eol] != '\n') eol++;
        next = eol < len ? eol + 1 : eol;
        ll = eol - pos;
        /* a fenced block with the language base */
        if ((ll >= 7 && (!strncmp(line, "```base", 7) || !strncmp(line, "~~~base", 7))) && (ll == 7 || isspace((unsigned char)line[7]))) {
            char fence = line[0];
            size_t body = next, p2 = next, end = len, after = len;
            while (p2 < len) {
                size_t e2 = p2;
                while (e2 < len && text[e2] != '\n') e2++;
                if (e2 - p2 >= 3 && text[p2] == fence && text[p2 + 1] == fence && text[p2 + 2] == fence) { end = p2; after = e2 < len ? e2 + 1 : e2; break; }
                p2 = e2 < len ? e2 + 1 : e2;
            }
            if (!loaded) { if (ex_base_data_load(r, &data) != 0) { free(o.p); return NULL; } loaded = 1; }
            embed_view(&o, r, &data, text + body, end - body, NULL, this_path, tz_min, now_ms);
            changed = 1;
            pos = after;
            continue;
        }
        /* a line that is only an embed of a .base file */
        {
            size_t a = 0, z = ll;
            while (a < z && isspace((unsigned char)line[a])) a++;
            while (z > a && isspace((unsigned char)line[z - 1])) z--;
            if (z - a > 6 && line[a] == '!' && line[a + 1] == '[' && line[a + 2] == '[' && line[z - 1] == ']' && line[z - 2] == ']') {
                char target[600], view[300] = "", *hash, *bar;
                size_t tl = z - a - 5;
                if (tl < sizeof target) {
                    memcpy(target, line + a + 3, tl); target[tl] = 0;
                    if ((bar = strchr(target, '|'))) *bar = 0;
                    if ((hash = strchr(target, '#'))) { snprintf(view, sizeof view, "%s", hash + 1); *hash = 0; }
                    if (ends_base(target, strlen(target))) {
                        int64_t id = ex_link_resolve(r, this_path ? this_path : "", target, EX_LINK_EMBED);
                        char *path = id ? ex_index_file_path(r, id) : NULL;
                        size_t flen = 0;
                        char *ftext = path ? ex_index_read_note(root, path, &flen) : NULL;
                        if (!loaded && ftext) { if (ex_base_data_load(r, &data) != 0) { free(ftext); free(path); free(o.p); return NULL; } loaded = 1; }
                        if (ftext) embed_view(&o, r, &data, ftext, flen, view, this_path, tz_min, now_ms);
                        else { sb_str(&o, "*The base "); sb_str(&o, target); sb_str(&o, " was not found.*\n"); }
                        free(ftext); free(path);
                        changed = 1;
                        pos = next;
                        continue;
                    }
                }
            }
        }
        sb_put(&o, text + pos, next - pos);
        pos = next;
    }
    if (loaded) ex_base_data_free(&data);
    if (!changed) { free(o.p); return NULL; }
    if (out_len) *out_len = o.n;
    return o.p ? o.p : ex_strdup("");
}
