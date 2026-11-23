/* Embedded notes: see include/exhaust_embed.h. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_embed.h"
#include "exhaust_links.h"

typedef struct Sb { char *p; size_t n, cap; } Sb;
static void sb_put(Sb *s, const char *t, size_t n) {
    if (s->n + n + 1 > s->cap) { size_t nc = s->cap ? s->cap * 2 : 1024; char *np; while (nc < s->n + n + 1) nc *= 2; np = realloc(s->p, nc); if (!np) return; s->p = np; s->cap = nc; }
    memcpy(s->p + s->n, t, n); s->n += n; s->p[s->n] = 0;
}

/* `name` is a note when its last part has no dot extension, or the extension is .md */
static int is_note_name(const char *name) {
    const char *slash = strrchr(name, '/'), *dot;
    slash = slash ? slash + 1 : name;
    dot = strrchr(slash, '.');
    if (!dot) return 1;
    return (dot[1] == 'm' || dot[1] == 'M') && (dot[2] == 'd' || dot[2] == 'D') && !dot[3];
}

/* the whole line is `![[target]]` (spaces around it allowed): the target text, else NULL */
static int embed_line(const char *l, size_t n, char *target, size_t cap) {
    size_t a = 0, b = n;
    while (a < b && (l[a] == ' ' || l[a] == '\t')) a++;
    while (b > a && (l[b - 1] == ' ' || l[b - 1] == '\t' || l[b - 1] == '\r')) b--;
    if (b - a < 6 || l[a] != '!' || l[a + 1] != '[' || l[a + 2] != '[' || l[b - 1] != ']' || l[b - 2] != ']') return 0;
    if (memchr(l + a + 3, ']', b - a - 5) || memchr(l + a + 3, '[', b - a - 5)) return 0;
    snprintf(target, cap, "%.*s", (int)(b - a - 5), l + a + 3);
    return 1;
}

char *ex_embed_expand(ExIndex *r, const char *root, const char *this_path, const char *text, size_t len, size_t *out_len) {
    Sb o = { NULL, 0, 0 };
    size_t i = 0;
    int fence = 0, expanded = 0;
    while (i < len) {
        size_t e = i;
        char target[600];
        int done = 0;
        while (e < len && text[e] != '\n') e++;
        {
            size_t a = i;
            while (a < e && (text[a] == ' ' || text[a] == '\t')) a++;
            if (e - a >= 3 && (!strncmp(text + a, "```", 3) || !strncmp(text + a, "~~~", 3))) fence = !fence;
        }
        if (!fence && expanded < 8 && embed_line(text + i, e - i, target, sizeof target)) {
            char name[400], frag[200], *bar;
            const char *hash = strchr(target, '#');
            int64_t id;
            snprintf(name, sizeof name, "%.*s", (int)(hash ? (size_t)(hash - target) : strlen(target)), target);
            bar = strchr(name, '|');
            if (bar) *bar = 0;
            frag[0] = 0;
            if (hash) { snprintf(frag, sizeof frag, "%s", hash + 1); bar = strchr(frag, '|'); if (bar) *bar = 0; }
            if (name[0] && is_note_name(name)) {
                id = ex_link_resolve(r, this_path ? this_path : "", name, EX_LINK_WIKI);
                if (id) {
                    char *path = ex_index_file_path(r, id), *body = NULL;
                    if (path) {
                        body = ex_link_preview(r, root, path, frag[0] && frag[0] != '^' ? frag : NULL, frag[0] == '^' ? frag + 1 : NULL, 3000);
                        if (body && *body) {
                            const char *p = body;
                            sb_put(&o, "> *", 3); sb_put(&o, name, strlen(name)); if (frag[0]) { sb_put(&o, " > ", 3); sb_put(&o, frag[0] == '^' ? frag + 1 : frag, strlen(frag[0] == '^' ? frag + 1 : frag)); } sb_put(&o, "*\n>\n", 4);
                            while (*p) {
                                const char *nl = strchr(p, '\n');
                                size_t n = nl ? (size_t)(nl - p) : strlen(p);
                                sb_put(&o, "> ", 2); sb_put(&o, p, n); sb_put(&o, "\n", 1);
                                if (!nl) break;
                                p = nl + 1;
                            }
                            done = 1; expanded++;
                        }
                    }
                    free(body); free(path);
                }
            }
        }
        if (!done) { sb_put(&o, text + i, e - i); if (e < len) sb_put(&o, "\n", 1); }
        i = e < len ? e + 1 : e;
    }
    if (!expanded) { free(o.p); return NULL; }
    if (out_len) *out_len = o.n;
    return o.p;
}
