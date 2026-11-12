#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_findbar.h"

#define MAX_COUNT 10000

struct ExFindBar {
    ExUi *ui;
    ExWidget *root, *find_row, *replace_row;
    ExWidget *find_field, *replace_field, *status, *b_regex, *b_case, *b_word;
    ExDoc *find_doc, *replace_doc;
    ExWidget *target;                 /* the editor widget (not owned) */
    ExFind *find;
    int regex, match_case, word;
    int open, replace_mode;
    unsigned long pat_version;        /* the find field's version when the pattern was compiled */
    int opts_dirty;
    unsigned long text_version;
    ExDoc *text_doc;
    size_t count;
    char err[160];
    char status_text[200];
};

static char *dup_str(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }
static void set_name(ExWidget *w, const char *name) { free(w->name); w->name = dup_str(name); }

static char *field_text(ExDoc *d) {
    char *t = ex_buffer_text(ex_doc_buffer(d), NULL), *nl;
    if (!t) return NULL;
    nl = strpbrk(t, "\r\n");
    if (nl) *nl = 0;
    return t;
}

static void set_field(ExDoc *d, const char *text) {
    const ExBuffer *b = ex_doc_buffer(d);
    ExChange c;
    ExSel all;
    c.offset = 0; c.remove_len = ex_buffer_size(b); c.text = text; c.text_len = strlen(text);
    ex_doc_apply(d, &c, 1, 0, NULL, 0);
    ex_doc_seal(d);
    all.anchor = 0; all.head = strlen(text);
    ex_doc_set_selections(d, &all, 1);
}

static ExEditor *ed(const ExFindBar *fb) { return fb->target ? fb->target->editor : NULL; }

static void release_find(ExFindBar *fb) {
    if (ed(fb)) ex_editor_set_find(ed(fb), NULL);
    ex_find_free(fb->find);
    fb->find = NULL;
}

static void refresh_status(ExFindBar *fb, size_t current) {
    if (fb->err[0]) snprintf(fb->status_text, sizeof fb->status_text, "%s", fb->err);
    else if (!fb->find) fb->status_text[0] = 0;
    else if (fb->count == 0) snprintf(fb->status_text, sizeof fb->status_text, "No results");
    else if (current) snprintf(fb->status_text, sizeof fb->status_text, "%zu of %s%zu", current, fb->count >= MAX_COUNT ? "more than " : "", fb->count);
    else snprintf(fb->status_text, sizeof fb->status_text, "%s%zu results", fb->count >= MAX_COUNT ? "more than " : "", fb->count);
    ex_widget_set_text(fb->status, fb->status_text);
}

/* Counts the matches and finds the one the selection is on. */
static void recount(ExFindBar *fb) {
    ExEditor *e = ed(fb);
    size_t len = 0, pos = 0, s, en, current = 0;
    char *text;
    ExSel sel;
    fb->count = 0;
    if (!e || !fb->find) { refresh_status(fb, 0); return; }
    text = ex_buffer_text(ex_doc_buffer(ex_editor_doc(e)), &len);
    if (!text) return;
    sel = ex_doc_sel(ex_editor_doc(e), ex_doc_primary(ex_editor_doc(e)));
    while (pos <= len && fb->count < MAX_COUNT && ex_find_next(fb->find, text, len, pos, &s, &en) == EX_FIND_MATCH) {
        size_t a = sel.anchor < sel.head ? sel.anchor : sel.head, b = sel.anchor < sel.head ? sel.head : sel.anchor;
        fb->count++;
        if (s == a && en == b) current = fb->count;
        pos = en > s ? en : s + 1;
    }
    free(text);
    refresh_status(fb, current);
}

/* Moves to a match; wraps around. */
void ex_findbar_next(ExFindBar *fb, int dir) {
    ExEditor *e = ed(fb);
    ExDoc *d;
    char *text;
    size_t len, s, en, a, b;
    ExSel sel;
    ExFindResult r;
    if (!e || !fb->find) return;
    d = ex_editor_doc(e);
    text = ex_buffer_text(ex_doc_buffer(d), &len);
    if (!text) return;
    sel = ex_doc_sel(d, ex_doc_primary(d));
    a = sel.anchor < sel.head ? sel.anchor : sel.head;
    b = sel.anchor < sel.head ? sel.head : sel.anchor;
    if (dir >= 0) {
        r = ex_find_next(fb->find, text, len, b, &s, &en);
        if (r == EX_FIND_MATCH && s == a && en == b && b == a) r = b < len ? ex_find_next(fb->find, text, len, b + 1, &s, &en) : EX_FIND_NONE;
        if (r == EX_FIND_NONE) r = ex_find_next(fb->find, text, len, 0, &s, &en);          /* wrap */
    } else {
        r = a > 0 ? ex_find_prev(fb->find, text, len, a, &s, &en) : EX_FIND_NONE;
        if (r == EX_FIND_NONE) r = ex_find_prev(fb->find, text, len, len + 1, &s, &en);    /* wrap */
    }
    free(text);
    if (r == EX_FIND_MATCH) { ex_editor_select(e, ex_ui_renderer(fb->ui), s, en); recount(fb); }
    else if (r == EX_FIND_LIMIT) { snprintf(fb->err, sizeof fb->err, "This pattern takes too long to match"); refresh_status(fb, 0); }
}

static void compile(ExFindBar *fb) {
    char *pat = field_text(fb->find_doc);
    ExFindOptions o;
    char err[160];
    release_find(fb);
    fb->err[0] = 0;
    o.regex = fb->regex; o.match_case = fb->match_case; o.whole_word = fb->word;
    if (pat && pat[0]) {
        fb->find = ex_find_compile(pat, strlen(pat), &o, err, sizeof err);
        if (!fb->find) snprintf(fb->err, sizeof fb->err, "%s", err);
    }
    free(pat);
    if (fb->find && ed(fb)) ex_editor_set_find(ed(fb), fb->find);
}

void ex_findbar_update(ExFindBar *fb) {
    unsigned long pv = ex_doc_version(fb->find_doc);
    int pattern_changed = pv != fb->pat_version || fb->opts_dirty;
    ExEditor *e = ed(fb);
    if (!fb->open) return;
    if (pattern_changed) {
        compile(fb);
        fb->pat_version = pv;
        fb->opts_dirty = 0;
        fb->text_doc = NULL;
        if (fb->find && e) {                                   /* incremental: the first match from where the selection starts */
            ExSel sel = ex_doc_sel(ex_editor_doc(e), ex_doc_primary(ex_editor_doc(e)));
            size_t len, s, en, from = sel.anchor < sel.head ? sel.anchor : sel.head;
            char *text = ex_buffer_text(ex_doc_buffer(ex_editor_doc(e)), &len);
            if (text) {
                ExFindResult r = ex_find_next(fb->find, text, len, from, &s, &en);
                if (r == EX_FIND_NONE) r = ex_find_next(fb->find, text, len, 0, &s, &en);
                if (r == EX_FIND_MATCH) ex_editor_select(e, ex_ui_renderer(fb->ui), s, en);
                free(text);
            }
        }
        recount(fb);
    } else if (e && (fb->text_doc != ex_editor_doc(e) || fb->text_version != ex_doc_version(ex_editor_doc(e)))) {
        recount(fb);
    }
    if (e) { fb->text_doc = ex_editor_doc(e); fb->text_version = ex_doc_version(ex_editor_doc(e)); }
}

int ex_findbar_replace(ExFindBar *fb) {
    ExEditor *e = ed(fb);
    ExDoc *d;
    char *text, *rep, *with;
    size_t len, s, en, a, b, rl = 0;
    ExSel sel;
    int done = 0;
    if (!e || !fb->find || ex_editor_readonly(e)) return 0;
    d = ex_editor_doc(e);
    text = ex_buffer_text(ex_doc_buffer(d), &len);
    if (!text) return 0;
    sel = ex_doc_sel(d, ex_doc_primary(d));
    a = sel.anchor < sel.head ? sel.anchor : sel.head;
    b = sel.anchor < sel.head ? sel.head : sel.anchor;
    rep = field_text(fb->replace_doc);
    if (ex_find_next(fb->find, text, len, a, &s, &en) == EX_FIND_MATCH && s == a && en == b && rep) {
        with = ex_find_replacement(fb->find, text, len, s, en, rep, strlen(rep), &rl);
        if (with) {
            ExChange c;
            ExSel after;
            c.offset = s; c.remove_len = en - s; c.text = with; c.text_len = rl;
            after.anchor = after.head = s + rl;
            ex_doc_apply(d, &c, 1, 0, &after, 1);
            free(with);
            done = 1;
        }
    }
    free(rep);
    free(text);
    ex_findbar_next(fb, 1);
    return done;
}

size_t ex_findbar_replace_all(ExFindBar *fb) {
    ExEditor *e = ed(fb);
    ExDoc *d;
    char *text, *rep;
    size_t len, n, ne, i;
    ExFindEdit *edits = NULL;
    if (!e || !fb->find || ex_editor_readonly(e)) return 0;
    d = ex_editor_doc(e);
    text = ex_buffer_text(ex_doc_buffer(d), &len);
    rep = field_text(fb->replace_doc);
    if (!text || !rep) { free(text); free(rep); return 0; }
    n = ne = ex_find_replace_all(fb->find, text, len, rep, strlen(rep), &edits);
    if (n) {
        ExChange *ch = malloc(n * sizeof *ch);
        if (ch) {
            for (i = 0; i < n; i++) { ch[i].offset = edits[i].offset; ch[i].remove_len = edits[i].remove_len; ch[i].text = edits[i].text; ch[i].text_len = edits[i].text_len; }
            if (ex_doc_apply(d, ch, n, 0, NULL, 0) != 0) n = 0;
            free(ch);
        } else n = 0;
    }
    ex_find_edits_free(edits, ne);
    free(text);
    free(rep);
    ex_doc_seal(d);
    snprintf(fb->status_text, sizeof fb->status_text, "Replaced %zu", n);
    recount(fb);
    ex_widget_set_text(fb->status, fb->status_text);
    return n;
}

/* ---- widgets ---- */

static void sync_buttons(ExFindBar *fb) {
    fb->b_regex->selected = fb->regex;
    fb->b_case->selected = fb->match_case;
    fb->b_word->selected = fb->word;
}

void ex_findbar_set_option(ExFindBar *fb, ExFindBarOption o, int on) {
    on = on != 0;
    if (o == EX_FINDBAR_REGEX) fb->regex = on; else if (o == EX_FINDBAR_CASE) fb->match_case = on; else fb->word = on;
    fb->opts_dirty = 1;
    sync_buttons(fb);
}

static void on_regex(ExWidget *w, void *u) { ExFindBar *fb = u; (void)w; ex_findbar_set_option(fb, EX_FINDBAR_REGEX, !fb->regex); ex_findbar_update(fb); }
static void on_case(ExWidget *w, void *u) { ExFindBar *fb = u; (void)w; ex_findbar_set_option(fb, EX_FINDBAR_CASE, !fb->match_case); ex_findbar_update(fb); }
static void on_word(ExWidget *w, void *u) { ExFindBar *fb = u; (void)w; ex_findbar_set_option(fb, EX_FINDBAR_WORD, !fb->word); ex_findbar_update(fb); }
static void on_prev(ExWidget *w, void *u) { (void)w; ex_findbar_update(u); ex_findbar_next(u, -1); }
static void on_next(ExWidget *w, void *u) { (void)w; ex_findbar_update(u); ex_findbar_next(u, 1); }
static void on_close(ExWidget *w, void *u) { (void)w; ex_findbar_close(u); }
static void on_replace(ExWidget *w, void *u) { (void)w; ex_findbar_update(u); ex_findbar_replace(u); }
static void on_replace_all(ExWidget *w, void *u) { (void)w; ex_findbar_update(u); ex_findbar_replace_all(u); }

static int find_key(ExWidget *w, ExKey key, int mods, void *u) {
    ExFindBar *fb = u;
    (void)w;
    if (key == EX_KEY_ENTER) { ex_findbar_update(fb); ex_findbar_next(fb, (mods & EX_MOD_SHIFT) ? -1 : 1); return 1; }
    if (key == EX_KEY_ESCAPE) { ex_findbar_close(fb); return 1; }
    return 0;
}

static int replace_key(ExWidget *w, ExKey key, int mods, void *u) {
    ExFindBar *fb = u;
    (void)w; (void)mods;
    if (key == EX_KEY_ENTER) { ex_findbar_update(fb); ex_findbar_replace(fb); return 1; }
    if (key == EX_KEY_ESCAPE) { ex_findbar_close(fb); return 1; }
    return 0;
}

static ExWidget *button(ExFindBar *fb, const char *text, const char *name, ExClickFn fn) {
    ExWidget *b = ex_widget_button(fb->ui, text, fn, fb);
    if (b && name) set_name(b, name);
    return b;
}

static ExWidget *field(ExFindBar *fb, ExDoc *doc, const char *name, int (*key)(ExWidget *, ExKey, int, void *)) {
    ExWidget *f = ex_widget_editor(fb->ui, doc, name);
    if (!f) return NULL;
    f->bg_token = EX_COLOR_BACKGROUND;
    f->border_token = EX_COLOR_BORDER;
    f->radius_token = EX_SIZE_RADIUS;
    f->height = ex_size_token(EX_SIZE_TAB_HEIGHT, 1.0f);
    f->width = ex_size_grow();
    f->on_key = key;
    f->on_key_user = fb;
    return f;
}

ExFindBar *ex_findbar_create(ExUi *ui) {
    ExFindBar *fb = calloc(1, sizeof *fb);
    ExWidget *lab;
    if (!fb) return NULL;
    fb->ui = ui;
    fb->find_doc = ex_doc_create("", 0);
    fb->replace_doc = ex_doc_create("", 0);
    fb->root = ex_widget_box(ui, 1, "Find and replace");
    fb->find_row = ex_widget_box(ui, 0, "Find");
    fb->replace_row = ex_widget_box(ui, 0, "Replace");
    fb->find_field = field(fb, fb->find_doc, "Find", find_key);
    fb->replace_field = field(fb, fb->replace_doc, "Replace with", replace_key);
    fb->status = ex_widget_label(ui, "");
    fb->b_regex = button(fb, ".*", "Use regular expression", on_regex);
    fb->b_case = button(fb, "Aa", "Match case", on_case);
    fb->b_word = button(fb, "W", "Match whole word", on_word);
    if (!fb->find_doc || !fb->replace_doc || !fb->root || !fb->find_row || !fb->replace_row || !fb->find_field || !fb->replace_field || !fb->status || !fb->b_regex || !fb->b_case || !fb->b_word) {
        ex_findbar_free(fb);
        return NULL;
    }
    fb->root->width = ex_size_grow();
    fb->root->bg_token = EX_COLOR_SURFACE;
    fb->root->border_token = EX_COLOR_BORDER;
    fb->root->pad_x = 1; fb->root->pad_y = 0.5f; fb->root->gap = 0.5f;
    fb->root->hidden = 1;
    fb->find_row->width = ex_size_grow(); fb->find_row->gap = 0.5f;
    fb->replace_row->width = ex_size_grow(); fb->replace_row->gap = 0.5f;
    fb->replace_row->hidden = 1;
    fb->status->text_token = EX_COLOR_TEXT_MUTED;
    fb->status->width = ex_size_token(EX_SIZE_SPACING, 14.0f);
    lab = ex_widget_label(ui, "Find");
    if (lab) { lab->width = ex_size_token(EX_SIZE_SPACING, 6.0f); ex_widget_add(fb->find_row, lab); }
    ex_widget_add(fb->find_row, fb->find_field);
    ex_widget_add(fb->find_row, fb->b_case);
    ex_widget_add(fb->find_row, fb->b_word);
    ex_widget_add(fb->find_row, fb->b_regex);
    ex_widget_add(fb->find_row, button(fb, "Prev", "Previous match", on_prev));
    ex_widget_add(fb->find_row, button(fb, "Next", "Next match", on_next));
    ex_widget_add(fb->find_row, fb->status);
    ex_widget_add(fb->find_row, button(fb, "Close", "Close find", on_close));
    lab = ex_widget_label(ui, "Replace");
    if (lab) { lab->width = ex_size_token(EX_SIZE_SPACING, 6.0f); ex_widget_add(fb->replace_row, lab); }
    ex_widget_add(fb->replace_row, fb->replace_field);
    ex_widget_add(fb->replace_row, button(fb, "Replace", "Replace this match", on_replace));
    ex_widget_add(fb->replace_row, button(fb, "Replace all", "Replace every match", on_replace_all));
    ex_widget_add(fb->root, fb->find_row);
    ex_widget_add(fb->root, fb->replace_row);
    return fb;
}

void ex_findbar_free(ExFindBar *fb) {
    if (!fb) return;
    release_find(fb);
    if (fb->root) {
        ex_ui_release(fb->ui, fb->root);
        if (fb->root->parent) ex_widget_remove(fb->root->parent, fb->root);
        ex_widget_free(fb->root);
    }
    ex_doc_free(fb->find_doc);
    ex_doc_free(fb->replace_doc);
    free(fb);
}

ExWidget *ex_findbar_widget(ExFindBar *fb) { return fb->root; }
int ex_findbar_is_open(const ExFindBar *fb) { return fb->open; }
size_t ex_findbar_match_count(const ExFindBar *fb) { return fb->count; }
const char *ex_findbar_status(const ExFindBar *fb) { return fb->status_text; }

void ex_findbar_set_pattern(ExFindBar *fb, const char *pattern) { set_field(fb->find_doc, pattern); }
void ex_findbar_set_replacement(ExFindBar *fb, const char *text) { set_field(fb->replace_doc, text); }

void ex_findbar_target_closed(ExFindBar *fb, ExWidget *editor) {
    if (fb->target != editor) return;
    ex_find_free(fb->find);                      /* the editor is going away: its highlight pointer goes with it */
    fb->find = NULL;
    fb->target = NULL;
    fb->text_doc = NULL;
    fb->count = 0;
    if (fb->open) refresh_status(fb, 0);
}

void ex_findbar_set_target(ExFindBar *fb, ExWidget *editor) {
    if (fb->target == editor) return;
    release_find(fb);
    fb->target = editor;
    fb->opts_dirty = 1;                                    /* compile again for the new editor */
    fb->text_doc = NULL;
}

void ex_findbar_open(ExFindBar *fb, ExWidget *editor, int replace) {
    if (editor && editor->editor) {                         /* the selected text (one line, not too long) becomes the pattern */
        size_t n = 0;
        char *sel = ex_editor_selected_text(editor->editor, &n);
        if (sel && n > 0 && n < 200 && !strpbrk(sel, "\r\n")) { set_field(fb->find_doc, sel); }
        free(sel);
    }
    ex_findbar_set_target(fb, editor);
    fb->open = 1;
    fb->replace_mode = replace;
    fb->root->hidden = 0;
    fb->replace_row->hidden = !replace;
    fb->opts_dirty = 1;
    sync_buttons(fb);
    ex_ui_set_focus(fb->ui, fb->find_field);
    ex_findbar_update(fb);
}

void ex_findbar_close(ExFindBar *fb) {
    ExWidget *back = fb->target;
    release_find(fb);
    fb->open = 0;
    fb->root->hidden = 1;
    fb->err[0] = 0;
    if (back) ex_ui_set_focus(fb->ui, back);
}
