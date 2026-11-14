/* The panels of the sidebars: building them, filling them from the service's rows and what happens when a row is used. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_platform.h"
#include "panels_internal.h"

static const struct { const char *id, *title, *label, *feature; int side, auto_chevron; } DEFS[V_COUNT] = {
    {"files", "Files", "Files", "files", 0, 0},
    {"search", "Search", "Search", "search", 0, 0},
    {"bookmarks", "Bookmarks", "Bookmarks", "bookmarks", 0, 0},
    {"backlinks", "Backlinks", "Backlinks", "backlinks", 1, 0},
    {"outgoing", "Outgoing links", "Links", "outgoing_links", 1, 0},
    {"tags", "Tags", "Tags", "tags_view", 1, 1},
    {"properties", "All properties", "Properties", "properties_view", 1, 0},
    {"fileprops", "File properties", "Note properties", "properties_view", 1, 0},
    {"outline", "Outline", "Outline", "outline", 1, 0},
    {"footnotes", "Footnotes", "Notes", "footnotes", 1, 0},
};

static const int STRIP_ICONS[V_COUNT] = {
    EX_ICON_FILES, EX_ICON_SEARCH, EX_ICON_BOOKMARK, EX_ICON_BACKLINKS, EX_ICON_LINKS, EX_ICON_TAGS, EX_ICON_PROPERTIES,
    EX_ICON_FILE_PROPERTIES, EX_ICON_OUTLINE, EX_ICON_FOOTNOTES
};

View *view_by_id(ExPanels *p, const char *id) { int i; for (i = 0; i < V_COUNT; i++) if (!strcmp(p->views[i].id, id)) return &p->views[i]; return NULL; }
static int view_index(ExPanels *p, View *v) { return (int)(v - p->views); }

/* ---- collapsing and filling ---- */

static int is_collapsed(const View *v, const char *key) {
    size_t i;
    for (i = 0; i < v->ncollapsed; i++) if (!strcmp(v->collapsed[i], key)) return 1;
    return 0;
}

static void toggle_collapsed(View *v, const char *key) {
    size_t i;
    for (i = 0; i < v->ncollapsed; i++) if (!strcmp(v->collapsed[i], key)) { free(v->collapsed[i]); v->collapsed[i] = v->collapsed[--v->ncollapsed]; return; }
    { char **nv = realloc(v->collapsed, (v->ncollapsed + 1) * sizeof *nv); if (nv) { v->collapsed = nv; v->collapsed[v->ncollapsed++] = dup_s(key); } }
}

void fill_list(View *v, ExWidget *list, QRows *rows) {
    size_t i;
    int hide = -1;
    float scroll = ex_list_scroll_y(list);
    size_t sel = ex_list_selected(list);
    ex_list_clear(list);
    for (i = 0; i < rows->n; i++) {
        QRow *q = &rows->v[i];
        ExListRow r;
        int has_child = i + 1 < rows->n && rows->v[i + 1].indent > q->indent, glyph = q->glyph;
        const char *ck = q->key && q->key[0] ? q->key : q->text;
        if (hide >= 0 && q->indent > hide) continue;
        hide = -1;
        if (glyph == EX_GLYPH_NONE && v->auto_chevron && has_child) glyph = EX_GLYPH_CHEVRON_OPEN;
        if (glyph >= EX_GLYPH_CHEVRON_CLOSED && glyph <= EX_GLYPH_CHEVRON_OPEN) {
            int closed = is_collapsed(v, ck) || (v->collapse_all && q->indent == 0);
            glyph = closed ? EX_GLYPH_CHEVRON_CLOSED : EX_GLYPH_CHEVRON_OPEN;
            if (closed) hide = q->indent;
        }
        memset(&r, 0, sizeof r);
        r.text = q->text; r.detail = q->detail; r.action = q->action; r.key = q->key;
        r.indent = q->indent; r.glyph = glyph; r.muted = q->muted; r.bold = q->bold; r.id = q->id; r.aux1 = q->aux1; r.aux2 = q->aux2;
        ex_list_add(list, &r);
    }
    ex_list_select(list, sel);
    ex_list_set_scroll_y(list, scroll);
}

static void set_summary(View *v, const char *text) {
    snprintf(v->summary_text, sizeof v->summary_text, "%s", text);
    if (v->summary) ex_widget_set_text(v->summary, v->summary_text);
}

/* ---- list handlers ---- */

typedef struct Ctx { ExPanels *p; View *v; int second; } Ctx;
static Ctx g_ctx[V_COUNT][2];

static const char *row_key(ExWidget *w, size_t row) { const ExListRow *r = ex_list_row(w, row); return r && r->key ? r->key : ""; }

static void on_toggle(ExWidget *w, size_t row, void *user) {
    Ctx *c = user;
    const ExListRow *r = ex_list_row(w, row);
    QRows *rows = c->second ? &c->v->rows2 : &c->v->rows;
    if (!r) return;
    toggle_collapsed(c->v, r->key && r->key[0] ? r->key : r->text);
    fill_list(c->v, w, rows);
}

static void open_row(ExPanels *p, ExWidget *w, size_t row) {
    const ExListRow *r = ex_list_row(w, row);
    if (r && r->key && r->key[0]) panels_open_note(p, r->key, r->indent ? r->aux1 : (size_t)-1, 0);
}

static void on_search_activate(ExWidget *w, size_t row, void *user) { open_row(((Ctx *)user)->p, w, row); }
static void on_backlink_activate(ExWidget *w, size_t row, void *user) { open_row(((Ctx *)user)->p, w, row); }

static void on_link_action(ExWidget *w, size_t row, void *user) {       /* "Link" on an unlinked mention */
    Ctx *c = user;
    const ExListRow *r = ex_list_row(w, row);
    ExPanels *p = c->p;
    QJob *j;
    char name[300];
    if (!r || !r->key || !p->cur_path) return;
    if (p->in.host.flush_notes) p->in.host.flush_notes(p->in.host.user);
    note_title(p->cur_path, name, sizeof name);
    j = qjob_new(QJ_LINKIT, ++p->next_req);
    if (!j) return;
    j->a = dup_s(r->key); j->b = dup_s(name); j->i1 = (int)r->aux1; j->i2 = (int)r->aux2;
    send_job(p, j);
}

static void on_outgoing_activate(ExWidget *w, size_t row, void *user) {
    ExPanels *p = ((Ctx *)user)->p;
    const ExListRow *r = ex_list_row(w, row);
    char path[400];
    if (!r || !r->key) return;
    if (r->id == 1) { panels_open_note(p, r->key, (size_t)-1, 0); return; }
    {   /* an unresolved link: the note is made (it is written once something is typed) */
        size_t n = strlen(r->key);
        snprintf(path, sizeof path, "%s%s", r->key, (n > 3 && !strcmp(r->key + n - 3, ".md")) || strchr(r->key, '.') ? "" : ".md");
        panels_open_note(p, path, (size_t)-1, 0);
    }
}

static void on_tag_activate(ExWidget *w, size_t row, void *user) {
    ExPanels *p = ((Ctx *)user)->p;
    char q[300];
    snprintf(q, sizeof q, "tag:#%s", row_key(w, row));
    ex_panels_search_for(p, q);
}

static void on_select_range(ExWidget *w, size_t row, void *user) {
    ExPanels *p = ((Ctx *)user)->p;
    const ExListRow *r = ex_list_row(w, row);
    if (r && p->in.host.select_range) p->in.host.select_range(p->in.host.user, r->aux1, r->aux1);
}

static void on_outline_move(ExWidget *w, size_t from, size_t to, void *user) {
    ExPanels *p = ((Ctx *)user)->p;
    size_t len = 0, nlen = 0;
    char *text = p->in.host.active_text ? p->in.host.active_text(p->in.host.user, &len) : NULL, *out;
    (void)w;
    if (!text || !p->cur_path) { free(text); return; }
    out = ex_outline_move(text, len, from, to, &nlen);
    if (out) {
        if (p->in.host.replace_text) p->in.host.replace_text(p->in.host.user, p->cur_path, out, nlen);
        p->text_dirty = 1; p->text_seen_at = now_ms() - 1000;
        free(out);
    } else if (p->in.host.message) p->in.host.message(p->in.host.user, "A section cannot be moved into itself.");
    free(text);
}

/* ---- search ---- */

static void run_search_now(ExPanels *p) {
    char *q = field_text_of(p->search_doc);
    View *v = &p->views[V_SEARCH];
    QJob *j;
    p->search_pending = 0;
    ex_ws_set_search_text(p->in.ws, q ? q : "");
    if (!q || !q[0]) { qrows_free(&v->rows); fill_list(v, v->list, &v->rows); set_summary(v, "Type to search. Operators: file: path: tag: line: section: task: [property:value] /regex/"); free(q); return; }
    j = qjob_new(QJ_SEARCH, v->req = ++p->next_req);
    if (j) {
        j->a = q;
        j->so.sort = p->sort; j->so.match_case = p->match_case;
        j->view.more_context = p->more_context;
        send_job(p, j);
    } else free(q);
}

static void sync_search_buttons(ExPanels *p) {
    static const char *const SORTS[] = {"A to Z", "Z to A", "Newest", "Oldest", "Most", "Fewest", "Created new", "Created old"};
    char label[48];
    p->b_case->selected = p->match_case;
    p->b_collapse->selected = p->views[V_SEARCH].collapse_all;
    p->b_context->selected = p->more_context;
    snprintf(label, sizeof label, "%s", SORTS[p->sort]);
    ex_widget_set_text(p->b_sort, label);
}

static void on_b_case(ExWidget *w, void *u) { ExPanels *p = u; (void)w; p->match_case = !p->match_case; sync_search_buttons(p); run_search_now(p); }
static void on_b_context(ExWidget *w, void *u) { ExPanels *p = u; (void)w; p->more_context = !p->more_context; sync_search_buttons(p); run_search_now(p); }
static void on_b_sort(ExWidget *w, void *u) { ExPanels *p = u; (void)w; p->sort = (p->sort + 1) % 8; sync_search_buttons(p); run_search_now(p); }
static void on_b_collapse(ExWidget *w, void *u) {
    ExPanels *p = u;
    View *v = &p->views[V_SEARCH];
    (void)w;
    v->collapse_all = !v->collapse_all;
    sync_search_buttons(p);
    fill_list(v, v->list, &v->rows);
}

static void on_b_copy(ExWidget *w, void *u) {
    ExPanels *p = u;
    View *v = &p->views[V_SEARCH];
    size_t i, cap = 1024, n = 0;
    char *out = malloc(cap);
    (void)w;
    if (!out) return;
    out[0] = 0;
    for (i = 0; i < v->rows.n; i++) {
        const QRow *q = &v->rows.v[i];
        size_t need = strlen(q->text) + strlen(q->key ? q->key : "") + 16;
        if (n + need > cap) { cap = (n + need) * 2; out = realloc(out, cap); if (!out) return; }
        if (q->indent == 0) n += (size_t)sprintf(out + n, "%s[[%s]]\n", n ? "\n" : "", q->key && strlen(q->key) > 3 ? q->key : q->text), out[n - 0] = 0;
        else if (!v->collapse_all) n += (size_t)sprintf(out + n, "    %s\n", q->text);
    }
    { size_t k; for (k = 0; k + 3 < n; k++) if (out[k] == '.' && out[k + 1] == 'm' && out[k + 2] == 'd' && out[k + 3] == ']') { memmove(out + k, out + k + 3, n - k - 3 + 1); n -= 3; } }
    ex_clipboard_set_text(out);
    free(out);
    if (p->in.host.message) p->in.host.message(p->in.host.user, "Search results copied.");
}

static void refresh_saved(ExPanels *p);
static void saved_name_done(ExPanels *p, const char *name, void *arg) {
    char *q = field_text_of(p->search_doc);
    ExSavedQuery s;
    (void)arg;
    memset(&s, 0, sizeof s);
    s.name = (char *)name; s.text = q ? q : (char *)""; s.sort = p->sort; s.collapse = p->views[V_SEARCH].collapse_all; s.more_context = p->more_context; s.match_case = p->match_case;
    if (name[0] && p->saved && ex_saved_queries_put(p->saved, &s) == 0) { ex_saved_queries_save(p->saved); refresh_saved(p); }
    free(q);
}

static void on_b_save(ExWidget *w, void *u) {
    ExPanels *p = u;
    char *q = field_text_of(p->search_doc);
    (void)w;
    if (q && q[0]) dialog_prompt(p, "Save search", "Name", q, saved_name_done, NULL);
    free(q);
}

static void on_b_bookmark_search(ExWidget *w, void *u) {
    ExPanels *p = u;
    char *q = field_text_of(p->search_doc);
    ExBookmark b;
    (void)w;
    if (!q || !q[0] || !p->bookmarks) { free(q); return; }
    memset(&b, 0, sizeof b);
    b.type = EX_BM_SEARCH; b.query = q;
    ex_bookmarks_add(p->bookmarks, &b, 0);
    free(q);
    p->views[V_BOOKMARKS].stale = 1;
    if (p->in.host.message) p->in.host.message(p->in.host.user, "Search bookmarked.");
}

static void load_saved(ExPanels *p, const ExSavedQuery *s) {
    p->sort = s->sort; p->match_case = s->match_case; p->more_context = s->more_context; p->views[V_SEARCH].collapse_all = s->collapse;
    sync_search_buttons(p);
    set_doc_text(p->search_doc, s->text);
    run_search_now(p);
}

static void on_saved_activate(ExWidget *w, size_t row, void *user) {
    ExPanels *p = ((Ctx *)user)->p;
    const ExSavedQuery *s = p->saved ? ex_saved_queries_find(p->saved, ex_list_row(w, row) ? ex_list_row(w, row)->text : "") : NULL;
    if (s) load_saved(p, s);
}

static void on_saved_action(ExWidget *w, size_t row, void *user) {
    ExPanels *p = ((Ctx *)user)->p;
    const ExListRow *r = ex_list_row(w, row);
    if (r && p->saved && ex_saved_queries_remove(p->saved, r->text) == 0) { ex_saved_queries_save(p->saved); refresh_saved(p); }
}

static int search_key(ExWidget *w, ExKey key, int mods, void *u) {
    ExPanels *p = u;
    (void)w; (void)mods;
    if (key == EX_KEY_ENTER) { run_search_now(p); return 1; }
    if (key == EX_KEY_DOWN) { ex_ui_set_focus(p->ui, p->views[V_SEARCH].list); return 1; }
    return 0;
}

static void refresh_saved(ExPanels *p) {
    View *v = &p->views[V_SEARCH];
    size_t i;
    ex_list_clear(v->list2);
    for (i = 0; p->saved && i < ex_saved_queries_count(p->saved); i++) {
        ExListRow r;
        const ExSavedQuery *s = ex_saved_queries_get(p->saved, i);
        memset(&r, 0, sizeof r);
        r.text = s->name; r.action = "Delete"; r.detail = s->text;
        ex_list_add(v->list2, &r);
    }
    v->extra->hidden = !p->saved || ex_saved_queries_count(p->saved) == 0;
    { size_t n = p->saved ? ex_saved_queries_count(p->saved) : 0; v->list2->height = ex_size_px(p->ui, 29.0f * (float)(n > 4 ? 4 : n) + 4); v->list2->hidden = n == 0; }
}

/* ---- bookmarks ---- */

static size_t add_bm_rows(ExPanels *p, View *v, int parent, int depth) {
    size_t i, n = 0;
    for (i = 0; i < ex_bookmarks_count(p->bookmarks); i++) {
        const ExBookmark *b = ex_bookmarks_at(p->bookmarks, i);
        char key[32], detail[80];
        static const char *const KIND[] = {"note", "heading", "block", "search", "graph", "group"};
        if (b->parent != parent) continue;
        snprintf(key, sizeof key, "bm%d", b->id);
        snprintf(detail, sizeof detail, "%s", KIND[b->type]);
        qrows_add(&v->rows, b->title, b->type == EX_BM_GROUP ? NULL : detail, b->type == EX_BM_GROUP ? key : (b->path ? b->path : key), depth, b->type == EX_BM_GROUP ? EX_GLYPH_CHEVRON_OPEN : EX_GLYPH_NONE, b->type == EX_BM_GRAPH, b->type == EX_BM_GROUP, b->id, 0, 0);
        n++;
        if (b->type == EX_BM_GROUP) {
            /* a group's key must be unique and stable for the collapse state: bm<id> */
            n += add_bm_rows(p, v, b->id, depth + 1);
        }
    }
    return n;
}

static void fill_bookmarks(ExPanels *p) {
    View *v = &p->views[V_BOOKMARKS];
    char s[64];
    size_t n;
    qrows_free(&v->rows);
    n = p->bookmarks ? add_bm_rows(p, v, 0, 0) : 0;
    fill_list(v, v->list, &v->rows);
    snprintf(s, sizeof s, "%zu bookmark%s", p->bookmarks ? ex_bookmarks_count(p->bookmarks) : 0, p->bookmarks && ex_bookmarks_count(p->bookmarks) == 1 ? "" : "s");
    set_summary(v, s);
    (void)n;
}

static size_t offset_of_heading(ExPanels *p, const char *path, const char *heading) {
    size_t len = 0, i, n = 0;
    char *text = p->in.host.read_text ? p->in.host.read_text(p->in.host.user, path, &len) : NULL;
    ExOutlineItem *it;
    size_t at = (size_t)-1;
    if (!text) return at;
    if (ex_outline(text, len, &it, &n) == 0) {
        for (i = 0; i < n; i++) if (!strcmp(it[i].text, heading)) { at = it[i].start; break; }
        ex_outline_free(it, n);
    }
    free(text);
    return at;
}

static size_t offset_of_block(ExPanels *p, const char *path, const char *id) {
    size_t len = 0, at = (size_t)-1, i, il = strlen(id);
    char *text = p->in.host.read_text ? p->in.host.read_text(p->in.host.user, path, &len) : NULL;
    if (!text) return at;
    for (i = 0; i + il + 1 <= len; i++) if (text[i] == '^' && !strncmp(text + i + 1, id, il) && (i + 1 + il == len || text[i + 1 + il] == '\n')) { at = i; break; }
    free(text);
    return at;
}

static void on_bm_activate(ExWidget *w, size_t row, void *user) {
    ExPanels *p = ((Ctx *)user)->p;
    const ExListRow *r = ex_list_row(w, row);
    const ExBookmark *b = r ? ex_bookmarks_find(p->bookmarks, (int)r->id) : NULL;
    if (!b) return;
    switch (b->type) {
    case EX_BM_NOTE: panels_open_note(p, b->path, (size_t)-1, 0); break;
    case EX_BM_HEADING: panels_open_note(p, b->path, offset_of_heading(p, b->path, b->heading ? b->heading : ""), 0); break;
    case EX_BM_BLOCK: panels_open_note(p, b->path, offset_of_block(p, b->path, b->block ? b->block : ""), 0); break;
    case EX_BM_SEARCH: ex_panels_search_for(p, b->query ? b->query : ""); break;
    case EX_BM_GRAPH: if (p->in.host.message) p->in.host.message(p->in.host.user, "The graph view arrives in a later sub-slice, so this bookmark cannot be opened yet."); break;
    default: on_toggle(w, row, user); break;
    }
}

static void refresh_saved(ExPanels *p);
static int selected_bm(ExPanels *p) {
    View *v = &p->views[V_BOOKMARKS];
    const ExListRow *r = ex_list_row(v->list, ex_list_selected(v->list));
    return r ? (int)r->id : 0;
}

static void on_bm_move(ExWidget *w, size_t from, size_t to, void *user) {
    ExPanels *p = ((Ctx *)user)->p;
    const ExListRow *a = ex_list_row(w, from), *b = ex_list_row(w, to);
    int parent = 0, before = 0;
    if (!a) return;
    if (b) { const ExBookmark *bb = ex_bookmarks_find(p->bookmarks, (int)b->id); if (bb) { parent = bb->parent; before = bb->id; } }
    if (ex_bookmarks_move(p->bookmarks, (int)a->id, parent, before) == 0) fill_bookmarks(p);
}

static void on_bm_add(ExWidget *w, void *u) {
    ExPanels *p = u;
    ExBookmark b;
    int sel = selected_bm(p), parent = 0;
    const ExBookmark *s = sel ? ex_bookmarks_find(p->bookmarks, sel) : NULL;
    const char *path = p->in.host.active_path ? p->in.host.active_path(p->in.host.user) : NULL;
    (void)w;
    if (s && s->type == EX_BM_GROUP) parent = s->id; else if (s) parent = s->parent;
    if (!path) return;
    memset(&b, 0, sizeof b);
    b.type = EX_BM_NOTE; b.path = (char *)path;
    ex_bookmarks_add(p->bookmarks, &b, parent);
    fill_bookmarks(p);
}

static void group_done(ExPanels *p, const char *name, void *arg) {
    ExBookmark b;
    (void)arg;
    if (!name[0]) return;
    memset(&b, 0, sizeof b);
    b.type = EX_BM_GROUP; b.title = (char *)name;
    ex_bookmarks_add(p->bookmarks, &b, 0);
    fill_bookmarks(p);
}
static void on_bm_group(ExWidget *w, void *u) { (void)w; dialog_prompt(u, "New group", "Name", "", group_done, NULL); }

static void rename_bm_done(ExPanels *p, const char *name, void *arg) {
    if (name[0]) ex_bookmarks_rename(p->bookmarks, (int)(intptr_t)arg, name);
    fill_bookmarks(p);
}
static void on_bm_rename(ExWidget *w, void *u) {
    ExPanels *p = u;
    int id = selected_bm(p);
    const ExBookmark *b = id ? ex_bookmarks_find(p->bookmarks, id) : NULL;
    (void)w;
    if (b) dialog_prompt(p, "Rename bookmark", "Name", b->title, rename_bm_done, (void *)(intptr_t)id);
}
static void on_bm_remove(ExWidget *w, void *u) {
    ExPanels *p = u;
    int id = selected_bm(p);
    (void)w;
    if (id && ex_bookmarks_remove(p->bookmarks, id) == 0) fill_bookmarks(p);
}

/* ---- properties ---- */

static void propop_rename_done(ExPanels *p, const char *name, void *arg) {
    QJob *j;
    if (!name[0] || !arg) return;
    if (p->in.host.flush_notes) p->in.host.flush_notes(p->in.host.user);
    j = qjob_new(QJ_PROPOP, p->req_propop = ++p->next_req);
    if (!j) return;
    j->a = dup_s((const char *)arg); j->b = dup_s(name); j->i1 = 0;
    send_job(p, j);
}

static const char *selected_prop(ExPanels *p) { View *v = &p->views[V_PROPS]; return row_key(v->list, ex_list_selected(v->list)); }

static void on_prop_rename(ExWidget *w, void *u) {
    ExPanels *p = u;
    const char *name = selected_prop(p);
    (void)w;
    free(p->dialog_arg_text);
    p->dialog_arg_text = dup_s(name);
    if (name[0]) dialog_prompt(p, "Rename property in every note", "New name", name, propop_rename_done, p->dialog_arg_text);
}

static void type_done(ExPanels *p, size_t idx, const char *key, void *arg) {
    QJob *j;
    (void)key;
    if (!arg) return;
    if (p->in.host.flush_notes) p->in.host.flush_notes(p->in.host.user);
    j = qjob_new(QJ_PROPOP, p->req_propop = ++p->next_req);
    if (!j) return;
    j->a = dup_s((const char *)arg); j->i1 = 1; j->i2 = (int)idx;
    send_job(p, j);
}

static void on_prop_type(ExWidget *w, void *u) {
    ExPanels *p = u;
    static const char *labels[] = {"Text", "List", "Number", "Checkbox", "Date", "Date and time", "Links"};
    const char *name = selected_prop(p);
    (void)w;
    free(p->dialog_arg_text);
    p->dialog_arg_text = dup_s(name);
    if (name[0]) dialog_choice(p, "Change type in every note", labels, 7, 0, type_done, p->dialog_arg_text);
}

/* ---- backlinks ---- */

static void on_b_unlinked(ExWidget *w, void *u) {
    ExPanels *p = u;
    View *v = &p->views[V_BACKLINKS];
    (void)w;
    p->show_unlinked = !p->show_unlinked;
    v->list2->hidden = !p->show_unlinked;
    v->extra->selected = p->show_unlinked;
    v->stale = 1;
}

/* ---- building ---- */

static ExWidget *small(ExWidget *b) { if (b) { b->text_scale = 0.85f; b->pad_x = 0.9f; b->pad_y = 0.4f; } return b; }

static ExWidget *vbox(ExPanels *p, const char *name) { ExWidget *w = ex_widget_box(p->ui, 1, name); w->width = ex_size_grow(); w->height = ex_size_grow(); return w; }
static ExWidget *hbox(ExPanels *p, const char *name) { ExWidget *w = ex_widget_box(p->ui, 0, name); w->width = ex_size_grow(); w->gap = 0.5f; return w; }

static ExWidget *summary_label(ExPanels *p, const char *text) {
    ExWidget *l = ex_widget_label(p->ui, text);
    l->text_token = EX_COLOR_TEXT_MUTED; l->text_scale = 0.85f; l->pad_x = 1; l->pad_y = 0.5f; l->width = ex_size_grow();
    return l;
}

static void setup_list(ExPanels *p, View *v, ExWidget *list, int second, const ExListHandlers *h) {
    ExListHandlers hh = *h;
    g_ctx[view_index(p, v)][second].p = p; g_ctx[view_index(p, v)][second].v = v; g_ctx[view_index(p, v)][second].second = second;
    hh.user = &g_ctx[view_index(p, v)][second];
    if (!hh.toggle) hh.toggle = on_toggle;
    ex_list_set_handlers(list, &hh);
}

static void fileprops_activate(ExWidget *w, size_t row, void *user) { Ctx *c = user; (void)w; (void)row; fileprops_on_edit(NULL, c->p); }

static void on_strip_click(ExWidget *w, void *u) {
    ExPanels *p = u;
    int i;
    for (i = 0; i < V_COUNT; i++) if (p->views[i].button == w) views_show(p, i);
}

void views_build(ExPanels *p) {
    int i, side;
    ExWidget *strips[2], *stacks[2];
    ExListHandlers h;
    View *v;
    for (i = 0; i < V_COUNT; i++) {
        View *x = &p->views[i];
        x->id = DEFS[i].id; x->title = DEFS[i].title; x->label = DEFS[i].label; x->feature = DEFS[i].feature; x->side = DEFS[i].side; x->auto_chevron = DEFS[i].auto_chevron; x->shown = 1; x->stale = 1;
    }
    for (side = 0; side < 2; side++) {
        ExWidget *root = vbox(p, side ? "Right sidebar panels" : "Left sidebar panels");
        strips[side] = ex_widget_box(p->ui, 0, side ? "Right sidebar views" : "Left sidebar views");
        strips[side]->width = ex_size_grow(); strips[side]->gap = 0.25f; strips[side]->pad_x = 0.5f; strips[side]->pad_y = 0.5f; strips[side]->bg_token = EX_COLOR_BACKGROUND_ALT;
        stacks[side] = vbox(p, side ? "Right sidebar view" : "Left sidebar view");
        ex_widget_add(root, strips[side]); ex_widget_add(root, stacks[side]);
        if (side) p->right = root; else p->left = root;
    }
    p->left_strip = strips[0]; p->right_strip = strips[1]; p->left_stack = stacks[0]; p->right_stack = stacks[1];

    /* files */
    v = &p->views[V_FILES];
    v->root = p->in.files ? p->in.files : vbox(p, "Files");
    /* search */
    v = &p->views[V_SEARCH];
    v->root = vbox(p, "Search");
    {
        ExWidget *row1 = hbox(p, "Search field"), *row2 = hbox(p, "Search options"), *saved;
        row1->pad_x = 1; row1->pad_y = 0.5f; row2->pad_x = 1;
        p->search_doc = ex_doc_create("", 0);
        p->search_field = make_field(p, p->search_doc, "Search", search_key, p);
        ex_widget_add(row1, p->search_field);
        p->b_case = small(make_button(p, "Aa", "Match case", on_b_case));
        p->b_collapse = small(make_button(p, "Collapse", "Collapse results", on_b_collapse));
        p->b_context = small(make_button(p, "Context", "Show more context", on_b_context));
        p->b_sort = small(make_button(p, "Sort", "Sort order", on_b_sort));
        ex_widget_add(row2, p->b_case); ex_widget_add(row2, p->b_collapse); ex_widget_add(row2, p->b_context); ex_widget_add(row2, p->b_sort);
        {
            ExWidget *row3 = hbox(p, "Search actions");
            row3->pad_x = 1;
            ex_widget_add(row3, small(make_button(p, "Copy", "Copy the results", on_b_copy)));
            ex_widget_add(row3, small(make_button(p, "Save", "Save this search", on_b_save)));
            ex_widget_add(row3, small(make_button(p, "Bookmark", "Bookmark this search", on_b_bookmark_search)));
            ex_widget_add(v->root, row1); ex_widget_add(v->root, row2); ex_widget_add(v->root, row3);
        }
        v->summary = summary_label(p, "");
        ex_widget_add(v->root, v->summary);
        saved = ex_widget_list(p->ui, "Saved searches");
        saved->height = ex_size_px(p->ui, 33);
        memset(&h, 0, sizeof h); h.activate = on_saved_activate; h.action = on_saved_action; h.single_click = 1;
        setup_list(p, v, saved, 1, &h);
        v->list2 = saved;
        v->extra = ex_widget_label(p->ui, "Saved searches");           /* the heading above the saved searches, hidden with them */
        v->extra->text_token = EX_COLOR_TEXT_MUTED; v->extra->text_scale = 0.85f; v->extra->pad_x = 1;
        ex_widget_add(v->root, v->extra); ex_widget_add(v->root, saved);
        v->list = ex_widget_list(p->ui, "Search results");
        memset(&h, 0, sizeof h); h.activate = on_search_activate;
        setup_list(p, v, v->list, 0, &h);
        ex_list_set_empty_text(v->list, "No results");
        ex_widget_add(v->root, v->list);
        sync_search_buttons(p);
        set_summary(v, "Type to search. Operators: file: path: tag: line: section: task: [property:value] /regex/");
    }
    /* bookmarks */
    v = &p->views[V_BOOKMARKS];
    v->root = vbox(p, "Bookmarks");
    {
        ExWidget *row = hbox(p, "Bookmark actions");
        row->pad_x = 1; row->pad_y = 0.5f;
        ex_widget_add(row, small(make_button(p, "Add note", "Bookmark the current note", on_bm_add)));
        ex_widget_add(row, small(make_button(p, "Group", "New bookmark group", on_bm_group)));
        p->bm_rename = small(make_button(p, "Rename", "Rename the selected bookmark", on_bm_rename));
        p->bm_remove = small(make_button(p, "Remove", "Remove the selected bookmark", on_bm_remove));
        ex_widget_add(row, p->bm_rename); ex_widget_add(row, p->bm_remove);
        ex_widget_add(v->root, row);
        v->summary = summary_label(p, "");
        ex_widget_add(v->root, v->summary);
        v->list = ex_widget_list(p->ui, "Bookmarks");
        memset(&h, 0, sizeof h); h.activate = on_bm_activate; h.move = on_bm_move; h.drag = 1; h.single_click = 1;
        setup_list(p, v, v->list, 0, &h);
        ex_list_set_empty_text(v->list, "No bookmarks yet. Bookmark the open note with Add note.");
        ex_widget_add(v->root, v->list);
    }
    /* backlinks */
    v = &p->views[V_BACKLINKS];
    v->root = vbox(p, "Backlinks");
    {
        ExWidget *row = hbox(p, "Backlink options");
        row->pad_x = 0; row->pad_y = 0.25f; row->align_y = 1;
        v->summary = summary_label(p, "");
        ex_widget_add(row, v->summary);
        v->extra = small(make_button(p, "Unlinked", "Show unlinked mentions", on_b_unlinked));
        ex_widget_add(row, v->extra);
        ex_widget_add(v->root, row);
        v->list = ex_widget_list(p->ui, "Linked mentions");
        memset(&h, 0, sizeof h); h.activate = on_backlink_activate;
        setup_list(p, v, v->list, 0, &h);
        ex_list_set_empty_text(v->list, "No other note links here");
        ex_widget_add(v->root, v->list);
        v->list2 = ex_widget_list(p->ui, "Unlinked mentions");
        memset(&h, 0, sizeof h); h.activate = on_backlink_activate; h.action = on_link_action;
        setup_list(p, v, v->list2, 1, &h);
        ex_list_set_empty_text(v->list2, "No unlinked mentions");
        v->list2->hidden = 1;
        ex_widget_add(v->root, v->list2);
    }
    /* outgoing links */
    v = &p->views[V_OUTGOING];
    v->root = vbox(p, "Outgoing links");
    v->summary = summary_label(p, ""); ex_widget_add(v->root, v->summary);
    v->list = ex_widget_list(p->ui, "Outgoing links");
    memset(&h, 0, sizeof h); h.activate = on_outgoing_activate;
    setup_list(p, v, v->list, 0, &h);
    ex_list_set_empty_text(v->list, "This note has no links");
    ex_widget_add(v->root, v->list);
    /* tags */
    v = &p->views[V_TAGS];
    v->root = vbox(p, "Tags");
    v->summary = summary_label(p, ""); ex_widget_add(v->root, v->summary);
    v->list = ex_widget_list(p->ui, "Tags");
    memset(&h, 0, sizeof h); h.activate = on_tag_activate; h.single_click = 1;
    setup_list(p, v, v->list, 0, &h);
    ex_list_set_empty_text(v->list, "No tags in this vault");
    ex_widget_add(v->root, v->list);
    /* properties */
    v = &p->views[V_PROPS];
    v->root = vbox(p, "All properties");
    {
        ExWidget *row = hbox(p, "Property actions");
        row->pad_x = 1; row->pad_y = 0.5f;
        p->prop_rename = small(make_button(p, "Rename", "Rename the selected property in every note", on_prop_rename));
        p->prop_type = small(make_button(p, "Change type", "Change the type of the selected property", on_prop_type));
        ex_widget_add(row, p->prop_rename); ex_widget_add(row, p->prop_type);
        ex_widget_add(v->root, row);
    }
    v->summary = summary_label(p, ""); ex_widget_add(v->root, v->summary);
    v->list = ex_widget_list(p->ui, "Properties");
    memset(&h, 0, sizeof h);
    setup_list(p, v, v->list, 0, &h);
    ex_list_set_empty_text(v->list, "No properties in this vault");
    ex_widget_add(v->root, v->list);
    /* the properties of the open note */
    v = &p->views[V_FILEPROPS];
    v->root = vbox(p, "File properties");
    {
        ExWidget *row = ex_widget_box(p->ui, 0, "Property actions");
        row->width = ex_size_grow(); row->gap = 0.5f; row->pad_x = 0.5f; row->pad_y = 0.25f;
        p->fp_add = small(make_button(p, "Add", "Add a property to this note", fileprops_on_add));
        p->fp_edit = small(make_button(p, "Edit", "Edit the selected property", fileprops_on_edit));
        p->fp_type = small(make_button(p, "Type", "Change the type of the selected property", fileprops_on_type));
        p->fp_remove = small(make_button(p, "Remove", "Remove the selected property from this note", fileprops_on_remove));
        ex_widget_add(row, p->fp_add); ex_widget_add(row, p->fp_edit); ex_widget_add(row, p->fp_type); ex_widget_add(row, p->fp_remove);
        ex_widget_add(v->root, row);
    }
    v->summary = summary_label(p, ""); ex_widget_add(v->root, v->summary);
    v->list = ex_widget_list(p->ui, "Properties of this note");
    memset(&h, 0, sizeof h); h.activate = fileprops_activate;
    setup_list(p, v, v->list, 0, &h);
    ex_list_set_empty_text(v->list, "This note has no properties");
    ex_widget_add(v->root, v->list);
    /* outline */
    v = &p->views[V_OUTLINE];
    v->root = vbox(p, "Outline");
    v->summary = summary_label(p, ""); ex_widget_add(v->root, v->summary);
    v->list = ex_widget_list(p->ui, "Outline");
    memset(&h, 0, sizeof h); h.activate = on_select_range; h.move = on_outline_move; h.drag = 1; h.single_click = 1;
    setup_list(p, v, v->list, 0, &h);
    ex_list_set_empty_text(v->list, "This note has no headings");
    ex_widget_add(v->root, v->list);
    /* footnotes */
    v = &p->views[V_FOOTNOTES];
    v->root = vbox(p, "Footnotes");
    v->summary = summary_label(p, ""); ex_widget_add(v->root, v->summary);
    v->list = ex_widget_list(p->ui, "Footnotes");
    memset(&h, 0, sizeof h); h.activate = on_select_range; h.single_click = 1;
    setup_list(p, v, v->list, 0, &h);
    ex_list_set_empty_text(v->list, "This note has no footnotes");
    ex_widget_add(v->root, v->list);

    {   /* the list of backlinks at the bottom of the note */
        ExWidget *t;
        p->bottom = vbox(p, "Backlinks in the note");
        p->bottom->height = ex_size_px(p->ui, 170); p->bottom->bg_token = EX_COLOR_BACKGROUND_ALT; p->bottom->border_token = EX_COLOR_BORDER; p->bottom->hidden = 1;
        t = summary_label(p, "Backlinks");
        ex_widget_add(p->bottom, t);
        p->bottom_list = ex_widget_list(p->ui, "Backlinks in the note");
        memset(&h, 0, sizeof h); h.activate = on_backlink_activate;
        setup_list(p, &p->views[V_BACKLINKS], p->bottom_list, 0, &h);
        ex_list_set_empty_text(p->bottom_list, "No other note links here");
        ex_widget_add(p->bottom, p->bottom_list);
    }
    for (i = 0; i < V_COUNT; i++) {
        View *x = &p->views[i];
        int s = x->side;
        x->button = make_button(p, x->label, x->title, on_strip_click);
        ex_widget_set_icon(x->button, STRIP_ICONS[i]);
        ex_widget_add(s ? p->right_strip : p->left_strip, x->button);
        if (x->root) { x->root->hidden = 1; if (x->root->parent == NULL) ex_widget_add(s ? p->right_stack : p->left_stack, x->root); }
        x->root->width = ex_size_grow(); x->root->height = ex_size_grow();
    }
    refresh_saved(p);
    fill_bookmarks(p);
}

void views_free(ExPanels *p) {
    int i;
    size_t k;
    for (i = 0; i < V_COUNT; i++) {
        View *v = &p->views[i];
        qrows_free(&v->rows); qrows_free(&v->rows2);
        for (k = 0; k < v->ncollapsed; k++) free(v->collapsed[k]);
        free(v->collapsed);
    }
    ex_doc_free(p->search_doc);
}

int view_visible(const ExPanels *p, int view) {
    const View *v = &p->views[view];
    return v->shown && p->active[v->side] == view && ex_ws_sidebar_visible(p->in.ws, v->side ? EX_SIDE_RIGHT : EX_SIDE_LEFT);
}

void views_features(ExPanels *p) {
    int i, side;
    for (i = 0; i < V_COUNT; i++) {
        View *v = &p->views[i];
        v->shown = ex_features_enabled(p->in.features, v->feature);
        v->button->hidden = !v->shown;
    }
    for (side = 0; side < 2; side++) {
        if (!p->views[p->active[side]].shown) for (i = 0; i < V_COUNT; i++) if (p->views[i].side == side && p->views[i].shown) { p->active[side] = i; break; }
        for (i = 0; i < V_COUNT; i++) {
            View *v = &p->views[i];
            if (v->side != side) continue;
            v->root->hidden = !(v->shown && p->active[side] == i);
            v->button->selected = v->shown && p->active[side] == i;
        }
    }
}

void views_show(ExPanels *p, int view) {
    View *v = &p->views[view];
    int s = v->side;
    if (!v->shown) return;
    p->active[s] = view;
    views_features(p);
    v->stale = 1;
    ex_ws_open_panel(p->in.ws, s ? EX_SIDE_RIGHT : EX_SIDE_LEFT, v->id);
    {   /* the workspace remembers only the one that is shown */
        int i;
        for (i = 0; i < V_COUNT; i++) if (i != view && p->views[i].side == s) ex_ws_close_panel(p->in.ws, s ? EX_SIDE_RIGHT : EX_SIDE_LEFT, p->views[i].id);
    }
    if (!ex_ws_sidebar_visible(p->in.ws, s ? EX_SIDE_RIGHT : EX_SIDE_LEFT)) ex_ws_set_sidebar(p->in.ws, s ? EX_SIDE_RIGHT : EX_SIDE_LEFT, 1, ex_ws_sidebar_width(p->in.ws, s ? EX_SIDE_RIGHT : EX_SIDE_LEFT));
    if (view == V_SEARCH) ex_ui_set_focus(p->ui, p->search_field);
}

void views_note_changed(ExPanels *p) {
    p->views[V_BACKLINKS].stale = p->views[V_OUTGOING].stale = p->views[V_OUTLINE].stale = p->views[V_FOOTNOTES].stale = p->views[V_FILEPROPS].stale = 1;
}

/* Sends the requests for the views that are showing and out of date. */
static QJob *text_job(ExPanels *p, int kind, unsigned long *req) {
    QJob *j = qjob_new(kind, *req = ++p->next_req);
    size_t len = 0;
    if (!j) return NULL;
    j->text = p->in.host.active_text ? p->in.host.active_text(p->in.host.user, &len) : NULL;
    j->len = len;
    j->a = dup_s(p->cur_path ? p->cur_path : "");
    if (!j->text) { j->text = dup_s(""); j->len = 0; }
    return j;
}

void views_refresh(ExPanels *p) {
    int i;
    for (i = 0; i < V_COUNT; i++) {
        View *v = &p->views[i];
        QJob *j = NULL;
        if (!v->stale || !(view_visible(p, i) || (i == V_BACKLINKS && p->bottom_on))) continue;
        v->stale = 0;
        switch (i) {
        case V_SEARCH:
            refresh_saved(p);
            { char *q = field_text_of(p->search_doc); int has = q && q[0]; free(q); if (has) run_search_now(p); }
            break;
        case V_BOOKMARKS: fill_bookmarks(p); break;
        case V_BACKLINKS:
            if (!p->cur_path) { qrows_free(&v->rows); qrows_free(&v->rows2); fill_list(v, v->list, &v->rows); fill_list(v, v->list2, &v->rows2); set_summary(v, "No note is open"); break; }
            j = qjob_new(QJ_BACKLINKS, v->req = ++p->next_req);
            if (j) { j->a = dup_s(p->cur_path); send_job(p, j); }
            if (p->show_unlinked) { j = qjob_new(QJ_UNLINKED, v->req2 = ++p->next_req); if (j) { j->a = dup_s(p->cur_path); send_job(p, j); } }
            break;
        case V_OUTGOING:
            if (!p->cur_path) { qrows_free(&v->rows); fill_list(v, v->list, &v->rows); set_summary(v, "No note is open"); break; }
            j = qjob_new(QJ_OUTGOING, v->req = ++p->next_req);
            if (j) { j->a = dup_s(p->cur_path); send_job(p, j); }
            break;
        case V_TAGS: j = qjob_new(QJ_TAGS, v->req = ++p->next_req); if (j) send_job(p, j); break;
        case V_PROPS: j = qjob_new(QJ_PROPS, v->req = ++p->next_req); if (j) send_job(p, j); break;
        case V_OUTLINE: case V_FOOTNOTES: case V_FILEPROPS:
            if (!p->cur_path) { qrows_free(&v->rows); fill_list(v, v->list, &v->rows); set_summary(v, "No note is open"); break; }
            j = text_job(p, i == V_OUTLINE ? QJ_OUTLINE : i == V_FILEPROPS ? QJ_FILEPROPS : QJ_FOOTNOTES, &v->req);
            if (j) send_job(p, j);
            break;
        default: break;
        }
    }
}

static void take_rows(QRows *dst, QRows *src) { qrows_free(dst); *dst = *src; memset(src, 0, sizeof *src); }

void views_result(ExPanels *p, QRes *r) {
    View *v = NULL;
    switch (r->kind) {
    case QJ_SEARCH: v = &p->views[V_SEARCH]; if (r->req != v->req) return; take_rows(&v->rows, &r->rows); fill_list(v, v->list, &v->rows); set_summary(v, r->summary); break;
    case QJ_BACKLINKS: v = &p->views[V_BACKLINKS]; if (r->req != v->req) return; take_rows(&v->rows, &r->rows); fill_list(v, v->list, &v->rows); fill_list(v, p->bottom_list, &v->rows); set_summary(v, r->summary); break;
    case QJ_UNLINKED: v = &p->views[V_BACKLINKS]; if (r->req != v->req2) return; take_rows(&v->rows2, &r->rows); fill_list(v, v->list2, &v->rows2); break;
    case QJ_OUTGOING: v = &p->views[V_OUTGOING]; if (r->req != v->req) return; take_rows(&v->rows, &r->rows); fill_list(v, v->list, &v->rows); set_summary(v, r->summary); break;
    case QJ_TAGS: v = &p->views[V_TAGS]; if (r->req != v->req) return; take_rows(&v->rows, &r->rows); fill_list(v, v->list, &v->rows); set_summary(v, r->summary); break;
    case QJ_PROPS: v = &p->views[V_PROPS]; if (r->req != v->req) return; take_rows(&v->rows, &r->rows); fill_list(v, v->list, &v->rows); set_summary(v, r->summary); break;
    case QJ_OUTLINE: v = &p->views[V_OUTLINE]; if (r->req != v->req) return; take_rows(&v->rows, &r->rows); fill_list(v, v->list, &v->rows); set_summary(v, r->summary); break;
    case QJ_FILEPROPS: v = &p->views[V_FILEPROPS]; if (r->req != v->req) return; take_rows(&v->rows, &r->rows); fill_list(v, v->list, &v->rows); set_summary(v, r->summary); break;
    case QJ_FOOTNOTES: v = &p->views[V_FOOTNOTES]; if (r->req != v->req) return; take_rows(&v->rows, &r->rows); fill_list(v, v->list, &v->rows); set_summary(v, r->summary); break;
    case QJ_SNAPSHOT: if (r->snapshot) { ex_switcher_free(p->snapshot); p->snapshot = r->snapshot; r->snapshot = NULL; } break;
    case QJ_PROPOP:
        p->views[V_PROPS].stale = 1; p->views[V_TAGS].stale = 1; p->index_dirty_at = now_ms();
        if (p->in.host.message) p->in.host.message(p->in.host.user, r->summary);
        break;
    case QJ_LINKIT:
        if (p->in.host.message) p->in.host.message(p->in.host.user, r->summary);
        p->views[V_BACKLINKS].stale = 1; p->index_dirty_at = now_ms();
        break;
    default: break;
    }
}

void ex_panels_search_for(ExPanels *p, const char *query) {
    set_doc_text(p->search_doc, query);
    p->views[V_SEARCH].stale = 0;
    views_show(p, V_SEARCH);
    run_search_now(p);
}

/* The search field's text changed: a short pause, then the search runs (on the service's thread). */
void views_search_tick(ExPanels *p) {
    unsigned long v = ex_doc_version(p->search_doc), now = now_ms();
    if (v != p->search_seen) { p->search_seen = v; p->search_changed_at = now; p->search_pending = 1; }
    if (p->search_pending && now - p->search_changed_at >= 250) run_search_now(p);
}

void views_set_search_text(ExPanels *p, const char *text) { set_doc_text(p->search_doc, text); p->search_seen = ex_doc_version(p->search_doc); }
void views_bookmarks_changed(ExPanels *p) { fill_bookmarks(p); }
void views_refresh_saved(ExPanels *p) { refresh_saved(p); }
