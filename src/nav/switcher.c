#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_fuzzy.h"
#include "exhaust_nav.h"
#include "exhaust_vault.h"
#include "sqlite3.h"
#include "../index/index_internal.h"

typedef struct Entry { char *path, *label; int via_alias, is_note, is_path; } Entry;
struct ExSwitcher { Entry *v; size_t n, cap; char **paths; size_t npaths; };

static int add(ExSwitcher *s, const char *path, const char *label, int via_alias, int is_note, int is_path) {
    Entry *e;
    if (s->n == s->cap) { size_t nc = s->cap ? s->cap * 2 : 256; Entry *nv = realloc(s->v, nc * sizeof *nv); if (!nv) return -1; s->v = nv; s->cap = nc; }
    e = &s->v[s->n++];
    e->path = ex_strdup(path); e->label = ex_strdup(label); e->via_alias = via_alias; e->is_note = is_note; e->is_path = is_path;
    return e->path && e->label ? 0 : -1;
}

ExSwitcher *ex_switcher_load(ExIndex *r, int include_attachments) {
    ExSwitcher *s = calloc(1, sizeof *s);
    sqlite3_stmt *st;
    if (!s) return NULL;
    st = ex_db_prepare(r, "SELECT f.path, f.kind, n.kind, n.text FROM names n JOIN files f ON f.id = n.file_id WHERE n.kind IN (0, 1, 2) ORDER BY f.path, n.kind");
    if (!st) { free(s); return NULL; }
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *path = (const char *)sqlite3_column_text(st, 0), *text = (const char *)sqlite3_column_text(st, 3);
        int kind = sqlite3_column_int(st, 1), nk = sqlite3_column_int(st, 2), note = kind == EX_KIND_MARKDOWN;
        if (!note && !include_attachments) continue;
        if (nk == 1 && !strchr(path, '/')) continue;              /* the path of a note in the top folder is its name */
        if (add(s, path, text, nk == 2, note, nk == 1) != 0) break;
    }
    sqlite3_finalize(st);
    return s;
}

void ex_switcher_free(ExSwitcher *s) {
    size_t i;
    if (!s) return;
    for (i = 0; i < s->n; i++) { free(s->v[i].path); free(s->v[i].label); }
    free(s->v); free(s);
}

size_t ex_switcher_size(const ExSwitcher *s) { return s->n; }

static int cmp_item(const void *a, const void *b) {
    const ExSwitchItem *x = a, *y = b;
    if (x->score != y->score) return y->score - x->score;
    if (x->via_alias != y->via_alias) return x->via_alias - y->via_alias;
    return strcmp(x->label, y->label);
}

size_t ex_switcher_query(const ExSwitcher *s, const char *query, const char *const *recent, size_t nrecent, size_t max, ExSwitchItem *out) {
    size_t i, n = 0, k;
    ExSwitchItem *all;
    if (!max) return 0;
    if (!query[0]) {                                              /* the recent notes that are still there */
        for (k = 0; k < nrecent && n < max; k++) {
            for (i = 0; i < s->n; i++) if (!s->v[i].via_alias && !s->v[i].is_path && !strcmp(s->v[i].path, recent[k])) {
                out[n].path = s->v[i].path; out[n].label = s->v[i].label; out[n].via_alias = 0; out[n].is_note = s->v[i].is_note; out[n].score = (int)(nrecent - k);
                n++;
                break;
            }
        }
        return n;
    }
    all = malloc((s->n + 1) * sizeof *all);
    if (!all) return 0;
    for (i = 0; i < s->n; i++) {
        int sc = ex_fuzzy_score(query, s->v[i].label);
        if (sc < 0) continue;
        if (s->v[i].is_path) sc = sc > 30 ? sc - 30 : 0;           /* a match on the folders counts for less than one on the name */
        all[n].path = s->v[i].path; all[n].label = s->v[i].label; all[n].via_alias = s->v[i].via_alias; all[n].is_note = s->v[i].is_note; all[n].score = sc;
        n++;
    }
    /* a note found by its name and by its path is listed once, with the better score */
    qsort(all, n, sizeof *all, cmp_item);
    {
        size_t o = 0;
        for (i = 0; i < n && o < max; i++) {
            int dup = 0;
            for (k = 0; k < o; k++) if (!strcmp(out[k].path, all[i].path) && out[k].via_alias == all[i].via_alias && (!all[i].via_alias || !strcmp(out[k].label, all[i].label))) { dup = 1; break; }
            if (!dup) out[o++] = all[i];
        }
        n = o;
    }
    free(all);
    return n;
}

int ex_switcher_enter(const ExSwitcher *s, const char *query, const ExSwitchItem *selected, int show_existing_only, ExSwitchAction *out) {
    char name[500];
    size_t n, i;
    char *seg;
    (void)s;
    memset(out, 0, sizeof *out);
    if (selected) { out->create = 0; snprintf(out->path, sizeof out->path, "%s", selected->path); return 0; }
    if (show_existing_only) return -1;
    while (*query == ' ') query++;
    snprintf(name, sizeof name, "%s", query);
    n = strlen(name);
    while (n && name[n - 1] == ' ') name[--n] = 0;
    if (n > 3 && !strcmp(name + n - 3, ".md")) name[n -= 3] = 0;
    if (!n || name[0] == '/' || name[n - 1] == '/') return -1;
    {
        char copy[512];
        snprintf(copy, sizeof copy, "%s", name);
        for (seg = copy; seg; ) {
            char *slash = strchr(seg, '/');
            if (slash) *slash = 0;
            if (!ex_name_valid(seg)) return -1;
            seg = slash ? slash + 1 : NULL;
        }
    }
    for (i = 0; name[i]; i++) if (name[i] == '\\') return -1;
    out->create = 1;
    snprintf(out->path, sizeof out->path, "%s.md", name);
    return 0;
}

/* ---- the command palette ---- */

static int pin_index(const char *id, const char *const *pinned, size_t n) { size_t i; for (i = 0; i < n; i++) if (!strcmp(pinned[i], id)) return (int)i; return -1; }

static int cmp_palette(const void *a, const void *b) {
    const ExPaletteItem *x = a, *y = b;
    return y->score - x->score;
}

size_t ex_palette_query(const ExCommands *c, const char *query, const char *const *pinned, size_t npinned, ExPaletteItem **out) {
    size_t total = ex_commands_count(c), i, n = 0;
    ExPaletteItem *v = malloc((total + 1) * sizeof *v);
    *out = NULL;
    if (!v) return 0;
    for (i = 0; i < total; i++) {
        const ExCommandDef *d = ex_commands_at(c, i);
        int pin = pin_index(d->id, pinned, npinned), sc = query[0] ? ex_fuzzy_score(query, d->name) : 0;
        if (sc < 0) continue;
        v[n].id = d->id; v[n].name = d->name; v[n].keys = d->default_keys;
        v[n].available = ex_commands_available(c, d->id);
        v[n].pinned = pin >= 0;
        /* pinned commands first; among them the order of the setting. Unavailable commands go below available ones. */
        v[n].score = sc + (pin >= 0 ? 1000 - pin : 0);
        n++;
    }
    {   /* a stable sort: ties stay in the order of the table */
        size_t k;
        for (i = 1; i < n; i++) {
            ExPaletteItem it = v[i];
            for (k = i; k > 0 && cmp_palette(&v[k - 1], &it) > 0; k--) v[k] = v[k - 1];
            v[k] = it;
        }
    }
    *out = v;
    return n;
}
