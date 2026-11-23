/* The importers (sub-slice 6e-v): see include/exhaust_import.h. Everything that reaches the vault goes through `new_file`, which never replaces a file: it
 * writes a temporary file and renames it without replacing, and a name that is taken gets a number. */
#include "exhaust_import.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_platform.h"
#include "exhaust_templates.h"
#include "exhaust_vault.h"

#define MAX_FILE (64u * 1024u * 1024u)

static const char *const LATER[] = { "Apple Journal", "Apple Notes", "Bear", "Craft", "Evernote", "Google Keep", "Logseq", "Notion", "OneNote", "Roam", "Tomboy / Gnote", "Airtable" };
size_t ex_import_later_count(void) { return sizeof LATER / sizeof *LATER; }
const char *ex_import_later_name(size_t i) { return i < sizeof LATER / sizeof *LATER ? LATER[i] : NULL; }

const char *ex_import_format_name(ExImportFormat f) {
    static const char *const n[] = { "Markdown", "Plain text", "HTML", "CSV", "Textbundle" };
    return (int)f >= 0 && f < EX_IMPORT_FORMAT_COUNT ? n[f] : "";
}
const char *ex_import_format_help(ExImportFormat f) {
    static const char *const h[] = { "A folder of Markdown notes (and the pictures and files beside them).", "A folder of .txt files.", "A folder of HTML files, or one HTML file.",
                                     "One CSV file: a note for every row.", "A folder of .textbundle folders or .textpack files." };
    return (int)f >= 0 && f < EX_IMPORT_FORMAT_COUNT ? h[f] : "";
}
int ex_import_wants_file(ExImportFormat f) { return f == EX_IMPORT_CSV; }

typedef struct Ctx {
    const ExImportOptions *o;
    char root[1200];
    char dest[600];                      /* the folder of the vault, with no slash at the end ("" for the top) */
    ExImportReport *r;
    const volatile int *cancel;
    ExTplContext tc;
    char *tpl; size_t tpl_len;
    int tpl_has_content;
} Ctx;

static void problem(Ctx *c, const char *fmt, const char *what) {
    if (!c->r->first_problem[0]) snprintf(c->r->first_problem, sizeof c->r->first_problem, fmt, what ? what : "");
}

/* ---- names ---- */

static void sanitize(const char *in, char *out, size_t cap) {
    size_t i = 0, w = 0;
    while (in[i] && w + 4 < cap) {
        unsigned char c = (unsigned char)in[i];
        if (c < 32 || c == 127 || strchr("/\\:*?\"<>|#^[]", c)) { if (w && out[w - 1] != ' ') out[w++] = '-'; i++; continue; }
        if (c == '%' && in[i + 1] == '%') { i += 2; continue; }
        out[w++] = (char)c; i++;
    }
    out[w] = 0;
    while (w && (out[w - 1] == ' ' || out[w - 1] == '.' || out[w - 1] == '-')) out[--w] = 0;
    {   size_t lead = 0; while (out[lead] == ' ' || out[lead] == '.' || out[lead] == '-') lead++; if (lead) memmove(out, out + lead, strlen(out + lead) + 1); }
    if (strlen(out) > 120) { size_t k = 120; while (k > 0 && ((unsigned char)out[k] & 0xC0) == 0x80) k--; out[k] = 0; }
    if (!out[0]) snprintf(out, cap, "Untitled");
    if (!ex_name_valid(out)) { char t[200]; snprintf(t, sizeof t, "%s_", out); snprintf(out, cap, "%s", t); if (!ex_name_valid(out)) snprintf(out, cap, "Untitled"); }
}

static void abs_of(const Ctx *c, const char *rel, char *out, size_t cap) { snprintf(out, cap, "%s/%s", c->root, rel); }

/* Writes `data` as a new file at rel_dir/stem+ext (rel_dir below the destination), never replacing: returns 0 and the path used in `used` */
static int new_file(Ctx *c, const char *rel_dir, const char *stem, const char *ext, const void *data, size_t len, char *used, size_t ucap) {
    int k;
    char rel[1500], abs[3000], tmp[3100], dir[3000];
    ExPathInfo info;
    for (k = 1; k < 10000; k++) {
        if (k == 1) snprintf(rel, sizeof rel, "%s%s%s%s%s%s", c->dest, c->dest[0] && rel_dir[0] ? "/" : "", rel_dir, (c->dest[0] || rel_dir[0]) ? "/" : "", stem, ext);
        else snprintf(rel, sizeof rel, "%s%s%s%s%s %d%s", c->dest, c->dest[0] && rel_dir[0] ? "/" : "", rel_dir, (c->dest[0] || rel_dir[0]) ? "/" : "", stem, k, ext);
        abs_of(c, rel, abs, sizeof abs);
        if (ex_platform_path_info(abs, &info) == 0) continue;                          /* taken (also by a name that differs only in case, on a system that folds it) */
        snprintf(dir, sizeof dir, "%s", abs);
        { char *sl = strrchr(dir, '/'); if (sl) { *sl = 0; if (ex_platform_make_dirs(dir) != 0) { problem(c, "A folder could not be made: %s", dir); return -1; } } }
        snprintf(tmp, sizeof tmp, "%s.import~", abs);
        if (ex_platform_write_file_atomic(tmp, data, len) != 0) { problem(c, "A file could not be written: %s", abs); return -1; }
        if (ex_platform_rename_noreplace(tmp, abs) != 0) { ex_platform_remove_tree(tmp); continue; }         /* it appeared meanwhile: the next number */
        if (k > 1) c->r->renamed++;
        if (used) snprintf(used, ucap, "%s", rel);
        return 0;
    }
    return -1;
}

/* ---- the text of a note ---- */

/* the note's text from its content (and the properties a row made): the importer template wraps it */
static char *compose(Ctx *c, const char *title, const char *front, const char *content, size_t clen, size_t *out_len) {
    ExTplContext tc = c->tc;
    char *base, *res;
    size_t fl = front ? strlen(front) : 0, bl;
    tc.title = title;
    if (c->tpl && c->tpl_has_content) {            /* the template says where the content goes */
        char *expanded;
        char *cont = malloc(clen + 1);
        if (!cont) return NULL;
        memcpy(cont, content, clen); cont[clen] = 0;
        tc.content = cont;
        base = malloc(fl + 1);
        if (!base) { free(cont); return NULL; }
        memcpy(base, front ? front : "", fl); base[fl] = 0; bl = fl;
        expanded = ex_tpl_apply(base, bl, c->tpl, c->tpl_len, &tc, bl, out_len, NULL);
        free(base); free(cont);
        return expanded;
    }
    base = malloc(fl + clen + 1);
    if (!base) return NULL;
    memcpy(base, front ? front : "", fl); memcpy(base + fl, content, clen); base[fl + clen] = 0; bl = fl + clen;
    if (!c->tpl) { *out_len = bl; return base; }
    res = ex_tpl_apply(base, bl, c->tpl, c->tpl_len, &tc, fl, out_len, NULL);       /* the template's text goes in front of the content */
    free(base);
    return res;
}

/* the name pattern with {{title}}, {{filename}}, {{date}}, {{time}} and, for a row, its columns */
typedef struct Row { const char *const *heads; const char *const *vals; size_t n; } Row;

static void name_from(Ctx *c, const char *title, const char *filename, const Row *row, char *out, size_t cap) {
    const char *pat = c->o->note_name && *c->o->note_name ? c->o->note_name : "{{title}}";
    char *pre = malloc(strlen(pat) * 2 + 4096), *w;
    const char *p = pat;
    ExTplContext tc = c->tc;
    size_t n = 0;
    char *res;
    if (!pre) { snprintf(out, cap, "%s", title); return; }
    w = pre;
    while (*p) {
        if (p[0] == '{' && p[1] == '{') {
            const char *e = strstr(p + 2, "}}");
            if (e) {
                size_t nl = (size_t)(e - p - 2), i;
                char key[200];
                int done = 0;
                while (nl && (p[2] == ' ')) { p++; nl--; }
                while (nl && p[2 + nl - 1] == ' ') nl--;
                snprintf(key, sizeof key, "%.*s", (int)(nl < sizeof key - 1 ? nl : sizeof key - 1), p + 2);
                if (!strcmp(key, "title")) { size_t l = strlen(title); if (l > 400) l = 400; memcpy(w, title, l); w += l; done = 1; }
                else if (!strcmp(key, "filename")) { size_t l = strlen(filename); if (l > 400) l = 400; memcpy(w, filename, l); w += l; done = 1; }
                else if (row) for (i = 0; i < row->n; i++) {
                    const char *h = row->heads[i];
                    if (h && !strcmp(h, key) && row->vals[i]) { size_t l = strlen(row->vals[i]); if (l > 400) l = 400; memcpy(w, row->vals[i], l); w += l; done = 1; break; }
                }
                if (done) { p = e + 2; continue; }
            }
        }
        *w++ = *p++;
    }
    *w = 0;
    tc.title = title;
    res = ex_tpl_expand(pre, strlen(pre), &tc, &n);                                       /* {{date}} and {{time}} */
    sanitize(res ? res : pre, out, cap);
    free(res); free(pre);
}

/* ---- sources ---- */

typedef struct Walk { Ctx *c; const char *base; size_t baselen; ExImportFormat f; int count_only; } Walk;

static const char *ext_of(const char *name) { const char *d = strrchr(name, '.'); const char *s = strrchr(name, '/'); return d && (!s || d > s) ? d : ""; }
static int ci_eq(const char *a, const char *b) { for (; *a && *b; a++, b++) { char x = *a, y = *b; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) return 0; } return *a == *b; }

static char *read_limited(const char *path, size_t *len) {
    ExPathInfo info;
    if (ex_platform_path_info(path, &info) != 0 || !info.is_file || info.size > (int64_t)MAX_FILE) return NULL;
    return ex_platform_read_file(path, len);
}

/* the folder part of a source file, relative to the source folder: "a/b" for "<base>/a/b/file.md" */
static void rel_dir_of(const Walk *w, const char *path, char *out, size_t cap) {
    const char *rel = path + w->baselen, *sl;
    char tmp[1500];
    out[0] = 0;
    if (!w->c->o->keep_folders) return;
    while (*rel == '/') rel++;
    snprintf(tmp, sizeof tmp, "%s", rel);
    sl = strrchr(tmp, '/');
    if (!sl) return;
    *(char *)sl = 0;
    {   /* every part of the folder is a valid name */
        char *part = tmp;
        size_t w2 = 0;
        out[0] = 0;
        while (part && *part) {
            char *e = strchr(part, '/'), clean[200];
            if (e) *e = 0;
            sanitize(part, clean, sizeof clean);
            w2 += (size_t)snprintf(out + w2, cap - w2, "%s%s", w2 ? "/" : "", clean);
            part = e ? e + 1 : NULL;
        }
    }
}

static void stem_of(const char *path, char *out, size_t cap) {
    const char *s = strrchr(path, '/'), *d;
    char t[400];
    s = s ? s + 1 : path;
    snprintf(t, sizeof t, "%s", s);
    d = strrchr(t, '.');
    if (d && d != t) t[d - t] = 0;
    snprintf(out, cap, "%s", t);
}

/* Markdown, text and HTML pictures: a file next to the notes is copied as it is */
static void copy_file(Ctx *c, const char *path, const char *rel_dir) {
    size_t len = 0;
    char *data = read_limited(path, &len), name[400], stem[400], used[1500];
    const char *slash = strrchr(path, '/'), *dot;
    if (!data) { c->r->skipped++; problem(c, "A file could not be read: %s", path); return; }
    slash = slash ? slash + 1 : path;
    snprintf(name, sizeof name, "%s", slash);
    dot = strrchr(name, '.');
    if (dot && dot != name) { snprintf(stem, sizeof stem, "%.*s", (int)(dot - name), name); } else { snprintf(stem, sizeof stem, "%s", name); dot = ""; }
    { char clean[400]; sanitize(stem, clean, sizeof clean); snprintf(stem, sizeof stem, "%s", clean); }
    if (new_file(c, rel_dir, stem, dot, data, len, used, sizeof used) == 0) c->r->files++; else c->r->skipped++;
    free(data);
}

static void import_note_text(Ctx *c, const char *path, const char *rel_dir, const char *title, const char *front, const char *content, size_t clen, const Row *row) {
    char fname[400], name[400], used[1500];
    size_t tl = 0;
    char *text;
    stem_of(path, fname, sizeof fname);
    name_from(c, title, fname, row, name, sizeof name);
    text = compose(c, title, front, content, clen, &tl);
    if (!text) { c->r->skipped++; return; }
    if (new_file(c, rel_dir, name, ".md", text, tl, used, sizeof used) == 0) c->r->notes++; else c->r->skipped++;
    free(text);
}

/* a copied picture: the HTML's `![alt](path)` becomes `![[name]]` when the file is next to the HTML file */
static char *rewrite_images(Ctx *c, const char *html_path, const char *rel_dir, const char *md, size_t len, size_t *out_len) {
    char dirpath[1500];
    size_t i = 0, w = 0;
    char *out = malloc(len * 2 + 64);
    const char *sl = strrchr(html_path, '/');
    if (!out) return NULL;
    snprintf(dirpath, sizeof dirpath, "%s", html_path);
    if (sl) dirpath[sl - html_path] = 0; else dirpath[0] = 0;
    while (i < len) {
        if (md[i] == '!' && i + 1 < len && md[i + 1] == '[') {
            size_t a = i + 2, depth = 1;
            while (a < len && depth && md[a] != '\n') { if (md[a] == '\\') a++; else if (md[a] == '[') depth++; else if (md[a] == ']') depth--; a++; }
            if (!depth && a < len && md[a] == '(') {
                size_t u0 = a + 1, u1 = u0;
                while (u1 < len && md[u1] != ')' && md[u1] != '\n') u1++;
                if (u1 < len && md[u1] == ')' && u1 > u0) {
                    char target[800], file[2400], decoded[800];
                    size_t tl = u1 - u0, di = 0, k;
                    if (tl < sizeof target) {
                        memcpy(target, md + u0, tl); target[tl] = 0;
                        for (k = 0; target[k] && di < sizeof decoded - 1; k++) {
                            if (target[k] == '%' && target[k + 1] && target[k + 2]) { char h[3] = { target[k + 1], target[k + 2], 0 }; char *e; long v = strtol(h, &e, 16); if (!*e) { decoded[di++] = (char)v; k += 2; continue; } }
                            decoded[di++] = target[k];
                        }
                        decoded[di] = 0;
                        if (strncmp(decoded, "http://", 7) && strncmp(decoded, "https://", 8) && strncmp(decoded, "data:", 5) && strncmp(decoded, "file:", 5) && decoded[0] != '/' && !strstr(decoded, "..")) {
                            ExPathInfo info;
                            snprintf(file, sizeof file, "%s/%s", dirpath, decoded);
                            if (ex_platform_path_info(file, &info) == 0 && info.is_file) {
                                size_t flen = 0;
                                char *data = read_limited(file, &flen), stem[800], used[1500];
                                const char *nm = strrchr(decoded, '/'), *dot;
                                char name[800];
                                nm = nm ? nm + 1 : decoded;
                                snprintf(name, sizeof name, "%s", nm);
                                dot = strrchr(name, '.');
                                if (dot && dot != name) snprintf(stem, sizeof stem, "%.*s", (int)(dot - name), name); else { snprintf(stem, sizeof stem, "%s", name); dot = ""; }
                                { char clean[400]; sanitize(stem, clean, sizeof clean); snprintf(stem, sizeof stem, "%s", clean); }
                                if (data && new_file(c, rel_dir, stem, dot, data, flen, used, sizeof used) == 0) {
                                    const char *un = strrchr(used, '/');
                                    c->r->files++;
                                    w += (size_t)snprintf(out + w, len * 2 + 64 - w, "![[%s]]", un ? un + 1 : used);
                                    free(data);
                                    i = u1 + 1;
                                    continue;
                                }
                                free(data);
                            }
                        }
                    }
                }
            }
        }
        if (w + 2 > len * 2 + 64) break;
        out[w++] = md[i++];
    }
    out[w] = 0;
    *out_len = w;
    return out;
}

static int walk_cb(const char *path, int is_dir, int64_t mtime, void *user) {
    Walk *w = user;
    Ctx *c = w->c;
    const char *name = strrchr(path, '/'), *ext;
    char rel_dir[1500];
    (void)mtime;
    if (c->cancel && *c->cancel) { c->r->cancelled = 1; return 1; }
    if (is_dir) return 0;
    name = name ? name + 1 : path;
    if (name[0] == '.' || strstr(path + w->baselen, "/.")) return 0;                    /* hidden files and folders (.git, .obsidian, .DS_Store) are not notes */
    ext = ext_of(name);
    rel_dir_of(w, path, rel_dir, sizeof rel_dir);
    switch (w->f) {
    case EX_IMPORT_MARKDOWN:
        if (ci_eq(ext, ".md") || ci_eq(ext, ".markdown")) {
            size_t len = 0;
            char *t = read_limited(path, &len), stem[400];
            if (!t) { c->r->skipped++; problem(c, "A note could not be read: %s", path); return 0; }
            stem_of(path, stem, sizeof stem);
            import_note_text(c, path, rel_dir, stem, NULL, t, len, NULL);
            free(t);
        } else if (!ci_eq(ext, ".exhaustimport")) copy_file(c, path, rel_dir);
        break;
    case EX_IMPORT_TEXT:
        if (ci_eq(ext, ".txt")) {
            size_t len = 0;
            char *t = read_limited(path, &len), stem[400];
            if (!t) { c->r->skipped++; problem(c, "A file could not be read: %s", path); return 0; }
            stem_of(path, stem, sizeof stem);
            import_note_text(c, path, rel_dir, stem, NULL, t, len, NULL);
            free(t);
        }
        break;
    case EX_IMPORT_HTML:
        if (ci_eq(ext, ".html") || ci_eq(ext, ".htm") || ci_eq(ext, ".xhtml")) {
            size_t len = 0, ml = 0, rl = 0;
            char *t = read_limited(path, &len), *title = NULL, *md, *rew, stem[400];
            if (!t) { c->r->skipped++; problem(c, "A file could not be read: %s", path); return 0; }
            md = ex_html_to_markdown(t, len, &title, &ml);
            free(t);
            if (!md) { c->r->skipped++; return 0; }
            rew = rewrite_images(c, path, rel_dir, md, ml, &rl);
            stem_of(path, stem, sizeof stem);
            import_note_text(c, path, rel_dir, title && *title ? title : stem, NULL, rew ? rew : md, rew ? rl : ml, NULL);
            free(rew); free(md); free(title);
        }
        break;
    default: break;
    }
    return 0;
}

/* ---- CSV ---- */

typedef struct Csv { Ctx *c; const char *path; char **heads; size_t nheads; int title_col; } Csv;

static int yaml_plain_ok(const char *v) {
    size_t i, n = strlen(v);
    if (!n) return 0;
    if (strchr(" -?:,[]{}#&*!|>'\"%@`", v[0]) && !(v[0] == '-' && n > 1 && v[1] >= '0' && v[1] <= '9')) return 0;
    if (v[n - 1] == ' ' || v[n - 1] == ':') return 0;
    for (i = 0; i < n; i++) { if ((unsigned char)v[i] < 32) return 0; if (v[i] == ':' && (i + 1 == n || v[i + 1] == ' ')) return 0; if (v[i] == '#' && i > 0 && v[i - 1] == ' ') return 0; }
    if (!strcmp(v, "true") || !strcmp(v, "false") || !strcmp(v, "null") || !strcmp(v, "yes") || !strcmp(v, "no") || !strcmp(v, "~")) return 0;
    return 1;
}

static void yaml_put(char **buf, size_t *n, size_t *cap, const char *s, size_t len) {
    if (*n + len + 1 > *cap) { size_t nc = *cap ? *cap : 256; char *nb; while (nc < *n + len + 1) nc *= 2; nb = realloc(*buf, nc); if (!nb) return; *buf = nb; *cap = nc; }
    memcpy(*buf + *n, s, len); *n += len; (*buf)[*n] = 0;
}

static int csv_row(const char *const *f, const size_t *lens, size_t n, size_t row, void *user) {
    Csv *cs = user;
    Ctx *c = cs->c;
    size_t i;
    if (c->cancel && *c->cancel) { c->r->cancelled = 1; return 1; }
    (void)lens;
    if (row == 0) {                                                             /* the headings */
        cs->heads = calloc(n ? n : 1, sizeof *cs->heads);
        if (!cs->heads) return 1;
        cs->nheads = n;
        cs->title_col = 0;
        for (i = 0; i < n; i++) {
            cs->heads[i] = malloc(strlen(f[i]) + 1);
            if (cs->heads[i]) strcpy(cs->heads[i], f[i]);
            if (cs->heads[i] && (ci_eq(f[i], "title") || ci_eq(f[i], "name"))) { if (cs->title_col == 0 || ci_eq(f[cs->title_col], "name")) cs->title_col = (int)i; }
        }
        return 0;
    }
    {
        char *front = NULL, title[400];
        size_t fl = 0, fcap = 0, k;
        const char *tv = (size_t)cs->title_col < n ? f[cs->title_col] : "";
        const char *vals[1000];
        Row r;
        snprintf(title, sizeof title, "%s", tv && *tv ? tv : "Row");
        if (strlen(tv) == 0) snprintf(title, sizeof title, "Row %zu", row);
        for (k = 0; k < n && k < 1000; k++) vals[k] = f[k];
        r.heads = (const char *const *)cs->heads; r.vals = vals; r.n = n < cs->nheads ? n : cs->nheads;
        for (k = 0; k < r.n; k++) {
            char key[200], q[8200];
            size_t kl = 0, j;
            const char *v = f[k];
            if ((int)k == cs->title_col && 0) continue;
            if (!v[0] || !cs->heads[k] || !cs->heads[k][0]) continue;
            for (j = 0; cs->heads[k][j] && kl < sizeof key - 1; j++) { char ch = cs->heads[k][j]; key[kl++] = strchr(":#[]{},\"'`|>&*!%@\n\r\t", ch) ? ' ' : ch; }
            key[kl] = 0;
            { size_t a = 0; while (key[a] == ' ') a++; if (a) memmove(key, key + a, strlen(key + a) + 1); kl = strlen(key); while (kl && key[kl - 1] == ' ') key[--kl] = 0; }
            if (!key[0]) continue;
            if (!fl) yaml_put(&front, &fl, &fcap, "---\n", 4);
            yaml_put(&front, &fl, &fcap, key, strlen(key));
            yaml_put(&front, &fl, &fcap, ": ", 2);
            if (yaml_plain_ok(v) && strlen(v) < 4000) yaml_put(&front, &fl, &fcap, v, strlen(v));
            else {
                size_t qn = 0;
                q[qn++] = '"';
                for (j = 0; v[j] && qn + 6 < sizeof q && j < 4000; j++) {
                    unsigned char ch = (unsigned char)v[j];
                    if (ch == '"' || ch == '\\') { q[qn++] = '\\'; q[qn++] = (char)ch; }
                    else if (ch == '\n') { q[qn++] = '\\'; q[qn++] = 'n'; }
                    else if (ch == '\r') { q[qn++] = '\\'; q[qn++] = 'r'; }
                    else if (ch == '\t') { q[qn++] = '\\'; q[qn++] = 't'; }
                    else if (ch < 32) { q[qn++] = ' '; }
                    else q[qn++] = (char)ch;
                }
                q[qn++] = '"';
                yaml_put(&front, &fl, &fcap, q, qn);
            }
            yaml_put(&front, &fl, &fcap, "\n", 1);
        }
        if (fl) yaml_put(&front, &fl, &fcap, "---\n", 4);
        import_note_text(c, cs->path, "", title, front, "", 0, &r);
        free(front);
    }
    return 0;
}

/* ---- Textbundle ---- */

static int is_text_name(const char *n) { return ci_eq(n, "text.markdown") || ci_eq(n, "text.md") || ci_eq(n, "text.mdown") || ci_eq(n, "text.mkd") || ci_eq(n, "text.txt") || ci_eq(n, "text.text"); }

/* `![alt](assets/x.png)` becomes `![[x.png]]` with the name the picture got in the vault; names[i] -> used[i] */
static char *rewrite_assets(const char *md, size_t len, char **orig, char **used, size_t n, size_t *out_len) {
    size_t i = 0, w = 0, cap = len * 2 + 64;
    char *out = malloc(cap);
    if (!out) return NULL;
    while (i < len) {
        if (md[i] == '!' && i + 1 < len && md[i + 1] == '[') {
            size_t a = i + 2;
            while (a < len && md[a] != ']' && md[a] != '\n') a++;
            if (a + 1 < len && md[a] == ']' && md[a + 1] == '(') {
                size_t u0 = a + 2, u1 = u0, k;
                while (u1 < len && md[u1] != ')' && md[u1] != '\n') u1++;
                if (u1 < len && md[u1] == ')' && u1 - u0 > 7 && !strncmp(md + u0, "assets/", 7)) {
                    for (k = 0; k < n; k++) {
                        size_t ol = strlen(orig[k]);
                        if (ol == u1 - u0 - 7 && !strncmp(md + u0 + 7, orig[k], ol)) { w += (size_t)snprintf(out + w, cap - w, "![[%s]]", used[k]); i = u1 + 1; goto next; }
                    }
                }
            }
        }
        if (w + 2 > cap) break;
        out[w++] = md[i++];
    next:;
    }
    out[w] = 0; *out_len = w;
    return out;
}

typedef struct Bundle { Ctx *c; const char *base; size_t baselen; } Bundle;

typedef struct Collect { char **v; size_t n, cap; } Collect;
static int collect_cb(const char *p, int is_dir, int64_t mt, void *u) {
    Collect *cc = u;
    (void)mt;
    if (is_dir) return 0;
    if (cc->n == cc->cap) { size_t nc = cc->cap ? cc->cap * 2 : 16; char **nv = realloc(cc->v, nc * sizeof *nv); if (!nv) return 1; cc->v = nv; cc->cap = nc; }
    cc->v[cc->n] = malloc(strlen(p) + 1);
    if (cc->v[cc->n]) { strcpy(cc->v[cc->n], p); cc->n++; }
    return 0;
}

static void import_bundle_folder(Ctx *c, const char *bundle, const char *bundle_name) {
    char *txt_path = NULL, assets[1600];
    char **orig = NULL, **used = NULL;
    size_t n = 0, cap = 0, k;
    const char *const names[] = { "text.markdown", "text.md", "text.mdown", "text.mkd", "text.txt", "text.text" };
    size_t i;
    char path[1700], stem[300], *text = NULL;
    size_t tl = 0;
    for (i = 0; i < sizeof names / sizeof *names && !txt_path; i++) {
        ExPathInfo info;
        snprintf(path, sizeof path, "%s/%s", bundle, names[i]);
        if (ex_platform_path_info(path, &info) == 0 && info.is_file) { txt_path = malloc(strlen(path) + 1); if (txt_path) strcpy(txt_path, path); }
    }
    if (!txt_path) { c->r->skipped++; problem(c, "No text file in the bundle: %s", bundle); return; }
    text = read_limited(txt_path, &tl);
    free(txt_path);
    if (!text) { c->r->skipped++; return; }
    snprintf(assets, sizeof assets, "%s/assets", bundle);
    {   /* the pictures and files the text uses */
        ExPathInfo info;
        if (ex_platform_path_info(assets, &info) == 0 && info.is_dir) {
            Collect col;
            memset(&col, 0, sizeof col);
            ex_platform_walk(assets, collect_cb, &col);
            for (i = 0; i < col.n; i++) {
                const char *rel = col.v[i] + strlen(assets) + 1;
                size_t flen = 0;
                char *data = read_limited(col.v[i], &flen), fstem[400], name[400], u[1500];
                const char *dot;
                if (!data) { c->r->skipped++; free(col.v[i]); continue; }
                snprintf(name, sizeof name, "%s", strrchr(rel, '/') ? strrchr(rel, '/') + 1 : rel);
                dot = strrchr(name, '.');
                if (dot && dot != name) snprintf(fstem, sizeof fstem, "%.*s", (int)(dot - name), name); else { snprintf(fstem, sizeof fstem, "%s", name); dot = ""; }
                { char clean[400]; sanitize(fstem, clean, sizeof clean); snprintf(fstem, sizeof fstem, "%s", clean); }
                if (new_file(c, "", fstem, dot, data, flen, u, sizeof u) == 0) {
                    const char *un = strrchr(u, '/');
                    c->r->files++;
                    if (n == cap) { size_t nc = cap ? cap * 2 : 16; char **a = realloc(orig, nc * sizeof *a), **b = realloc(used, nc * sizeof *b); if (a) orig = a; if (b) used = b; if (a && b) cap = nc; }
                    if (n < cap) {
                        orig[n] = malloc(strlen(rel) + 1); used[n] = malloc(strlen(un ? un + 1 : u) + 1);
                        if (orig[n] && used[n]) { strcpy(orig[n], rel); strcpy(used[n], un ? un + 1 : u); n++; } else { free(orig[n]); free(used[n]); }
                    }
                } else c->r->skipped++;
                free(data); free(col.v[i]);
            }
            free(col.v);
        }
    }
    {
        size_t rl = 0;
        char *rew = rewrite_assets(text, tl, orig, used, n, &rl);
        snprintf(stem, sizeof stem, "%s", bundle_name);
        { char *d = strrchr(stem, '.'); if (d) *d = 0; }
        import_note_text(c, bundle, "", stem, NULL, rew ? rew : text, rew ? rl : tl, NULL);
        free(rew);
    }
    for (k = 0; k < n; k++) { free(orig[k]); free(used[k]); }
    free(orig); free(used); free(text);
}

static void import_textpack(Ctx *c, const char *path, const char *name) {
    size_t len = 0, i, nz, nassets = 0;
    unsigned char *data = (unsigned char *)read_limited(path, &len);
    ExZip *z;
    char **orig = NULL, **used = NULL, stem[300];
    size_t cap = 0, tl = 0;
    char *text = NULL;
    if (!data) { c->r->skipped++; problem(c, "A file could not be read: %s", path); return; }
    z = ex_zip_open(data, len);
    if (!z) { c->r->skipped++; problem(c, "Not a .textpack archive: %s", path); free(data); return; }
    nz = ex_zip_count(z);
    for (i = 0; i < nz; i++) {                                                         /* the text first */
        const char *en = ex_zip_name(z, i), *base;
        if (ex_zip_is_dir(z, i)) continue;
        base = strrchr(en, '/'); base = base ? base + 1 : en;
        if (is_text_name(base) && !text) { text = (char *)ex_zip_extract(z, i, &tl); }
    }
    for (i = 0; i < nz; i++) {
        const char *en = ex_zip_name(z, i), *rel, *dot;
        size_t flen = 0;
        unsigned char *fd;
        char fstem[400], fname[400], u[1500];
        if (ex_zip_is_dir(z, i)) continue;
        rel = strstr(en, "assets/");
        if (!rel || (rel != en && rel[-1] != '/')) continue;
        rel += 7;
        fd = ex_zip_extract(z, i, &flen);
        if (!fd) { c->r->skipped++; continue; }
        snprintf(fname, sizeof fname, "%s", strrchr(rel, '/') ? strrchr(rel, '/') + 1 : rel);
        dot = strrchr(fname, '.');
        if (dot && dot != fname) snprintf(fstem, sizeof fstem, "%.*s", (int)(dot - fname), fname); else { snprintf(fstem, sizeof fstem, "%s", fname); dot = ""; }
        { char clean[400]; sanitize(fstem, clean, sizeof clean); snprintf(fstem, sizeof fstem, "%s", clean); }
        if (new_file(c, "", fstem, dot, fd, flen, u, sizeof u) == 0) {
            const char *un = strrchr(u, '/');
            c->r->files++;
            if (nassets == cap) { size_t nc = cap ? cap * 2 : 16; char **a = realloc(orig, nc * sizeof *a), **b = realloc(used, nc * sizeof *b); if (a) orig = a; if (b) used = b; if (a && b) cap = nc; }
            if (nassets < cap) {
                orig[nassets] = malloc(strlen(rel) + 1); used[nassets] = malloc(strlen(un ? un + 1 : u) + 1);
                if (orig[nassets] && used[nassets]) { strcpy(orig[nassets], rel); strcpy(used[nassets], un ? un + 1 : u); nassets++; } else { free(orig[nassets]); free(used[nassets]); }
            }
        } else c->r->skipped++;
        free(fd);
    }
    if (text) {
        size_t rl = 0;
        char *rew = rewrite_assets(text, tl, orig, used, nassets, &rl);
        snprintf(stem, sizeof stem, "%s", name);
        { char *d = strrchr(stem, '.'); if (d) *d = 0; }
        import_note_text(c, path, "", stem, NULL, rew ? rew : text, rew ? rl : tl, NULL);
        free(rew);
    } else { c->r->skipped++; problem(c, "No text file in the archive: %s", path); }
    for (i = 0; i < nassets; i++) { free(orig[i]); free(used[i]); }
    free(orig); free(used); free(text); ex_zip_close(z); free(data);
}

static int bundle_cb(const char *path, int is_dir, int64_t mtime, void *user) {
    Bundle *b = user;
    const char *name = strrchr(path, '/');
    (void)mtime;
    if (b->c->cancel && *b->c->cancel) { b->c->r->cancelled = 1; return 1; }
    name = name ? name + 1 : path;
    if (is_dir && (ci_eq(ext_of(name), ".textbundle"))) { import_bundle_folder(b->c, path, name); return 0; }
    if (!is_dir && ci_eq(ext_of(name), ".textpack")) import_textpack(b->c, path, name);
    return 0;
}

/* ---- the entry ---- */

int ex_import_run(ExImportFormat f, const char *source, const char *vault_root, const ExImportOptions *o, ExImportReport *r, const volatile int *cancel) {
    Ctx c;
    ExPathInfo info;
    char src[1500];
    size_t n;
    ExImportOptions dflt;
    memset(r, 0, sizeof *r);
    if (!source || !vault_root || (int)f < 0 || f >= EX_IMPORT_FORMAT_COUNT) { snprintf(r->first_problem, sizeof r->first_problem, "Nothing to import."); return -1; }
    memset(&dflt, 0, sizeof dflt);
    if (!o) o = &dflt;
    memset(&c, 0, sizeof c);
    c.o = o; c.r = r; c.cancel = cancel;
    snprintf(c.root, sizeof c.root, "%s", vault_root);
    n = strlen(c.root);
    while (n > 1 && c.root[n - 1] == '/') c.root[--n] = 0;
    {   /* the destination: every part is a valid name; the folder is made when the first file is written */
        const char *fo = o->folder ? o->folder : "Imported", *p = fo;
        size_t w = 0;
        c.dest[0] = 0;
        while (*p) {
            char part[200], clean[200];
            size_t k = 0;
            while (*p == '/') p++;
            while (*p && *p != '/' && k < sizeof part - 1) part[k++] = *p++;
            part[k] = 0;
            if (!k) break;
            if (!strcmp(part, ".") || !strcmp(part, "..")) continue;
            sanitize(part, clean, sizeof clean);
            w += (size_t)snprintf(c.dest + w, sizeof c.dest - w, "%s%s", w ? "/" : "", clean);
        }
    }
    c.tc.now_ms = o->now_ms; c.tc.tz_min = o->tz_min; c.tc.date_format = o->date_format; c.tc.time_format = o->time_format;
    if (o->template_text && o->template_len) {
        c.tpl = malloc(o->template_len + 1);
        if (c.tpl) { memcpy(c.tpl, o->template_text, o->template_len); c.tpl[o->template_len] = 0; c.tpl_len = o->template_len; c.tpl_has_content = strstr(c.tpl, "{{content}}") != NULL || strstr(c.tpl, "{{ content }}") != NULL; }
    }
    snprintf(src, sizeof src, "%s", source);
    n = strlen(src);
    while (n > 1 && src[n - 1] == '/') src[--n] = 0;
    if (ex_platform_path_info(src, &info) != 0) { snprintf(r->first_problem, sizeof r->first_problem, "The source does not exist: %.200s", src); free(c.tpl); return -1; }
    if (f == EX_IMPORT_CSV) {
        size_t len = 0;
        char *text = info.is_file ? read_limited(src, &len) : NULL;
        Csv cs;
        size_t i;
        if (!text) { snprintf(r->first_problem, sizeof r->first_problem, "The CSV file could not be read: %.200s", src); free(c.tpl); return -1; }
        memset(&cs, 0, sizeof cs);
        cs.c = &c; cs.path = src;
        ex_csv_parse(text, len, csv_row, &cs);
        for (i = 0; i < cs.nheads; i++) free(cs.heads[i]);
        free(cs.heads); free(text);
    } else if (f == EX_IMPORT_TEXTBUNDLE) {
        Bundle b;
        b.c = &c; b.base = src; b.baselen = strlen(src);
        if (info.is_file) { const char *nm = strrchr(src, '/'); nm = nm ? nm + 1 : src; if (ci_eq(ext_of(nm), ".textpack")) import_textpack(&c, src, nm); else problem(&c, "Not a Textbundle: %s", src); }
        else if (ci_eq(ext_of(strrchr(src, '/') ? strrchr(src, '/') + 1 : src), ".textbundle")) import_bundle_folder(&c, src, strrchr(src, '/') ? strrchr(src, '/') + 1 : src);
        else ex_platform_walk(src, bundle_cb, &b);
    } else {
        Walk w;
        w.c = &c; w.base = src; w.baselen = strlen(src); w.f = f; w.count_only = 0;
        if (info.is_file) {                                               /* one file: HTML (or any of the folder formats) */
            char *slash = strrchr(src, '/');
            (void)slash;
            w.baselen = slash ? (size_t)(slash - src) : 0;
            walk_cb(src, 0, 0, &w);
        } else ex_platform_walk(src, walk_cb, &w);
    }
    free(c.tpl);
    return 0;
}
