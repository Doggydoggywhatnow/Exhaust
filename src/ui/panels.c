#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_extract.h"
#include "exhaust_fuzzy.h"
#include "exhaust_platform.h"
#include "exhaust_vault.h"
#include "panels_internal.h"

unsigned long now_ms(void) { return (unsigned long)ex_platform_now_ms(); }
char *dup_s(const char *s) { size_t n; char *p; if (!s) return NULL; n = strlen(s); p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }

char *field_text_of(ExDoc *d) {
    char *t = ex_buffer_text(ex_doc_buffer(d), NULL), *nl;
    if (!t) return NULL;
    nl = strpbrk(t, "\r\n");
    if (nl) *nl = 0;
    return t;
}

void set_doc_text(ExDoc *d, const char *text) {
    ExChange c;
    ExSel all;
    c.offset = 0; c.remove_len = ex_buffer_size(ex_doc_buffer(d)); c.text = text; c.text_len = strlen(text);
    ex_doc_apply(d, &c, 1, 0, NULL, 0);
    ex_doc_seal(d);
    all.anchor = 0; all.head = strlen(text);
    ex_doc_set_selections(d, &all, 1);
}

ExWidget *make_field(ExPanels *p, ExDoc *doc, const char *name, int (*key)(ExWidget *, ExKey, int, void *), void *user) {
    ExWidget *f = ex_widget_editor(p->ui, doc, name);
    if (!f) return NULL;
    f->bg_token = EX_COLOR_BACKGROUND; f->border_token = EX_COLOR_BORDER; f->radius_token = EX_SIZE_RADIUS;
    f->height = ex_size_token(EX_SIZE_TAB_HEIGHT, 1.0f); f->width = ex_size_grow();
    f->on_key = key; f->on_key_user = user;
    return f;
}

ExWidget *make_button(ExPanels *p, const char *text, const char *name, ExClickFn fn) {
    ExWidget *b = ex_widget_button(p->ui, text, fn, p);
    if (b && name) { free(b->name); b->name = dup_s(name); }
    return b;
}

void note_title(const char *path, char *out, size_t cap) {
    const char *b = strrchr(path, '/');
    size_t n;
    b = b ? b + 1 : path;
    n = strlen(b);
    if (n > 3 && !strcmp(b + n - 3, ".md")) n -= 3;
    if (n >= cap) n = cap - 1;
    memcpy(out, b, n); out[n] = 0;
}

void send_job(ExPanels *p, QJob *j) { if (p->svc) qservice_post(p->svc, j); else { qres_free(NULL); free(j); } }

void panels_open_note(ExPanels *p, const char *path, size_t offset, int new_tab) {
    if (p->in.host.open_note) p->in.host.open_note(p->in.host.user, path, offset, new_tab);
}

/* ---- creation ---- */

static size_t split_list(const char *value, char **block, const char ***items) {
    char *copy = dup_s(value ? value : ""), *tok, *save;
    const char **list = NULL;
    size_t n = 0;
    for (tok = copy; tok; tok = save) {
        const char **nl;
        save = strchr(tok, ',');
        if (save) *save++ = 0;
        while (*tok == ' ') tok++;
        if (!*tok) continue;
        nl = realloc(list, (n + 1) * sizeof *nl);
        if (!nl) break;
        list = nl; list[n++] = tok;
    }
    *block = copy; *items = list;
    return n;
}

static void on_feature(const char *id, int enabled, void *user) { (void)id; (void)enabled; views_features(user); }

ExPanels *ex_panels_create(const ExPanelsInit *init) {
    ExPanels *p = calloc(1, sizeof *p);
    char *block = NULL;
    const char **excl = NULL;
    size_t nex;
    if (!p) return NULL;
    p->in = *init;
    p->ui = init->ui;
    p->root = dup_s(init->vault_root);
    p->index_path = init->index_path ? dup_s(init->index_path) : ex_index_path_for_vault(init->vault_id);
    p->results = ex_queue_create(init->wake, init->wake_user);
    p->active[0] = V_FILES; p->active[1] = V_BACKLINKS;
    if (!p->root || !p->index_path || !p->results) { ex_panels_free(p); return NULL; }
    nex = split_list(ex_settings_get(init->settings, "files", "excluded"), &block, &excl);
    p->indexer = ex_indexer_start(p->index_path, p->root, excl, nex, 3, p->results);
    free(block); free(excl);
    p->svc = qservice_start(p->index_path, p->root, p->results, 5);
    p->bookmarks = ex_bookmarks_load(p->root, 1);
    p->saved = ex_saved_queries_load(p->root);
    views_build(p);
    {   /* the panel shown on each side when the vault was closed */
        int side, i;
        for (side = 0; side < 2; side++) {
            size_t k, n = ex_ws_panel_count(p->in.ws, side ? EX_SIDE_RIGHT : EX_SIDE_LEFT);
            for (k = 0; k < n; k++) for (i = 0; i < V_COUNT; i++) if (p->views[i].side == side && !strcmp(p->views[i].id, ex_ws_panel(p->in.ws, side ? EX_SIDE_RIGHT : EX_SIDE_LEFT, k))) p->active[side] = i;
        }
    }
    if (ex_ws_search_text(p->in.ws)[0]) views_set_search_text(p, ex_ws_search_text(p->in.ws));
    views_features(p);
    if (p->indexer) ex_indexer_sync(p->indexer);
    {
        QJob *j = qjob_new(QJ_SNAPSHOT, ++p->next_req);
        if (j) { j->include_attachments = ex_settings_get_bool(p->in.settings, "quick_switcher", "show_attachments"); send_job(p, j); }
    }
    return p;
}

void ex_panels_free(ExPanels *p) {
    size_t i;
    if (!p) return;
    dialogs_free(p);
    ex_indexer_stop(p->indexer);
    qservice_stop(p->svc);
    ex_index_close(p->reader);
    ex_bookmarks_free(p->bookmarks);
    ex_saved_queries_free(p->saved);
    ex_switcher_free(p->snapshot);
    {   /* the widgets: the explorer belongs to the application, so it is taken out first */
        View *f = &p->views[V_FILES];
        if (f->root && f->root->parent) { ex_ui_release(p->ui, f->root); ex_widget_remove(f->root->parent, f->root); }
    }
    if (p->bottom) { ex_ui_release(p->ui, p->bottom); if (p->bottom->parent) ex_widget_remove(p->bottom->parent, p->bottom); ex_widget_free(p->bottom); }
    if (p->left) { ex_ui_release(p->ui, p->left); if (p->left->parent) ex_widget_remove(p->left->parent, p->left); ex_widget_free(p->left); }
    if (p->right) { ex_ui_release(p->ui, p->right); if (p->right->parent) ex_widget_remove(p->right->parent, p->right); ex_widget_free(p->right); }
    views_free(p);
    {
        ExMessage m;
        if (p->results) while (ex_queue_pop(p->results, &m)) if (m.payload && m.free_payload) m.free_payload(m.payload);
    }
    ex_queue_destroy(p->results);
    for (i = 0; i < p->nrecent; i++) free(p->recent[i]);
    free(p->cur_path); free(p->root); free(p->index_path); free(p->dialog_arg_text);
    for (i = 0; i < p->npending; i++) { free((char *)p->pending_moves[i].from); free((char *)p->pending_moves[i].to); }
    free(p->pending_moves);
    free(p);
}

ExWidget *ex_panels_left(ExPanels *p) { return p->left; }
ExWidget *ex_panels_right(ExPanels *p) { return p->right; }
ExWidget *ex_panels_bottom(ExPanels *p) { return p->bottom; }
ExWidget *ex_panels_overlay(ExPanels *p) { return p->overlay ? p->overlay : p->preview; }
int ex_panels_modal(const ExPanels *p) { return p->overlay != NULL; }
int ex_panels_index_ready(const ExPanels *p) { return p->index_ready; }

void ex_panels_show(ExPanels *p, const char *panel) { View *v = view_by_id(p, panel); if (v) views_show(p, (int)(v - p->views)); }
void ex_panels_expand_queries(ExPanels *p, const char *path, const char *text, size_t len) {
    QJob *j;
    if (!ex_features_enabled(p->in.features, "search") || !p->index_ready || !(strstr(text, "```query") || strstr(text, "~~~query"))) return;
    j = qjob_new(QJ_EXPAND, ++p->next_req);
    if (!j) return;
    j->a = dup_s(path);
    j->text = malloc(len + 1);
    if (j->text) { memcpy(j->text, text, len); j->text[len] = 0; j->len = len; }
    send_job(p, j);
}

void ex_panels_open_switcher(ExPanels *p) { if (ex_features_enabled(p->in.features, "quick_switcher")) dialog_switcher(p); }
void ex_panels_open_palette(ExPanels *p) { if (ex_features_enabled(p->in.features, "command_palette")) dialog_palette(p); }
void ex_panels_close_dialog(ExPanels *p) { dialog_close(p); }

/* ---- following the application ---- */

void ex_panels_note_opened(ExPanels *p, const char *path) {
    size_t i;
    for (i = 0; i < p->nrecent; i++) if (!strcmp(p->recent[i], path)) { char *t = p->recent[i]; memmove(&p->recent[1], &p->recent[0], i * sizeof *p->recent); p->recent[0] = t; return; }
    if (p->nrecent == 16) free(p->recent[--p->nrecent]);
    memmove(&p->recent[1], &p->recent[0], p->nrecent * sizeof *p->recent);
    p->recent[0] = dup_s(path);
    p->nrecent++;
}

void ex_panels_file_changed(ExPanels *p, const char *path) { if (p->indexer) ex_indexer_file_changed(p->indexer, path); p->index_dirty_at = now_ms(); }
void ex_panels_file_removed(ExPanels *p, const char *path) { if (p->indexer) ex_indexer_file_removed(p->indexer, path); p->index_dirty_at = now_ms(); }
void ex_panels_tree_changed(ExPanels *p) { if (p->indexer) ex_indexer_sync(p->indexer); p->index_dirty_at = now_ms(); }
void ex_panels_files_moved(ExPanels *p, const char *from, const char *to) { if (p->bookmarks && ex_bookmarks_follow_rename(p->bookmarks, from, to)) p->views[V_BOOKMARKS].stale = 1; }

static void mark_index_views_stale(ExPanels *p) {
    p->views[V_BACKLINKS].stale = p->views[V_OUTGOING].stale = p->views[V_TAGS].stale = p->views[V_PROPS].stale = 1;
    if (p->views[V_SEARCH].rows.n || ex_ws_search_text(p->in.ws)[0]) p->views[V_SEARCH].stale = 1;
}

/* ---- renaming ---- */

static void finish_rename(ExPanels *p, QRes *r) {
    size_t i;
    char msg[200];
    for (i = 0; i < p->npending; i++) { ex_panels_file_removed(p, p->pending_moves[i].from); ex_panels_file_changed(p, p->pending_moves[i].to); }
    if (p->rename_is_dir) ex_panels_tree_changed(p);
    if (r) for (i = 0; i < r->report.n; i++) ex_panels_file_changed(p, r->report.paths[i]);
    for (i = 0; i < p->npending; i++) { free((char *)p->pending_moves[i].from); free((char *)p->pending_moves[i].to); }
    free(p->pending_moves); p->pending_moves = NULL; p->npending = 0;
    snprintf(msg, sizeof msg, r && r->total ? "Renamed. %zu link%s in %zu note%s updated." : "Renamed.", r ? r->total : 0, r && r->total == 1 ? "" : "s", r ? r->changed : 0, r && r->changed == 1 ? "" : "s");
    if (p->in.host.message) p->in.host.message(p->in.host.user, msg);
    mark_index_views_stale(p);
}

static void proceed_rename(ExPanels *p, int update) {
    int rc;
    size_t i;
    if (p->in.host.flush_notes) p->in.host.flush_notes(p->in.host.user);
    rc = p->in.host.move_file ? p->in.host.move_file(p->in.host.user, p->rename_from, p->rename_to) : -1;
    if (rc != 0) {
        for (i = 0; i < p->npending; i++) { free((char *)p->pending_moves[i].from); free((char *)p->pending_moves[i].to); }
        free(p->pending_moves); p->pending_moves = NULL; p->npending = 0;
        if (p->in.host.message) p->in.host.message(p->in.host.user, "The file could not be renamed: the name is taken or the disk refused.");
        return;
    }
    for (i = 0; i < p->npending; i++) ex_panels_files_moved(p, p->pending_moves[i].from, p->pending_moves[i].to);
    if (p->rename_is_dir) ex_panels_files_moved(p, p->rename_from, p->rename_to);
    if (update && p->index_ready && p->npending) {
        QJob *j = qjob_new(QJ_REWRITE, p->req_rewrite = ++p->next_req);
        if (j) {
            j->moves = calloc(p->npending, sizeof *j->moves);
            if (j->moves) {
                for (i = 0; i < p->npending; i++) { j->moves[i].from = dup_s(p->pending_moves[i].from); j->moves[i].to = dup_s(p->pending_moves[i].to); }
                j->nmoves = p->npending;
                send_job(p, j);
                return;
            }
            free(j);
        }
    }
    finish_rename(p, NULL);
}

static void confirm_done(ExPanels *p, int button, void *arg) {
    (void)arg;
    if (button == 1) proceed_rename(p, 1);
    else if (button == 2) proceed_rename(p, 0);
    else { size_t i; for (i = 0; i < p->npending; i++) { free((char *)p->pending_moves[i].from); free((char *)p->pending_moves[i].to); } free(p->pending_moves); p->pending_moves = NULL; p->npending = 0; }
}

void link_update_mode(ExPanels *p) {
    const char *m = ex_settings_get(p->in.settings, "files", "link_update");
    p->link_mode = m && !strcmp(m, "automatic") ? 1 : m && !strcmp(m, "off") ? 2 : 0;
    if (!ex_settings_is_set(p->in.settings, "files", "link_update") && !ex_settings_get_bool(p->in.settings, "files", "auto_update_links")) p->link_mode = 2;   /* the older switch, turned off */
}

/* The files that move: the note, or everything under a folder (from the index). */
static void collect_moves(ExPanels *p, const char *from, const char *to, int is_dir) {
    size_t i;
    for (i = 0; i < p->npending; i++) { free((char *)p->pending_moves[i].from); free((char *)p->pending_moves[i].to); }
    free(p->pending_moves); p->pending_moves = NULL; p->npending = 0;
    if (!is_dir) {
        p->pending_moves = calloc(1, sizeof *p->pending_moves);
        if (p->pending_moves) { p->pending_moves[0].from = dup_s(from); p->pending_moves[0].to = dup_s(to); p->npending = 1; }
        return;
    }
    if (!p->reader && p->index_ready) p->reader = ex_index_open(p->index_path, 0, NULL, 0);
    if (p->reader) {
        char **paths = NULL;
        size_t n = 0, fl = strlen(from);
        if (ex_index_files_under(p->reader, from, &paths, &n) == 0) {
            p->pending_moves = calloc(n ? n : 1, sizeof *p->pending_moves);
            for (i = 0; p->pending_moves && i < n; i++) {
                char *np = malloc(strlen(to) + strlen(paths[i]) - fl + 1);
                if (!np) continue;
                strcpy(np, to); strcat(np, paths[i] + fl);
                p->pending_moves[p->npending].from = dup_s(paths[i]); p->pending_moves[p->npending].to = np; p->npending++;
            }
            ex_paths_free(paths, n);
        }
    }
}

void do_rename_flow(ExPanels *p, const char *from, const char *to) {
    ExPathInfo info;
    char abs[1100];
    snprintf(abs, sizeof abs, "%s/%s", p->root, from);
    p->rename_is_dir = ex_platform_path_info(abs, &info) == 0 && info.is_dir;
    snprintf(p->rename_from, sizeof p->rename_from, "%s", from);
    snprintf(p->rename_to, sizeof p->rename_to, "%s", to);
    link_update_mode(p);
    collect_moves(p, from, to, p->rename_is_dir);
    if (p->link_mode == 2 || !p->index_ready) { proceed_rename(p, 0); return; }
    if (p->link_mode == 1) { proceed_rename(p, 1); return; }
    {   /* ask: first count what would change */
        QJob *j = qjob_new(QJ_PLAN, p->req_plan = ++p->next_req);
        size_t i;
        if (!j) { proceed_rename(p, 0); return; }
        j->moves = calloc(p->npending ? p->npending : 1, sizeof *j->moves);
        for (i = 0; j->moves && i < p->npending; i++) { j->moves[i].from = dup_s(p->pending_moves[i].from); j->moves[i].to = dup_s(p->pending_moves[i].to); }
        j->nmoves = p->npending;
        send_job(p, j);
        if (p->in.host.message) p->in.host.message(p->in.host.user, "Looking for links to update...");
    }
}

static void rename_prompt_done(ExPanels *p, const char *text, void *arg) {
    const char *from = arg;
    char to[512], seg[512], *s, *slash;
    size_t n = strlen(from);
    int note = n > 3 && !strcmp(from + n - 3, ".md");
    ExPathInfo info;
    char abs[1100];
    snprintf(to, sizeof to, "%s", text);
    while (to[0] == '/') memmove(to, to + 1, strlen(to));
    n = strlen(to);
    while (n && to[n - 1] == '/') to[--n] = 0;
    if (note && !(n > 3 && !strcmp(to + n - 3, ".md"))) strncat(to, ".md", sizeof to - strlen(to) - 1);
    snprintf(seg, sizeof seg, "%s", to);
    for (s = seg; s && *s; s = slash ? slash + 1 : NULL) {
        slash = strchr(s, '/');
        if (slash) *slash = 0;
        if (!ex_name_valid(s)) { if (p->in.host.message) p->in.host.message(p->in.host.user, "That is not a valid name: it cannot contain # ^ [ ] | : or %% pairs."); return; }
    }
    if (!to[0] || !strcmp(to, from)) return;
    snprintf(abs, sizeof abs, "%s/%s", p->root, to);
    if (ex_platform_path_info(abs, &info) == 0) { if (p->in.host.message) p->in.host.message(p->in.host.user, "Something with that name already exists."); return; }
    do_rename_flow(p, from, to);
}

void ex_panels_rename_file(ExPanels *p, const char *path) {
    char initial[512];
    size_t n = strlen(path);
    free(p->dialog_arg_text);
    p->dialog_arg_text = dup_s(path);
    snprintf(initial, sizeof initial, "%s", path);
    if (n > 3 && !strcmp(path + n - 3, ".md")) initial[n - 3] = 0;
    dialog_prompt(p, "Rename or move", "New name or path from the vault", initial, rename_prompt_done, p->dialog_arg_text);
}

static void handle_plan(ExPanels *p, QRes *r) {
    char msg[400], from[300];
    if (r->req != p->req_plan) return;
    if (!r->ok || r->total == 0) { proceed_rename(p, r->ok ? 1 : 0); return; }
    note_title(p->rename_from, from, sizeof from);
    snprintf(msg, sizeof msg, "%zu link%s in %zu note%s point%s to \"%s\".\nUpdate %s so nothing breaks?", r->total, r->total == 1 ? "" : "s", r->changed, r->changed == 1 ? "" : "s", r->total == 1 ? "s" : "", from, r->total == 1 ? "it" : "them");
    dialog_confirm(p, "Update links?", msg, "Update links", "Don't update", "Cancel", confirm_done, NULL);
}

/* ---- the frame loop ---- */

void ex_panels_update(ExPanels *p) {
    ExMessage m;
    unsigned long now = now_ms();
    const char *path = p->in.host.active_path ? p->in.host.active_path(p->in.host.user) : NULL;
    unsigned long ver = p->in.host.active_version ? p->in.host.active_version(p->in.host.user) : 0;
    while (ex_queue_pop(p->results, &m)) {
        if (m.type == EX_MSG_SERVICE && m.payload) {
            QRes *r = m.payload;
            switch (r->kind) {
            case QJ_PLAN: handle_plan(p, r); break;
            case QJ_REWRITE: if (r->req == p->req_rewrite) finish_rename(p, r); break;
            case QJ_PREVIEW: preview_result(p, r); break;
            case QJ_EXPAND: if (r->ok && r->text && p->in.host.query_results) p->in.host.query_results(p->in.host.user, r->summary, r->text); break;
            default: views_result(p, r); break;
            }
        } else if (m.type == EX_MSG_INDEX_DONE && m.payload) {
            ExIndexDoneMsg *d = m.payload;
            if (d->ok) {
                p->index_ready = 1;
                if (!p->reader) p->reader = ex_index_open(p->index_path, 0, NULL, 0);
                mark_index_views_stale(p);
                p->snapshot_stale = 1;
            }
        }
        if (m.payload && m.free_payload) m.free_payload(m.payload);
    }
    if ((path == NULL) != (p->cur_path == NULL) || (path && p->cur_path && strcmp(path, p->cur_path))) {
        free(p->cur_path);
        p->cur_path = path ? dup_s(path) : NULL;
        views_note_changed(p);
        if (path) ex_panels_note_opened(p, path);
    }
    if (ver != p->text_version) { p->text_version = ver; p->text_seen_at = now; p->text_dirty = 1; }
    if (p->text_dirty && now - p->text_seen_at >= 300) { p->text_dirty = 0; p->views[V_OUTLINE].stale = p->views[V_FOOTNOTES].stale = 1; }
    if (p->index_dirty_at && now - p->index_dirty_at >= 500) {
        p->index_dirty_at = 0;
        mark_index_views_stale(p);
        p->snapshot_stale = 1;
    }
    if (p->snapshot_stale && p->index_ready) {
        QJob *j = qjob_new(QJ_SNAPSHOT, ++p->next_req);
        p->snapshot_stale = 0;
        if (j) { j->include_attachments = ex_settings_get_bool(p->in.settings, "quick_switcher", "show_attachments"); send_job(p, j); }
    }
    {
        int on = ex_settings_get_bool(p->in.settings, "backlinks", "show_at_bottom") && ex_features_enabled(p->in.features, "backlinks") && p->cur_path != NULL;
        if (on != p->bottom_on) { p->bottom_on = on; p->bottom->hidden = !on; if (on) p->views[V_BACKLINKS].stale = 1; }
    }
    views_search_tick(p);
    dialog_refresh(p);
    views_refresh(p);
    if (p->prev_pending == 1 && now - p->prev_since >= 450) {
        QJob *j = qjob_new(QJ_PREVIEW, p->prev_req = ++p->next_req);
        p->prev_pending = 2;
        if (j) {
            j->a = dup_s(p->prev_target); j->b = dup_s(p->prev_heading); j->c = dup_s(p->prev_block);
            j->i1 = 1; j->text = dup_s(p->cur_path ? p->cur_path : "");
            if (p->prev_target[0] == '\x01') { memmove(j->a, j->a + 1, strlen(j->a)); j->i1 = 0; }
            send_job(p, j);
        }
    }
}

void ex_panels_pointer(ExPanels *p, ExWidget *root, float x, float y, int ctrl) {
    ExWidget *w;
    char target[300] = "", heading[200] = "", block[100] = "";
    const char *need = NULL;
    int found = 0;
    if (p->overlay || !ex_features_enabled(p->in.features, "page_preview")) { preview_hide(p); return; }
    w = ex_ui_widget_at(root, x, y);
    if (w && w->type == EX_WIDGET_LIST) {
        long row = ex_list_row_at(w, x, y);
        const ExListRow *r = row >= 0 ? ex_list_row(w, row) : NULL;
        int i;
        for (i = 0; r && i < V_COUNT; i++) {
            View *v = &p->views[i];
            if ((w == v->list || w == v->list2) && (i == V_BACKLINKS || i == V_OUTGOING || i == V_BOOKMARKS) && r->key && strstr(r->key, ".md")) {
                snprintf(target, sizeof target, "\x01%s", r->key);
                need = i == V_BOOKMARKS ? "ctrl_required_bookmarks" : "ctrl_required_links_panels";
                found = 1;
            }
        }
    } else if (w && w->type == EX_WIDGET_READING) {
        char t[300];
        if (ex_reading_internal_link_at(w, x, y, t, sizeof t)) {
            char *hash = strchr(t, '#');
            if (hash) { *hash++ = 0; if (*hash == '^') snprintf(block, sizeof block, "%s", hash + 1); else snprintf(heading, sizeof heading, "%s", hash); }
            snprintf(target, sizeof target, "%s", t);
            need = "ctrl_required_reading_view"; found = 1;
        }
    } else if (w && w->type == EX_WIDGET_EDITOR && w->editor && ex_ui_renderer(p->ui)) {
        if (ex_editor_link_at(w->editor, ex_ui_renderer(p->ui), x, y, target, sizeof target, heading, sizeof heading, block, sizeof block)) { need = "ctrl_required_editing_view"; found = 1; }
    }
    if (found && need && ex_settings_get_bool(p->in.settings, "page_preview", need) && !ctrl) found = 0;
    if (!found) { if (p->prev_target[0] || p->preview) preview_hide(p); return; }
    {
        char key[700];
        snprintf(key, sizeof key, "%s|%s|%s", target, heading, block);
        if (p->prev_target[0] && !strcmp(key, p->prev_key)) return;
        preview_hide(p);
        snprintf(p->prev_key, sizeof p->prev_key, "%s", key);
        snprintf(p->prev_target, sizeof p->prev_target, "%s", target);
        snprintf(p->prev_heading, sizeof p->prev_heading, "%s", heading);
        snprintf(p->prev_block, sizeof p->prev_block, "%s", block);
        p->prev_pending = 1; p->prev_since = now_ms(); p->prev_x = x; p->prev_y = y;
    }
}

/* ---- link suggestions for [[ ---- */

static int strcasecmp_ascii(const char *a, const char *b) {
    for (; *a && *b; a++, b++) { char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a, y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b; if (x != y) return 1; }
    return *a != *b;
}

static void suggest_fill(ExLinkSuggestion *o, const char *label, const char *insert, const char *detail) {
    snprintf(o->label, sizeof o->label, "%s", label);
    snprintf(o->insert, sizeof o->insert, "%s", insert);
    snprintf(o->detail, sizeof o->detail, "%s", detail ? detail : "");
}

size_t ex_panels_suggest_links(void *panels, const char *query, ExLinkSuggestion *out, size_t max) {
    ExPanels *p = panels;
    const char *hash = strchr(query, '#');
    size_t n = 0, i;
    if (hash) {                                                       /* Note#Heading, Note#^block, or #Heading in this note */
        char note[300], path[400] = "", *text;
        size_t len = 0, nl = (size_t)(hash - query);
        int blocks = hash[1] == '^';
        const char *want = hash + 1 + (blocks ? 1 : 0);
        snprintf(note, sizeof note, "%.*s", (int)(nl < sizeof note - 1 ? nl : sizeof note - 1), query);
        if (!note[0]) { if (p->cur_path) snprintf(path, sizeof path, "%s", p->cur_path); }
        else if (p->snapshot) {
            ExSwitchItem it[4];
            size_t k, c = ex_switcher_query(p->snapshot, note, NULL, 0, 4, it);
            for (k = 0; k < c; k++) if (it[k].is_note && !it[k].via_alias && !strcasecmp_ascii(it[k].label, note)) { snprintf(path, sizeof path, "%s", it[k].path); break; }
            if (!path[0] && c && it[0].is_note) snprintf(path, sizeof path, "%s", it[0].path);
        }
        if (!path[0] || !p->in.host.read_text) return 0;
        text = p->in.host.read_text(p->in.host.user, path, &len);
        if (!text) return 0;
        if (!blocks) {
            ExOutlineItem *h;
            size_t nh;
            if (ex_outline(text, len, &h, &nh) == 0) {
                for (i = 0; i < nh && n < max; i++) {
                    char ins[420];
                    if (want[0] && ex_fuzzy_score(want, h[i].text) < 0) continue;
                    snprintf(ins, sizeof ins, "%s#%s", note, h[i].text);
                    suggest_fill(&out[n++], h[i].text, ins, "heading");
                }
                ex_outline_free(h, nh);
            }
        } else {
            ExExtract *x = ex_extract(text, len);
            if (x) {
                for (i = 0; i < x->nblocks && n < max; i++) {
                    char ins[420];
                    if (want[0] && ex_fuzzy_score(want, x->blocks[i].id) < 0) continue;
                    snprintf(ins, sizeof ins, "%s#^%s", note, x->blocks[i].id);
                    suggest_fill(&out[n++], x->blocks[i].id, ins, "block");
                }
                ex_extract_free(x);
            }
        }
        free(text);
        return n;
    }
    if (!p->snapshot) return 0;
    {
        ExSwitchItem it[8];
        size_t c = ex_switcher_query(p->snapshot, query, (const char *const *)p->recent, p->nrecent, max < 8 ? max : 8, it);
        for (i = 0; i < c && n < max; i++) {
            char title[300], ins[620], detail[420];
            const char *slash = strrchr(it[i].path, '/');
            note_title(it[i].path, title, sizeof title);
            if (!it[i].is_note) snprintf(ins, sizeof ins, "%s", slash ? slash + 1 : it[i].path);
            else if (it[i].via_alias) snprintf(ins, sizeof ins, "%s|%s", title, it[i].label);
            else snprintf(ins, sizeof ins, "%s", title);
            snprintf(detail, sizeof detail, "%s", it[i].via_alias ? "alias" : slash ? it[i].path : "");
            if (!it[i].is_note) snprintf(detail, sizeof detail, "file");
            suggest_fill(&out[n++], it[i].via_alias ? it[i].label : (it[i].is_note ? title : it[i].path), ins, detail);
        }
    }
    return n;
}

/* ---- commands and features ---- */

static void cmd_switcher(void *u) { ex_panels_open_switcher(u); }
static void cmd_palette(void *u) { ex_panels_open_palette(u); }
static void cmd_show_search(void *u) { views_show(u, V_SEARCH); }
static void cmd_show_files(void *u) { views_show(u, V_FILES); }
static void cmd_show_bookmarks(void *u) { views_show(u, V_BOOKMARKS); }
static void cmd_show_backlinks(void *u) { views_show(u, V_BACKLINKS); }
static void cmd_show_outgoing(void *u) { views_show(u, V_OUTGOING); }
static void cmd_show_tags(void *u) { views_show(u, V_TAGS); }
static void cmd_show_props(void *u) { views_show(u, V_PROPS); }
static void cmd_show_outline(void *u) { views_show(u, V_OUTLINE); }
static void cmd_show_footnotes(void *u) { views_show(u, V_FOOTNOTES); }
static int has_note(void *u) { return ((ExPanels *)u)->cur_path != NULL; }
static int has_index(void *u) { return ((ExPanels *)u)->index_ready; }

static void cmd_toggle_backlinks_doc(void *u) {
    ExPanels *p = u;
    int on = ex_settings_get_bool(p->in.settings, "backlinks", "show_at_bottom");
    ex_settings_set_bool(p->in.settings, "backlinks", "show_at_bottom", !on);
    if (p->in.host.message) p->in.host.message(p->in.host.user, !on ? "Backlinks are shown at the bottom of notes." : "Backlinks are hidden at the bottom of notes.");
}

static void cmd_random(void *u) {
    ExPanels *p = u;
    unsigned long long st = (unsigned long long)now_ms() * 2654435761ull + 12345;
    char *path;
    if (!p->reader) p->reader = ex_index_open(p->index_path, 0, NULL, 0);
    path = p->reader ? ex_random_note(p->reader, p->cur_path, &st) : NULL;
    if (path) { panels_open_note(p, path, (size_t)-1, 0); free(path); }
    else if (p->in.host.message) p->in.host.message(p->in.host.user, "There is no note to open.");
}

static void cmd_rename(void *u) {
    ExPanels *p = u;
    const ExNode *n = NULL;
    ExWidget *tree = p->in.files ? ex_ui_focus(p->ui) : NULL;
    if (tree && tree->type == EX_WIDGET_TREE) n = ex_tree_selected(tree);
    if (n && n->rel && n->rel[0]) ex_panels_rename_file(p, n->rel);
    else if (p->cur_path) ex_panels_rename_file(p, p->cur_path);
}

static void move_done(ExPanels *p, const char *folder, void *arg) {
    char to[600], *slash;
    const char *from = arg, *base;
    slash = strrchr(from, '/');
    base = slash ? slash + 1 : from;
    while (*folder == '/') folder++;
    if (folder[0]) snprintf(to, sizeof to, "%s%s%s", folder, folder[strlen(folder) - 1] == '/' ? "" : "/", base);
    else snprintf(to, sizeof to, "%s", base);
    if (strcmp(to, from)) {
        ExPathInfo info;
        char abs[1200];
        snprintf(abs, sizeof abs, "%s/%s", p->root, to);
        if (ex_platform_path_info(abs, &info) == 0) { if (p->in.host.message) p->in.host.message(p->in.host.user, "Something with that name already exists there."); return; }
        do_rename_flow(p, from, to);
    }
}

static void cmd_move(void *u) {
    ExPanels *p = u;
    char *slash;
    if (!p->cur_path) return;
    free(p->dialog_arg_text);
    p->dialog_arg_text = dup_s(p->cur_path);
    slash = strrchr(p->cur_path, '/');
    {
        char initial[512] = "";
        if (slash) snprintf(initial, sizeof initial, "%.*s", (int)(slash - p->cur_path), p->cur_path);
        dialog_prompt(p, "Move current file", "Folder (from the vault; empty for the top)", initial, move_done, p->dialog_arg_text);
    }
}

static void cmd_bookmark(void *u) {
    ExPanels *p = u;
    ExBookmark b;
    memset(&b, 0, sizeof b);
    if (!p->cur_path) return;
    b.type = EX_BM_NOTE; b.path = p->cur_path;
    ex_bookmarks_add(p->bookmarks, &b, 0);
    views_bookmarks_changed(p);
    if (p->in.host.message) p->in.host.message(p->in.host.user, "Note bookmarked.");
}

static void cmd_bookmark_heading(void *u) {
    ExPanels *p = u;
    size_t len = 0, caret = p->in.host.caret_offset ? p->in.host.caret_offset(p->in.host.user) : (size_t)-1, n, i;
    char *text = p->in.host.active_text ? p->in.host.active_text(p->in.host.user, &len) : NULL;
    ExOutlineItem *h;
    if (!text || !p->cur_path) { free(text); return; }
    if (ex_outline(text, len, &h, &n) == 0) {
        long best = -1;
        for (i = 0; i < n; i++) if (h[i].start <= caret) best = (long)i;
        if (best >= 0) {
            ExBookmark b;
            memset(&b, 0, sizeof b);
            b.type = EX_BM_HEADING; b.path = p->cur_path; b.heading = h[best].text; b.title = h[best].text;
            ex_bookmarks_add(p->bookmarks, &b, 0);
            views_bookmarks_changed(p);
            if (p->in.host.message) p->in.host.message(p->in.host.user, "Heading bookmarked.");
        } else if (p->in.host.message) p->in.host.message(p->in.host.user, "The caret is not under a heading.");
        ex_outline_free(h, n);
    }
    free(text);
}

static void cmd_bookmark_block(void *u) {
    ExPanels *p = u;
    size_t len = 0, caret = p->in.host.caret_offset ? p->in.host.caret_offset(p->in.host.user) : (size_t)-1, i;
    char *text = p->in.host.active_text ? p->in.host.active_text(p->in.host.user, &len) : NULL;
    ExExtract *x;
    if (!text || !p->cur_path) { free(text); return; }
    x = ex_extract(text, len);
    if (x) {
        int done = 0;
        for (i = 0; i < x->nblocks; i++) if (caret >= x->blocks[i].start && caret <= x->blocks[i].end) {
            ExBookmark b;
            memset(&b, 0, sizeof b);
            b.type = EX_BM_BLOCK; b.path = p->cur_path; b.block = x->blocks[i].id; b.title = x->blocks[i].id;
            ex_bookmarks_add(p->bookmarks, &b, 0);
            views_bookmarks_changed(p);
            done = 1;
            break;
        }
        if (p->in.host.message) p->in.host.message(p->in.host.user, done ? "Block bookmarked." : "The caret is not in a block with an id (^id at the end of it).");
        ex_extract_free(x);
    }
    free(text);
}

static void cmd_bookmark_search(void *u) {
    ExPanels *p = u;
    char *q = field_text_of(p->search_doc);
    ExBookmark b;
    memset(&b, 0, sizeof b);
    if (q && q[0]) { b.type = EX_BM_SEARCH; b.query = q; ex_bookmarks_add(p->bookmarks, &b, 0); views_bookmarks_changed(p); }
    free(q);
}

static void cmd_bookmark_remove(void *u) {
    ExPanels *p = u;
    size_t i;
    for (i = ex_bookmarks_count(p->bookmarks); i > 0; i--) {
        const ExBookmark *b = ex_bookmarks_at(p->bookmarks, i - 1);
        if (b->path && p->cur_path && !strcmp(b->path, p->cur_path)) ex_bookmarks_remove(p->bookmarks, b->id);
    }
    views_bookmarks_changed(p);
}

static void cmd_bookmark_all_tabs(void *u) {
    ExPanels *p = u;
    size_t i;
    for (i = 0; i < ex_ws_tab_count(p->in.ws); i++) {
        const ExTab *t = ex_ws_tab(p->in.ws, ex_ws_tab_at(p->in.ws, i));
        if (t && t->path[0] && !strcmp(t->view, "note")) { ExBookmark b; memset(&b, 0, sizeof b); b.type = EX_BM_NOTE; b.path = (char *)t->path; ex_bookmarks_add(p->bookmarks, &b, 0); }
    }
    views_bookmarks_changed(p);
}

void ex_panels_register(ExPanels *p) {
    static const struct { const char *id, *label; } F[] = {
        {"search", "Search"}, {"bookmarks", "Bookmarks"}, {"backlinks", "Backlinks"}, {"outgoing_links", "Outgoing links"}, {"tags_view", "Tags view"},
        {"properties_view", "Properties view"}, {"outline", "Outline"}, {"footnotes", "Footnotes"}, {"quick_switcher", "Quick switcher"},
        {"command_palette", "Command palette"}, {"random_note", "Random note"}, {"page_preview", "Page preview"}};
    size_t i;
    ExCommands *c = p->in.commands;
    for (i = 0; i < sizeof F / sizeof F[0]; i++) ex_features_register(p->in.features, F[i].id, F[i].label, on_feature, p);
    views_features(p);
    ex_commands_bind(c, "quick-switcher.open-quick-switcher", cmd_switcher, NULL, p);
    ex_commands_bind(c, "command-palette.open-command-palette", cmd_palette, NULL, p);
    ex_commands_bind(c, "search.search-in-all-files", cmd_show_search, NULL, p);
    ex_commands_bind(c, "files.show-file-explorer", cmd_show_files, NULL, p);
    ex_commands_bind(c, "bookmarks.show-bookmarks", cmd_show_bookmarks, NULL, p);
    ex_commands_bind(c, "backlinks.show-backlinks", cmd_show_backlinks, NULL, p);
    ex_commands_bind(c, "backlinks.open-backlinks-for-the-current-note", cmd_show_backlinks, has_note, p);
    ex_commands_bind(c, "backlinks.toggle-backlinks-in-document", cmd_toggle_backlinks_doc, NULL, p);
    ex_commands_bind(c, "outgoing-links.show-outgoing-links", cmd_show_outgoing, NULL, p);
    ex_commands_bind(c, "outgoing-links.open-outgoing-links-for-the-current-file", cmd_show_outgoing, has_note, p);
    ex_commands_bind(c, "tags-view.show-tags", cmd_show_tags, NULL, p);
    ex_commands_bind(c, "properties-view.show-all-properties", cmd_show_props, NULL, p);
    ex_commands_bind(c, "properties-view.show-file-properties", cmd_show_props, has_note, p);
    ex_commands_bind(c, "outline.show-outline", cmd_show_outline, NULL, p);
    ex_commands_bind(c, "outline.open-outline-of-the-current-file", cmd_show_outline, has_note, p);
    ex_commands_bind(c, "footnotes.show-footnotes", cmd_show_footnotes, NULL, p);
    ex_commands_bind(c, "random-note.open-random-note", cmd_random, has_index, p);
    ex_commands_bind(c, "app.rename-file", cmd_rename, has_note, p);
    ex_commands_bind(c, "app.move-current-file-to-another-folder", cmd_move, has_note, p);
    ex_commands_bind(c, "bookmarks.bookmark", cmd_bookmark, has_note, p);
    ex_commands_bind(c, "bookmarks.bookmark-heading-under-cursor", cmd_bookmark_heading, has_note, p);
    ex_commands_bind(c, "bookmarks.bookmark-block-under-cursor", cmd_bookmark_block, has_note, p);
    ex_commands_bind(c, "bookmarks.bookmark-current-search", cmd_bookmark_search, NULL, p);
    ex_commands_bind(c, "bookmarks.remove-bookmark-for-the-current-file", cmd_bookmark_remove, has_note, p);
    ex_commands_bind(c, "bookmarks.bookmark-all-tabs", cmd_bookmark_all_tabs, NULL, p);
}
