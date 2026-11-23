#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_base_index.h"
#include "exhaust_audio.h"
#include "exhaust_embed.h"
#include "exhaust_mpv.h"
#include "exhaust_pdf.h"
#include "exhaust_image.h"
#include "exhaust_history.h"
#include "exhaust_platform.h"
#include "exhaust_notes.h"
#include "service.h"

static char *dups(const char *s) { size_t n; char *p; if (!s) return NULL; n = strlen(s); p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }

void qrows_free(QRows *r) {
    size_t i;
    for (i = 0; i < r->n; i++) { free(r->v[i].text); free(r->v[i].detail); free(r->v[i].action); free(r->v[i].key); }
    free(r->v);
    memset(r, 0, sizeof *r);
}

size_t qrows_add(QRows *r, const char *text, const char *detail, const char *key, int indent, int glyph, int muted, int bold, int64_t id, size_t aux1, size_t aux2) {
    QRow *q;
    if (r->n == r->cap) { size_t nc = r->cap ? r->cap * 2 : 64; QRow *nv = realloc(r->v, nc * sizeof *nv); if (!nv) return (size_t)-1; r->v = nv; r->cap = nc; }
    q = &r->v[r->n];
    memset(q, 0, sizeof *q);
    q->text = dups(text ? text : ""); q->detail = dups(detail); q->key = dups(key);
    q->indent = indent; q->glyph = glyph; q->muted = muted; q->bold = bold; q->id = id; q->aux1 = aux1; q->aux2 = aux2;
    return r->n++;
}

void qres_free(QRes *r) {
    if (!r) return;
    qrows_free(&r->rows); qrows_free(&r->rows2);
    free(r->text);
    ex_switcher_free(r->snapshot);
    if (r->graph) { ex_graph_free(r->graph); free(r->graph); }
    if (r->base) { ex_base_result_free(r->base); free(r->base); }
    if (r->image) { ex_image_free(r->image); free(r->image); }
    if (r->audio) ex_audio_decoder_close(r->audio);
    if (r->mpv) ex_mpv_close(r->mpv);
    if (r->pdf) ex_pdf_close(r->pdf);
    ex_rename_report_free(&r->report);
    free(r);
}

static void qres_free_payload(void *r) { qres_free((QRes *)r); }

QJob *qjob_new(int kind, unsigned long req) { QJob *j = calloc(1, sizeof *j); if (j) { j->kind = kind; j->req = req; } return j; }

static void qjob_free(void *p) {
    QJob *j = p;
    size_t i;
    if (!j) return;
    free(j->a); free(j->b); free(j->c); free(j->text);
    for (i = 0; i < j->nmoves; i++) { free((char *)j->moves[i].from); free((char *)j->moves[i].to); }
    free(j->moves);
    free(j);
}

struct QService {
    ExWorker *w;
    char *index_path, *root;
    ExIndex *r;
    uint32_t id;
};

/* A line of a note for a list row: blanks squeezed, cut to a length. */
static char *tidy(const char *s) {
    size_t n = strlen(s), i, o = 0;
    char *out = malloc(n + 4);
    int space = 1;
    if (!out) return NULL;
    for (i = 0; i < n && o < 200; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { if (!space) out[o++] = ' '; space = 1; }
        else { out[o++] = (char)c; space = 0; }
    }
    while (o && out[o - 1] == ' ') o--;
    out[o] = 0;
    return out;
}

static const char *base_name(const char *path) { const char *s = strrchr(path, '/'); return s ? s + 1 : path; }

/* "Folder/Note.md" -> "Note" */
static char *title_of(const char *path) {
    const char *b = base_name(path);
    size_t n = strlen(b);
    char *t;
    if (n > 3 && !strcmp(b + n - 3, ".md")) n -= 3;
    t = malloc(n + 1);
    if (t) { memcpy(t, b, n); t[n] = 0; }
    return t;
}

static void do_search(QService *s, QJob *j, QRes *res) {
    char err[200] = "";
    ExSearch *q = ex_search_parse(j->a, err, sizeof err);
    ExSearchResult *r = NULL;
    size_t n = 0, i, k, rows = 0;
    if (!q) { snprintf(res->summary, sizeof res->summary, "%s", err[0] ? err : "The search cannot be read"); return; }
    if (ex_search_run(s->r, s->root, q, &j->so, &r, &n, NULL) != 0) { snprintf(res->summary, sizeof res->summary, "The search failed"); ex_search_free(q); return; }
    for (i = 0; i < n && rows < 6000; i++) {
        char *title = title_of(r[i].path), detail[32];
        char *text;
        size_t len = 0;
        snprintf(detail, sizeof detail, "%zu", r[i].total_matches);
        qrows_add(&res->rows, title ? title : r[i].path, r[i].total_matches ? detail : NULL, r[i].path, 0, EX_GLYPH_CHEVRON_OPEN, 0, 1, (int64_t)r[i].name_match, 0, 0);
        rows++;
        free(title);
        if (!r[i].nmatches) continue;
        text = ex_index_read_note(s->root, r[i].path, &len);
        if (!text) continue;
        for (k = 0; k < r[i].nmatches && k < (j->view.max_matches ? j->view.max_matches : 5); k++) {
            size_t off = 0;
            char *ctx = ex_search_context(text, len, &r[i].matches[k], j->view.more_context ? 1 : 0, &off), *line;
            if (!ctx) continue;
            for (line = ctx; line; ) {
                char *nl = strchr(line, '\n'), *t;
                size_t ll = nl ? (size_t)(nl - line) : strlen(line), ls = (size_t)(line - ctx);
                int hit = off >= ls && off <= ls + ll;
                char *copy = malloc(ll + 1);
                if (copy) {
                    memcpy(copy, line, ll); copy[ll] = 0;
                    t = tidy(copy);
                    qrows_add(&res->rows, t ? t : copy, NULL, r[i].path, 1, EX_GLYPH_NONE, !hit, 0, (int64_t)r[i].matches[k].line, r[i].matches[k].start, r[i].matches[k].end);
                    free(t); free(copy);
                    rows++;
                }
                line = nl ? nl + 1 : NULL;
            }
            free(ctx);
        }
        if (r[i].total_matches > (j->view.max_matches ? j->view.max_matches : 5)) {
            char more[48];
            snprintf(more, sizeof more, "and %zu more", r[i].total_matches - (j->view.max_matches ? j->view.max_matches : 5));
            qrows_add(&res->rows, more, NULL, r[i].path, 1, EX_GLYPH_NONE, 1, 0, 0, r[i].matches[0].start, r[i].matches[0].end);
        }
        free(text);
    }
    res->total = n;
    snprintf(res->summary, sizeof res->summary, "%zu result%s", n, n == 1 ? "" : "s");
    res->ok = 1;
    ex_search_results_free(r, n);
    ex_search_free(q);
}

static void add_links(QService *s, QRows *rows, ExLinkItem *items, size_t n, int outgoing, int unresolved_only) {
    size_t i;
    const char *last = NULL;
    for (i = 0; i < n; i++) {
        const ExLinkItem *it = &items[i];
        char *line = ex_links_line_text(s->root, it->source, it->line), *t, *title;
        if (unresolved_only && it->dest) { free(line); continue; }
        if (!outgoing) {
            if (!last || strcmp(last, it->source)) {
                title = title_of(it->source);
                qrows_add(rows, title, NULL, it->source, 1, EX_GLYPH_CHEVRON_OPEN, 0, 0, 0, 0, 0);
                free(title);
                last = it->source;
            }
            t = line ? tidy(line) : NULL;
            qrows_add(rows, t ? t : "", NULL, it->source, 2, EX_GLYPH_NONE, 1, 0, (int64_t)it->line, it->start, it->end);
            free(t);
        } else {
            char label[400], d[32];
            const char *target = it->dest ? it->dest : it->target;
            title = title_of(target);
            snprintf(label, sizeof label, "%s%s%s%s", title ? title : target, it->heading ? " > " : "", it->heading ? it->heading : "", it->block ? " > ^" : "");
            if (it->block) strncat(label, it->block, sizeof label - strlen(label) - 1);
            snprintf(d, sizeof d, "line %u", it->line);
            qrows_add(rows, label, d, it->dest ? it->dest : it->target, 0, EX_GLYPH_NONE, it->dest == NULL, 0, it->dest ? 1 : 0, it->start, it->end);
            free(title);
        }
        free(line);
    }
}

static void do_backlinks(QService *s, QJob *j, QRes *res) {
    ExLinkItem *items = NULL;
    size_t n = 0;
    if (ex_links_backlinks(s->r, j->a, &items, &n) != 0) { snprintf(res->summary, sizeof res->summary, "The links cannot be read"); return; }
    add_links(s, &res->rows, items, n, 0, 0);
    ex_link_items_free(items, n);
    res->total = n;
    snprintf(res->summary, sizeof res->summary, "%zu linked mention%s", n, n == 1 ? "" : "s");
    res->ok = 1;
}

static void do_unlinked(QService *s, QJob *j, QRes *res) {
    ExMention *m = NULL;
    size_t n = 0, i;
    const char *last = NULL;
    if (ex_links_unlinked(s->r, s->root, j->a, &m, &n) != 0) { snprintf(res->summary, sizeof res->summary, "The mentions cannot be read"); return; }
    for (i = 0; i < n; i++) {
        char *line = ex_links_line_text(s->root, m[i].source, m[i].line), *t = line ? tidy(line) : NULL, *title;
        if (!last || strcmp(last, m[i].source)) { title = title_of(m[i].source); qrows_add(&res->rows, title, NULL, m[i].source, 1, EX_GLYPH_CHEVRON_OPEN, 0, 0, 0, 0, 0); free(title); last = m[i].source; }
        qrows_add(&res->rows, t ? t : "", NULL, m[i].source, 2, EX_GLYPH_NONE, 1, 0, (int64_t)m[i].line, m[i].start, m[i].end);
        res->rows.v[res->rows.n - 1].action = dups("Link");
        free(t); free(line);
    }
    res->total = n;
    snprintf(res->summary, sizeof res->summary, "%zu unlinked mention%s", n, n == 1 ? "" : "s");
    res->ok = 1;
    ex_mentions_free(m, n);
}

static void do_outgoing(QService *s, QJob *j, QRes *res) {
    ExLinkItem *items = NULL;
    size_t n = 0;
    if (ex_links_outgoing(s->r, j->a, &items, &n) != 0) { snprintf(res->summary, sizeof res->summary, "The links cannot be read"); return; }
    {
        size_t i;
        for (i = 0; i < n; i++) if (items[i].kind != EX_LINK_WIKI && items[i].kind != EX_LINK_EMBED && items[i].kind != EX_LINK_PROP && items[i].kind != EX_LINK_MD && items[i].kind != EX_LINK_MD_EMBED) {}
    }
    add_links(s, &res->rows, items, n, 1, 0);
    ex_link_items_free(items, n);
    res->total = n;
    snprintf(res->summary, sizeof res->summary, "%zu link%s", n, n == 1 ? "" : "s");
    res->ok = 1;
}

/* Tags as a tree: every prefix of a nested tag is a row, with the number of notes that use the tag or one below it. */
static void do_tags(QService *s, QRes *res) {
    ExTagInfo *t = NULL;
    size_t n = 0, i, total = 0;
    if (ex_tags_all(s->r, &t, &n) != 0) { snprintf(res->summary, sizeof res->summary, "The tags cannot be read"); return; }
    for (i = 0; i < n; i++) {
        const char *tag = t[i].tag;
        size_t len = strlen(tag), k;
        int depth = 0;
        for (k = 0; k <= len; k++) {
            if (tag[k] == '/' || tag[k] == 0) {
                char *prefix = malloc(k + 1), *name;
                char **paths = NULL;
                size_t np = 0;
                const char *seg = tag + k;
                int is_new = 1;
                size_t j;
                if (!prefix) break;
                memcpy(prefix, tag, k); prefix[k] = 0;
                for (j = 0; j < res->rows.n; j++) if (res->rows.v[j].key && !strcmp(res->rows.v[j].key, prefix)) { is_new = 0; break; }
                if (is_new) {
                    char d[32];
                    size_t b = k;
                    while (b > 0 && tag[b - 1] != '/') b--;
                    name = malloc(k - b + 2);
                    if (name) { name[0] = '#'; memcpy(name + 1, tag + b, k - b); name[k - b + 1] = 0; }
                    if (ex_tag_notes(s->r, prefix, 1, &paths, &np) != 0) np = 0;
                    snprintf(d, sizeof d, "%zu", np);
                    qrows_add(&res->rows, name ? name : prefix, d, prefix, depth, EX_GLYPH_NONE, 0, 0, (int64_t)np, 0, 0);
                    free(name);
                    ex_paths_free(paths, np);
                }
                (void)seg;
                free(prefix);
                depth++;
            }
        }
        total++;
    }
    ex_tags_free(t, n);
    res->total = total;
    snprintf(res->summary, sizeof res->summary, "%zu tag%s", total, total == 1 ? "" : "s");
    res->ok = 1;
}

static void do_props(QService *s, QRes *res) {
    ExPropInfo *p = NULL;
    size_t n = 0, i;
    if (ex_props_all(s->r, s->root, &p, &n) != 0) { snprintf(res->summary, sizeof res->summary, "The properties cannot be read"); return; }
    for (i = 0; i < n; i++) {
        char d[64];
        snprintf(d, sizeof d, "%s, %ld", ex_prop_type_name(p[i].type), p[i].notes);
        qrows_add(&res->rows, p[i].name, d, p[i].name, 0, EX_GLYPH_NONE, 0, 0, p[i].type, (size_t)p[i].notes, 0);
    }
    ex_props_all_free(p, n);
    res->total = n;
    snprintf(res->summary, sizeof res->summary, "%zu propert%s", n, n == 1 ? "y" : "ies");
    res->ok = 1;
}

static void do_outline(QJob *j, QRes *res) {
    ExOutlineItem *it = NULL;
    size_t n = 0, i;
    if (ex_outline(j->text, j->len, &it, &n) != 0) return;
    for (i = 0; i < n; i++) qrows_add(&res->rows, it[i].text, NULL, j->a, it[i].level - 1, EX_GLYPH_NONE, 0, it[i].level == 1, (int64_t)i, it[i].start, it[i].section_end);
    ex_outline_free(it, n);
    res->total = n;
    snprintf(res->summary, sizeof res->summary, "%zu heading%s", n, n == 1 ? "" : "s");
    res->ok = 1;
}

/* The properties of the open note, from its text (the buffer), so a change shows at once. */
static void do_fileprops(QJob *j, QRes *res) {
    ExExtract *x = ex_extract(j->text ? j->text : "", j->len);
    size_t i;
    if (!x) return;
    for (i = 0; i < x->nprops; i++) {
        char d[400], *nl;
        snprintf(d, sizeof d, "%s", x->props[i].value ? x->props[i].value : "");
        for (nl = d; *nl; nl++) if (*nl == '\n') *nl = ',';
        qrows_add(&res->rows, x->props[i].name, d, x->props[i].name, 0, EX_GLYPH_NONE, 0, 0, x->props[i].type, x->props[i].start, x->props[i].end);
    }
    res->total = x->nprops;
    if (!x->front_valid && x->front_end > x->front_start) snprintf(res->summary, sizeof res->summary, "The front matter is not valid YAML; it is left alone");
    else snprintf(res->summary, sizeof res->summary, "%zu propert%s", x->nprops, x->nprops == 1 ? "y" : "ies");
    ex_extract_free(x);
    res->ok = 1;
}

static void do_footnotes(QJob *j, QRes *res) {
    ExFootnote *f = NULL;
    size_t n = 0, i;
    if (ex_footnotes(j->text, j->len, &f, &n) != 0) return;
    for (i = 0; i < n; i++) {
        char label[300], *t = f[i].text ? tidy(f[i].text) : NULL;
        if (f[i].number) snprintf(label, sizeof label, "%d  %s", f[i].number, t ? t : "(no definition)");
        else snprintf(label, sizeof label, "-  %s  [%s, not used]", t ? t : "", f[i].id);
        qrows_add(&res->rows, label, f[i].inline_note ? "inline" : f[i].id, j->a, 0, EX_GLYPH_NONE, f[i].text == NULL || !f[i].number, 0, f[i].number, f[i].def_end > f[i].def_start ? f[i].def_start : f[i].first_ref, f[i].first_ref);
        free(t);
    }
    ex_footnotes_free(f, n);
    res->total = n;
    snprintf(res->summary, sizeof res->summary, "%zu footnote%s", n, n == 1 ? "" : "s");
    res->ok = 1;
}

/* "Link" on an unlinked mention: the mention becomes [[Note]] (or [[Note|text]] when it was written another way). */
static void do_linkit(QService *s, QJob *j, QRes *res) {
    ExNoteIO io = ex_note_io_files(s->root);
    size_t len = 0, olen;
    char *text = io.read(io.user, j->a, &len), *out;
    size_t at = (size_t)j->i1, end = (size_t)j->i2;
    if (text && at < end && end <= len) {
        char *shown = malloc(end - at + 1), *ins;
        size_t tl;
        const char *target = j->b;
        memcpy(shown, text + at, end - at); shown[end - at] = 0;
        ins = malloc(strlen(target) + strlen(shown) + 8);
        if (ins) {
            if (!strcmp(shown, target)) snprintf(ins, strlen(target) + strlen(shown) + 8, "[[%s]]", target);
            else snprintf(ins, strlen(target) + strlen(shown) + 8, "[[%s|%s]]", target, shown);
            tl = strlen(ins);
            out = malloc(len - (end - at) + tl + 1);
            if (out) {
                memcpy(out, text, at); memcpy(out + at, ins, tl); memcpy(out + at + tl, text + end, len - end);
                olen = len - (end - at) + tl; out[olen] = 0;
                if (io.write(io.user, j->a, out, olen) == 0) { res->ok = 1; res->changed = 1; }
                free(out);
            }
            free(ins);
        }
        free(shown);
    }
    free(text);
    ex_note_io_free(&io);
    snprintf(res->summary, sizeof res->summary, res->ok ? "Linked" : "Could not link the mention");
}

static void do_rewrite(QService *s, QJob *j, QRes *res, int write) {
    ExNoteIO io = ex_note_io_files(s->root);
    int rc = write ? ex_rename_links(s->r, &io, j->moves, j->nmoves, &res->report) : ex_rename_plan(s->r, &io, j->moves, j->nmoves, &res->report);
    ex_note_io_free(&io);
    res->ok = rc == 0;
    res->total = res->report.total_links;
    res->changed = res->report.n;
    snprintf(res->summary, sizeof res->summary, "%zu link%s in %zu note%s", res->report.total_links, res->report.total_links == 1 ? "" : "s", res->report.n, res->report.n == 1 ? "" : "s");
}

static void do_propop(QService *s, QJob *j, QRes *res) {
    ExNoteIO io = ex_note_io_files(s->root);
    size_t changed = 0;
    int rc = j->i1 == 0 ? ex_props_rename_everywhere(s->r, &io, s->root, j->a, j->b, &changed) : ex_props_retype_everywhere(s->r, &io, s->root, j->a, j->i2, &changed);
    ex_note_io_free(&io);
    res->ok = rc == 0;
    res->changed = changed;
    snprintf(res->summary, sizeof res->summary, "%zu note%s changed", changed, changed == 1 ? "" : "s");
}

static void do_expand(QService *s, QJob *j, QRes *res) {
    ExSearchOptions so;
    ExResultView view;
    memset(&so, 0, sizeof so); memset(&view, 0, sizeof view);
    res->text = ex_query_expand(s->r, s->root, j->text, j->len, &so, &view);
    if (res->text) {                                                                     /* the bases a note holds (6b-ii) */
        size_t bl = 0;
        char *with_bases = ex_base_expand(s->r, s->root, j->a ? j->a : "", res->text, strlen(res->text), ex_platform_tz_offset_minutes(), (double)ex_platform_unix_time() * 1000.0, &bl);
        if (with_bases) { free(res->text); res->text = with_bases; }
        {   /* the notes a note embeds (6c-v) */
            size_t el = 0;
            char *with_embeds = ex_embed_expand(s->r, s->root, j->a ? j->a : "", res->text, strlen(res->text), &el);
            if (with_embeds) { free(res->text); res->text = with_embeds; }
        }
    }
    snprintf(res->summary, sizeof res->summary, "%s", j->a ? j->a : "");
    res->ok = res->text != NULL;
}

static int strcasecmp_ascii(const char *a, const char *b) {
    for (; *a && *b; a++, b++) { char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a, y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b; if (x != y) return x < y ? -1 : 1; }
    return *a ? 1 : *b ? -1 : 0;
}

/* A PDF read and opened by PDFium on the document's own thread (6c-iv). */
static void do_pdf(QService *s, QJob *j, QRes *res) {
    char *abs, err[300] = "";
    ExPathInfo pi;
    size_t len = 0;
    uint8_t *bytes;
    const char *rel = j->a ? j->a : "";
    ExPdf *d;
    qrows_add(&res->rows, rel, NULL, NULL, 0, 0, 0, 0, 0, 0, 0);
    res->ok = 0;
    if (strstr(rel, "..") && (strstr(rel, "../") || strstr(rel, "/..") || !strcmp(rel, ".."))) { snprintf(res->summary, sizeof res->summary, "This path is outside the vault."); return; }
    if (!ex_pdf_loaded() && !ex_pdf_load(NULL, err, sizeof err)) { snprintf(res->summary, sizeof res->summary, "%.200s", err); return; }
    abs = malloc(strlen(s->root) + strlen(rel) + 2);
    if (!abs) return;
    sprintf(abs, "%s/%s", s->root, rel);
    if (ex_platform_path_info(abs, &pi) != 0 || !pi.is_file) { snprintf(res->summary, sizeof res->summary, "This file does not exist any more."); free(abs); return; }
    if (pi.size > (int64_t)512 * 1024 * 1024) { snprintf(res->summary, sizeof res->summary, "This PDF is too big (over 512 MB)."); free(abs); return; }
    bytes = (uint8_t *)ex_platform_read_file(abs, &len);
    free(abs);
    if (!bytes) { snprintf(res->summary, sizeof res->summary, "This file cannot be read."); return; }
    d = ex_pdf_open(bytes, len, err, sizeof err);                              /* takes the bytes */
    if (!d) { snprintf(res->summary, sizeof res->summary, "%.200s", err[0] ? err : "This PDF cannot be opened."); if (res->summary[0] >= 'a' && res->summary[0] <= 'z') res->summary[0] = (char)(res->summary[0] - 32); return; }
    res->pdf = d; res->ok = 1;
}

/* A media file opened by libmpv (6c-iii): only its path is passed on, libmpv reads the file. */
static void do_mpv(QService *s, QJob *j, QRes *res) {
    char *abs, err[300] = "";
    ExPathInfo pi;
    const char *rel = j->a ? j->a : "";
    ExMpv *m;
    qrows_add(&res->rows, rel, NULL, NULL, 0, 0, 0, 0, 0, 0, 0);
    res->ok = 0;
    if (strstr(rel, "..") && (strstr(rel, "../") || strstr(rel, "/..") || !strcmp(rel, ".."))) { snprintf(res->summary, sizeof res->summary, "This path is outside the vault."); return; }
    abs = malloc(strlen(s->root) + strlen(rel) + 2);
    if (!abs) return;
    sprintf(abs, "%s/%s", s->root, rel);
    if (ex_platform_path_info(abs, &pi) != 0 || !pi.is_file) { snprintf(res->summary, sizeof res->summary, "This file does not exist any more."); free(abs); return; }
    m = ex_mpv_open(abs, j->i1, err, sizeof err);
    free(abs);
    if (!m) { snprintf(res->summary, sizeof res->summary, "%.200s", err[0] ? err : "This file cannot be played."); return; }
    res->mpv = m; res->ok = 1;
}

/* An audio file read, its decoder opened, and the player's output device opened (6c-ii). */
static void do_audio(QService *s, QJob *j, QRes *res) {
    char *abs = NULL, err[200] = "";
    ExPathInfo pi;
    size_t len = 0;
    uint8_t *bytes;
    const char *rel = j->a ? j->a : "";
    ExAudioDecoder *d;
    qrows_add(&res->rows, rel, NULL, NULL, 0, 0, 0, 0, 0, 0, 0);
    res->ok = 0;
    if (strstr(rel, "..") && (strstr(rel, "../") || strstr(rel, "/..") || !strcmp(rel, ".."))) { snprintf(res->summary, sizeof res->summary, "This path is outside the vault."); return; }
    abs = malloc(strlen(s->root) + strlen(rel) + 2);
    if (!abs) return;
    sprintf(abs, "%s/%s", s->root, rel);
    if (ex_platform_path_info(abs, &pi) != 0 || !pi.is_file) { snprintf(res->summary, sizeof res->summary, "This file does not exist any more."); free(abs); return; }
    if (pi.size > (int64_t)EX_AUDIO_MAX_FILE) { snprintf(res->summary, sizeof res->summary, "This audio file is too big (over 256 MB)."); free(abs); return; }
    bytes = (uint8_t *)ex_platform_read_file(abs, &len);
    free(abs);
    if (!bytes) { snprintf(res->summary, sizeof res->summary, "This file cannot be read."); return; }
    d = ex_audio_decoder_open(bytes, len, err, sizeof err);                   /* takes the bytes */
    if (!d) {
        const char *dot = strrchr(rel, '.');
        if (dot && (!strcasecmp_ascii(dot, ".m4a") || !strcasecmp_ascii(dot, ".webm") || !strcasecmp_ascii(dot, ".3gp")))
            snprintf(res->summary, sizeof res->summary, "This audio format is played by the media add-on (libmpv), which is not installed.");
        else { snprintf(res->summary, sizeof res->summary, "%s", err[0] ? err : "This audio file cannot be played."); if (res->summary[0] >= 'a' && res->summary[0] <= 'z') res->summary[0] = (char)(res->summary[0] - 32); }
        return;
    }
    if (j->ptr && ex_player_prepare((ExPlayer *)j->ptr, err, sizeof err) != 0) { ex_audio_decoder_close(d); snprintf(res->summary, sizeof res->summary, "No sound output: %s", err); return; }
    res->audio = d; res->ok = 1;
}

/* A picture read and decoded (6c-i): the file's bytes, then pixels no bigger than 4096 on a side. */
static void do_image(QService *s, QJob *j, QRes *res) {
    char *abs = NULL, err[200] = "";
    ExPathInfo pi;
    size_t len = 0;
    uint8_t *bytes;
    ExImage *im;
    const char *rel = j->a ? j->a : "";
    qrows_add(&res->rows, rel, NULL, NULL, 0, 0, 0, 0, 0, 0, 0);
    res->total = (size_t)(j->i1 > 0 ? j->i1 : 0);
    res->ok = 0;
    if (strstr(rel, "..") && (strstr(rel, "../") || strstr(rel, "/..") || !strcmp(rel, ".."))) { snprintf(res->summary, sizeof res->summary, "This path is outside the vault."); return; }
    abs = malloc(strlen(s->root) + strlen(rel) + 2);
    if (!abs) return;
    sprintf(abs, "%s/%s", s->root, rel);
    if (ex_platform_path_info(abs, &pi) != 0 || !pi.is_file) { snprintf(res->summary, sizeof res->summary, "This file does not exist any more."); free(abs); return; }
    if (pi.size > (int64_t)64 * 1024 * 1024) { snprintf(res->summary, sizeof res->summary, "This picture file is too big (over 64 MB)."); free(abs); return; }
    bytes = (uint8_t *)ex_platform_read_file(abs, &len);
    free(abs);
    if (!bytes) { snprintf(res->summary, sizeof res->summary, "This file cannot be read."); return; }
    im = calloc(1, sizeof *im);
    if (im && ex_image_decode(bytes, len, 4096, j->i1, im, err, sizeof err) == 0) { res->image = im; res->ok = 1; }
    else { free(im); snprintf(res->summary, sizeof res->summary, "%s", err[0] ? err : "This picture cannot be opened."); }
    free(bytes);
}

/* A base view run over the vault (6b-iii). */
static void do_base(QService *s, QJob *j, QRes *res) {
    char err[200] = "";
    ExBase *b = ex_base_parse(j->text ? j->text : "", j->len, err, sizeof err);
    ExBaseData data;
    ExBaseResult *out;
    res->ok = 0;
    if (!b) { snprintf(res->summary, sizeof res->summary, "%s", err[0] ? err : "This base cannot be read."); return; }
    out = calloc(1, sizeof *out);
    if (out && ex_base_data_load(s->r, &data) == 0) {
        if (ex_base_run(b, (size_t)j->i1, &data, j->a, (double)ex_platform_unix_time() * 1000.0, ex_platform_tz_offset_minutes(), out) == 0) { res->base = out; out = NULL; res->ok = 1; }
        else snprintf(res->summary, sizeof res->summary, "This view could not be run.");
        ex_base_data_free(&data);
    } else snprintf(res->summary, sizeof res->summary, "The index is not ready.");
    free(out);
    ex_base_free(b);
}

/* File recovery (7e): the snapshots of a note, one snapshot with its changes, or clearing the history. No index needed. */
static size_t differing_lines(const char *a, const char *b) {
    char *d = ex_diff_unified(a, b, "a", "b"), *line;
    size_t n = 0;
    if (!d) return 0;
    for (line = d; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL)
        if ((line[0] == '+' || line[0] == '-') && strncmp(line, "+++", 3) != 0 && strncmp(line, "---", 3) != 0) n++;
    free(d);
    return n;
}

/* A snapshot's text in editing form (no byte-order mark, LF line ends), as the buffer holds it; NULL if it is not text we can compare and restore. */
static char *snapshot_text(const char *dir, const char *file, size_t *len) {
    size_t raw_len = 0;
    char *raw = ex_history_read(dir, file, &raw_len), *text = NULL;
    ExNoteFormat fmt;
    if (!raw) return NULL;
    if (ex_note_decode((const uint8_t *)raw, raw_len, &text, len, &fmt) != 0) text = NULL;
    free(raw);
    if (text && strlen(text) != *len) { free(text); text = NULL; }            /* a NUL in the text: it could not be put back whole */
    return text;
}

static void do_history(QJob *j, QRes *res) {
    const char *dir = j->b ? j->b : "", *now_text = j->text ? j->text : "";
    if (j->i1 == 0) {
        ExSnapshot *l = NULL;
        size_t n = ex_history_list(dir, j->a ? j->a : "", &l), i;
        for (i = 0; i < n; i++) {
            char label[300], when[40], size[40];
            size_t sl = 0, diff = 0;
            int known = 0;
            ex_history_format_time(l[i].time, when, sizeof when);
            if (l[i].size < 1024) snprintf(size, sizeof size, "%lld bytes", (long long)l[i].size);
            else if (l[i].size < 1024 * 1024) snprintf(size, sizeof size, "%.1f KB", (double)l[i].size / 1024.0);
            else snprintf(size, sizeof size, "%.1f MB", (double)l[i].size / (1024.0 * 1024.0));
            if (i < 30 && l[i].size < 400000 && j->len < 400000) {          /* how much it differs from now, for the newest ones */
                char *t = snapshot_text(dir, l[i].file, &sl);
                if (t) { diff = differing_lines(t, now_text); known = 1; free(t); }
            }
            if (known && diff == 0) snprintf(label, sizeof label, "%s  ·  %s  ·  same as now%s", when, size, l[i].kind == EX_SNAP_CONFLICT ? "  ·  the other side of a conflict" : "");
            else if (known) snprintf(label, sizeof label, "%s  ·  %s  ·  %zu line%s differ%s", when, size, diff, diff == 1 ? "" : "s", l[i].kind == EX_SNAP_CONFLICT ? "  ·  the other side of a conflict" : "");
            else snprintf(label, sizeof label, "%s  ·  %s%s", when, size, l[i].kind == EX_SNAP_CONFLICT ? "  ·  the other side of a conflict" : "");
            qrows_add(&res->rows, label, NULL, l[i].file, 0, l[i].kind, 0, 0, (int64_t)i, (size_t)l[i].time, (size_t)l[i].size);
        }
        res->total = n;
        ex_history_free(l, n);
        snprintf(res->summary, sizeof res->summary, "%zu earlier version%s", n, n == 1 ? "" : "s");
        res->ok = 1;
    } else if (j->i1 == 1) {
        size_t len = 0;
        char *t = snapshot_text(dir, j->a ? j->a : "", &len);
        if (!t) { snprintf(res->summary, sizeof res->summary, "That version could not be read as text"); return; }
        res->text = ex_diff_unified(t, now_text, "that version", "now");
        qrows_add(&res->rows2, t, NULL, j->a, 0, 0, 0, 0, 0, len, 0);
        free(t);
        res->ok = res->text != NULL;
    } else if (j->i1 == 2) {
        res->changed = ex_history_clear(dir, NULL);
        snprintf(res->summary, sizeof res->summary, "%zu snapshot%s deleted", res->changed, res->changed == 1 ? "" : "s");
        res->ok = 1;
    }
}

static void service_job(ExWorker *self, const ExMessage *m, void *user) {
    QService *s = user;
    QJob *j = m->payload;
    QRes *res;
    char err[200];
    if (!j) return;
    res = calloc(1, sizeof *res);
    if (!res) return;
    res->kind = j->kind; res->req = j->req;
    if (j->kind == QJ_HISTORY) do_history(j, res);
    else if (j->kind == QJ_IMAGE) do_image(s, j, res);
    else if (j->kind == QJ_AUDIO) do_audio(s, j, res);
    else if (j->kind == QJ_MPV) do_mpv(s, j, res);
    else if (j->kind == QJ_PDF) do_pdf(s, j, res);                     /* a picture needs no index */
    else {
    if (!s->r) s->r = ex_index_open(s->index_path, 0, err, sizeof err);
    if (!s->r) snprintf(res->summary, sizeof res->summary, "The index is not ready yet");
    else switch (j->kind) {
    case QJ_SEARCH: do_search(s, j, res); break;
    case QJ_BACKLINKS: do_backlinks(s, j, res); break;
    case QJ_UNLINKED: do_unlinked(s, j, res); break;
    case QJ_OUTGOING: do_outgoing(s, j, res); break;
    case QJ_TAGS: do_tags(s, res); break;
    case QJ_PROPS: do_props(s, res); break;
    case QJ_OUTLINE: do_outline(j, res); break;
    case QJ_FOOTNOTES: do_footnotes(j, res); break;
    case QJ_FILEPROPS: do_fileprops(j, res); break;
    case QJ_PREVIEW: {                       /* a is a note path, or (i1) a link target to resolve from the note in text */
        char *path = NULL;
        if (j->i1) { int64_t id = ex_link_resolve(s->r, j->text ? j->text : "", j->a, EX_LINK_WIKI); path = id ? ex_index_file_path(s->r, id) : NULL; }
        else path = dups(j->a);
        if (path) { res->text = ex_link_preview(s->r, s->root, path, j->b, j->c, 1500); res->ok = res->text != NULL; }
        free(path);
        break;
    }
    case QJ_FILETEXT: {                       /* a is path, path#Heading or path#^block */
        char path[700], *hash;
        const char *heading = NULL, *block = NULL;
        snprintf(path, sizeof path, "%s", j->a ? j->a : "");
        hash = strchr(path, '#');
        if (hash) { *hash++ = 0; if (*hash == '^') block = hash + 1; else if (*hash) heading = hash; }
        res->text = path[0] ? ex_link_preview(s->r, s->root, path, heading, block, 1500) : NULL;
        res->ok = res->text != NULL;
        qrows_add(&res->rows, j->a ? j->a : "", NULL, NULL, 0, 0, 0, 0, 0, 0, 0);
        break;
    }
    case QJ_SNAPSHOT: res->snapshot = ex_switcher_load(s->r, j->include_attachments); res->ok = res->snapshot != NULL; break;
    case QJ_PLAN: do_rewrite(s, j, res, 0); break;
    case QJ_REWRITE: do_rewrite(s, j, res, 1); break;
    case QJ_PROPOP: do_propop(s, j, res); break;
    case QJ_LINKIT: do_linkit(s, j, res); break;
    case QJ_EXPAND: do_expand(s, j, res); break;
    case QJ_BASE: do_base(s, j, res); break;
    case QJ_GRAPH:
        res->graph = calloc(1, sizeof *res->graph);
        if (res->graph && ex_graph_load(s->r, res->graph) == 0 && ex_graph_apply_options(s->r, s->root, &j->gopts, res->graph, NULL, res->summary, sizeof res->summary) == 0) {
            res->ok = 1;
            if (j->i1 > 0 && ex_graph_local(res->graph, j->a ? j->a : "", j->i1) < 0) res->ok = 0;      /* a local graph: the neighbourhood of the open note */
        }
        else { if (res->graph) { ex_graph_free(res->graph); free(res->graph); } res->graph = NULL; }
        break;
    default: break;
    }
    }
    if (j->kind == QJ_OUTLINE || j->kind == QJ_FOOTNOTES || j->kind == QJ_FILEPROPS) { if (!res->ok) res->ok = 1; }
    ex_worker_send(self, EX_MSG_SERVICE, res, sizeof *res, qres_free_payload);
}

QService *qservice_start(const char *index_path, const char *root, ExQueue *results, uint32_t worker_id) {
    QService *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->index_path = dups(index_path); s->root = dups(root); s->id = worker_id;
    s->w = s->index_path && s->root ? ex_worker_start("panels", worker_id, service_job, s, results) : NULL;
    if (!s->w) { free(s->index_path); free(s->root); free(s); return NULL; }
    return s;
}

void qservice_post(QService *s, QJob *job) {
    ExMessage m;
    memset(&m, 0, sizeof m);
    m.type = 1; m.payload = job; m.size = sizeof *job; m.free_payload = qjob_free;
    if (ex_worker_post(s->w, &m) != 0) qjob_free(job);
}

void qservice_stop(QService *s) {
    if (!s) return;
    ex_worker_stop(s->w);
    ex_index_close(s->r);
    free(s->index_path); free(s->root); free(s);
}
