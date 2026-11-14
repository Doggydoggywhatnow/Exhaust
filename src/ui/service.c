#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    ex_rename_report_free(&r->report);
    free(r);
}

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
    snprintf(res->summary, sizeof res->summary, "%s", j->a ? j->a : "");
    res->ok = res->text != NULL;
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
    case QJ_SNAPSHOT: res->snapshot = ex_switcher_load(s->r, j->include_attachments); res->ok = res->snapshot != NULL; break;
    case QJ_PLAN: do_rewrite(s, j, res, 0); break;
    case QJ_REWRITE: do_rewrite(s, j, res, 1); break;
    case QJ_PROPOP: do_propop(s, j, res); break;
    case QJ_LINKIT: do_linkit(s, j, res); break;
    case QJ_EXPAND: do_expand(s, j, res); break;
    default: break;
    }
    if (j->kind == QJ_OUTLINE || j->kind == QJ_FOOTNOTES || j->kind == QJ_FILEPROPS) { if (!res->ok) res->ok = 1; }
    ex_worker_send(self, EX_MSG_SERVICE, res, sizeof *res, (void (*)(void *))qres_free);
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
