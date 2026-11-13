/* Editing properties as text: only the lines of the property that changes are replaced. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_notes.h"
#include "exhaust_platform.h"
#include "exhaust_props.h"
#include "extract_internal.h"
#include "store_internal.h"

typedef struct B { char *v; size_t n, cap; int oom; } B;
static void badd(B *b, const char *s, size_t n) {
    if (b->oom) return;
    if (b->n + n + 1 > b->cap) { size_t nc = (b->n + n + 1) * 2; char *nv = realloc(b->v, nc); if (!nv) { b->oom = 1; return; } b->v = nv; b->cap = nc; }
    memcpy(b->v + b->n, s, n);
    b->n += n;
    b->v[b->n] = 0;
}
static void bstr(B *b, const char *s) { badd(b, s, strlen(s)); }

static int plain_word(const char *s) {
    return !strcmp(s, "true") || !strcmp(s, "false") || !strcmp(s, "null") || !strcmp(s, "yes") || !strcmp(s, "no") || !strcmp(s, "on") || !strcmp(s, "off") || !strcmp(s, "~");
}

/* Does a text need quotes to stay text in YAML? */
static int needs_quote(const char *s) {
    size_t n = strlen(s), i;
    if (!n) return 1;
    if (s[0] == ' ' || s[n - 1] == ' ' || strchr("-?:,[]{}#&*!|>'\"%@`", s[0])) return 1;
    if (plain_word(s) || ex_prop_type_of_text(s, 0) != EX_PROP_TEXT) return 1;
    for (i = 0; i < n; i++) {
        if (s[i] == '\n' || s[i] == '\t' || s[i] == '\\') return 1;
        if (s[i] == ':' && (i + 1 == n || s[i + 1] == ' ')) return 1;
        if (s[i] == '#' && i > 0 && s[i - 1] == ' ') return 1;
    }
    return 0;
}

static void put_quoted(B *b, const char *s) {
    bstr(b, "\"");
    for (; *s; s++) {
        if (*s == '"') bstr(b, "\\\"");
        else if (*s == '\\') bstr(b, "\\\\");
        else if (*s == '\n') bstr(b, "\\n");
        else if (*s == '\t') bstr(b, "\\t");
        else badd(b, s, 1);
    }
    bstr(b, "\"");
}

static void put_scalar(B *b, int type, const char *v) {
    if (type == EX_PROP_LINKS || (type == EX_PROP_TEXT && needs_quote(v)) || (type == EX_PROP_LIST && (needs_quote(v) || strstr(v, "[[")))) put_quoted(b, v);
    else bstr(b, v);
}

/* The lines of a property ("name: value\n" or a block list). */
static char *format_prop(const char *name, int type, const char *value, size_t *n) {
    B b = {0};
    if (type == EX_PROP_LIST) {
        if (!value[0]) { bstr(&b, name); bstr(&b, ": []\n"); }
        else {
            const char *s = value;
            bstr(&b, name); bstr(&b, ":\n");
            for (;;) {
                const char *e = strchr(s, '\n');
                size_t len = e ? (size_t)(e - s) : strlen(s);
                char *item = malloc(len + 1);
                if (!item) { b.oom = 1; break; }
                memcpy(item, s, len); item[len] = 0;
                bstr(&b, "  - "); put_scalar(&b, EX_PROP_LIST, item); bstr(&b, "\n");
                free(item);
                if (!e) break;
                s = e + 1;
            }
        }
    } else {
        bstr(&b, name); bstr(&b, ": "); put_scalar(&b, type, value); bstr(&b, "\n");
    }
    if (b.oom) { free(b.v); return NULL; }
    if (n) *n = b.n;
    return b.v;
}

static int valid_key(const char *name) {
    size_t i;
    if (!name[0]) return 0;
    for (i = 0; name[i]; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20 || c == 0x7F) return 0;                                                    /* a control character or a line break */
        if (c == 0xC2 && (unsigned char)name[i + 1] == 0x85) return 0;                          /* NEL, LS and PS are line breaks in YAML */
        if (c == 0xE2 && (unsigned char)name[i + 1] == 0x80 && ((unsigned char)name[i + 2] == 0xA8 || (unsigned char)name[i + 2] == 0xA9)) return 0;
        if (c == 0xEF && (unsigned char)name[i + 1] == 0xBB && (unsigned char)name[i + 2] == 0xBF) return 0;
    }
    for (i = 0; name[i]; i++) if (name[i] == ':' || name[i] == '#' || name[i] == '\n' || name[i] == '"' || name[i] == '\'' || name[i] == '[' || name[i] == ']' || name[i] == '{' || name[i] == '}' || name[i] == ',') return 0;
    return name[0] != ' ' && name[strlen(name) - 1] != ' ' && !strchr("-?&*!|>%@`", name[0]);
}

static char *splice(const char *text, size_t len, size_t from, size_t to, const char *repl, size_t rlen, size_t *out_len) {
    char *out = malloc(len - (to - from) + rlen + 1);
    if (!out) return NULL;
    memcpy(out, text, from);
    memcpy(out + from, repl, rlen);
    memcpy(out + from + rlen, text + to, len - to);
    out[len - (to - from) + rlen] = 0;
    if (out_len) *out_len = len - (to - from) + rlen;
    return out;
}

static const ExXProp *find_prop(const ExExtract *x, const char *name) {
    size_t i;
    for (i = 0; i < x->nprops; i++) if (!strcmp(x->props[i].name, name)) return &x->props[i];
    return NULL;
}

/* Where the content of the frontmatter ends: the start of the closing line. */
static size_t front_content_end(const ExExtract *x, const char *text) {
    size_t e = x->front_end;
    while (e > x->front_start && text[e - 1] != '\n') e--;
    return e;
}

char *ex_prop_text_set(const char *text, size_t len, const char *name, int type, const char *value, size_t *out_len) {
    ExExtract *x;
    const ExXProp *p;
    size_t flen = 0;
    char *lines, *out;
    if (!valid_key(name)) return NULL;
    x = ex_extract(text, len);
    if (!x) return NULL;
    lines = format_prop(name, type, value, &flen);
    if (!lines) { ex_extract_free(x); return NULL; }
    p = find_prop(x, name);
    if (p) out = splice(text, len, p->start, p->end, lines, flen, out_len);
    else if (x->front_end > x->front_start) {
        if (!x->front_valid) out = NULL;
        else out = splice(text, len, front_content_end(x, text), front_content_end(x, text), lines, flen, out_len);
    } else {                                                     /* no frontmatter yet: one is made at the top */
        B b = {0};
        bstr(&b, "---\n"); badd(&b, lines, flen); bstr(&b, "---\n"); badd(&b, text, len);
        out = b.oom ? NULL : b.v;
        if (b.oom) free(b.v);
        if (out && out_len) *out_len = b.n;
    }
    free(lines);
    ex_extract_free(x);
    return out;
}

char *ex_prop_text_remove(const char *text, size_t len, const char *name, size_t *out_len) {
    ExExtract *x = ex_extract(text, len);
    const ExXProp *p;
    char *out = NULL;
    if (!x) return NULL;
    p = find_prop(x, name);
    if (p) out = splice(text, len, p->start, p->end, "", 0, out_len);
    ex_extract_free(x);
    return out;
}

char *ex_prop_text_rename(const char *text, size_t len, const char *from, const char *to, size_t *out_len) {
    ExExtract *x;
    const ExXProp *p;
    char *out = NULL;
    size_t k;
    if (!valid_key(to)) return NULL;
    x = ex_extract(text, len);
    if (!x) return NULL;
    p = find_prop(x, from);
    if (p && !find_prop(x, to)) {
        k = p->start;
        if (text[k] == '"' || text[k] == '\'') k++;                  /* a quoted key keeps its quotes */
        if (k + strlen(from) <= len && !strncmp(text + k, from, strlen(from))) out = splice(text, len, k, k + strlen(from), to, strlen(to), out_len);
    }
    ex_extract_free(x);
    return out;
}

static int all_items(const char *v, int (*ok)(const char *)) { (void)v; (void)ok; return 1; }

char *ex_prop_text_retype(const char *text, size_t len, const char *name, int type, size_t *out_len) {
    ExExtract *x = ex_extract(text, len);
    const ExXProp *p;
    char *out = NULL, *lines = NULL, *val = NULL;
    size_t flen = 0, vn;
    if (!x) return NULL;
    p = find_prop(x, name);
    if (!p) { ex_extract_free(x); return NULL; }
    if (p->type == type) { ex_extract_free(x); out = malloc(len + 1); if (out) { memcpy(out, text, len); out[len] = 0; if (out_len) *out_len = len; } return out; }
    (void)all_items;
    vn = strlen(p->value);
    val = malloc(vn * 2 + 8);
    if (!val) { ex_extract_free(x); return NULL; }
    if (type == EX_PROP_LIST) {
        if (p->type == EX_PROP_LIST) strcpy(val, p->value);
        else strcpy(val, p->value);                                  /* the one value becomes the one item */
    } else if (p->type == EX_PROP_LIST) {                            /* the items joined with ", " */
        size_t i, o = 0;
        for (i = 0; i < vn; i++) { if (p->value[i] == '\n') { val[o++] = ','; val[o++] = ' '; } else val[o++] = p->value[i]; }
        val[o] = 0;
    } else strcpy(val, p->value);
    switch (type) {
    case EX_PROP_CHECKBOX: if (strcmp(val, "true") && strcmp(val, "false")) goto done; break;
    case EX_PROP_NUMBER: case EX_PROP_DATE: case EX_PROP_DATETIME: if (ex_prop_type_of_text(val, 0) != type) goto done; break;
    case EX_PROP_LINKS: if (!strstr(val, "[[")) { memmove(val + 2, val, strlen(val) + 1); val[0] = '['; val[1] = '['; strcat(val, "]]"); } break;
    default: break;
    }
    lines = format_prop(name, type, val, &flen);
    if (lines) out = splice(text, len, p->start, p->end, lines, flen, out_len);
done:
    free(lines); free(val);
    ex_extract_free(x);
    return out;
}

/* ---- notes as files ---- */

typedef struct FileIo { char *root; } FileIo;

static char *abs_of(const FileIo *f, const char *rel) {
    size_t n = strlen(f->root) + strlen(rel) + 2;
    char *p = malloc(n);
    if (p) snprintf(p, n, "%s/%s", f->root, rel);
    return p;
}

static char *file_read(void *user, const char *rel, size_t *len) {
    char *path = abs_of(user, rel), *bytes, *text = NULL;
    size_t blen = 0, tlen = 0;
    if (!path) return NULL;
    bytes = ex_platform_read_file(path, &blen);
    free(path);
    if (!bytes) return NULL;
    if (ex_note_decode((const uint8_t *)bytes, blen, &text, &tlen, NULL) != 0) text = NULL;
    free(bytes);
    if (text && len) *len = tlen;
    return text;
}

static int file_write(void *user, const char *rel, const char *text, size_t len) {
    char *path = abs_of(user, rel), *bytes;
    size_t blen = 0, olen = 0;
    char *old_text = NULL;
    ExNoteFormat fmt;
    uint8_t *enc;
    int rc = -1;
    if (!path) return -1;
    bytes = ex_platform_read_file(path, &blen);
    if (!bytes || ex_note_decode((const uint8_t *)bytes, blen, &old_text, NULL, &fmt) != 0) { free(bytes); free(path); return -1; }
    free(old_text); free(bytes);
    enc = ex_note_encode(text, len, &fmt, &olen);
    if (enc) { rc = ex_platform_save_file(path, enc, olen, NULL); free(enc); }
    free(path);
    return rc;
}

ExNoteIO ex_note_io_files(const char *vault_root) {
    ExNoteIO io;
    FileIo *f = calloc(1, sizeof *f);
    if (f) f->root = ex_strdup(vault_root);
    io.read = file_read; io.write = file_write; io.user = f;
    return io;
}

void ex_note_io_free(ExNoteIO *io) {
    FileIo *f = io->user;
    if (f) { free(f->root); free(f); }
    io->user = NULL;
}

/* ---- the whole vault ---- */

static int paths_with_prop(ExIndex *r, const char *name, char ***paths, size_t *n) {
    sqlite3_stmt *st = ex_db_prepare(r, "SELECT DISTINCT f.path FROM props p JOIN files f ON f.id = p.file_id WHERE p.name = ?1 ORDER BY f.path");
    char **v = NULL;
    size_t cnt = 0, cap = 0;
    *paths = NULL; *n = 0;
    if (!st) return -1;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (cnt == cap) { size_t nc = cap ? cap * 2 : 32; char **nv = realloc(v, nc * sizeof *nv); if (!nv) break; v = nv; cap = nc; }
        v[cnt++] = ex_strdup((const char *)sqlite3_column_text(st, 0));
    }
    sqlite3_finalize(st);
    *paths = v; *n = cnt;
    return 0;
}

int ex_props_rename_everywhere(ExIndex *r, const ExNoteIO *io, const char *root, const char *from, const char *to, size_t *changed) {
    char **paths;
    size_t n, i, c = 0;
    int rc = 0, old_type;
    if (changed) *changed = 0;
    if (!valid_key(to) || !strcmp(from, to) || paths_with_prop(r, from, &paths, &n) != 0) return -1;
    for (i = 0; i < n; i++) {
        size_t len = 0, olen = 0;
        char *text = io->read(io->user, paths[i], &len), *out;
        if (!text) { rc = -1; continue; }
        out = ex_prop_text_rename(text, len, from, to, &olen);
        if (out) { if (io->write(io->user, paths[i], out, olen) == 0) c++; else rc = -1; free(out); }
        free(text);
    }
    ex_paths_free(paths, n);
    old_type = root ? ex_prop_type_get(root, from) : -1;
    if (old_type >= 0 && ex_prop_type_get(root, to) < 0) ex_prop_type_set(root, to, old_type);
    if (changed) *changed = c;
    return rc;
}

int ex_props_retype_everywhere(ExIndex *r, const ExNoteIO *io, const char *root, const char *name, int type, size_t *changed) {
    char **paths;
    size_t n, i, c = 0;
    int rc = 0;
    if (changed) *changed = 0;
    if (type < 0 || type > EX_PROP_LINKS || paths_with_prop(r, name, &paths, &n) != 0) return -1;
    for (i = 0; i < n; i++) {
        size_t len = 0, olen = 0;
        char *text = io->read(io->user, paths[i], &len), *out;
        if (!text) { rc = -1; continue; }
        out = ex_prop_text_retype(text, len, name, type, &olen);
        if (out) {
            if (olen != len || memcmp(out, text, len)) { if (io->write(io->user, paths[i], out, olen) == 0) c++; else rc = -1; }
            free(out);
        }
        free(text);
    }
    ex_paths_free(paths, n);
    if (root && ex_prop_type_set(root, name, type) != 0) rc = -1;
    if (changed) *changed = c;
    return rc;
}
