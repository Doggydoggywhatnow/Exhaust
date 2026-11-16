/* The status bar: see include/exhaust_statusbar.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_statusbar.h"

struct ExStatusBar {
    ExUi *ui;
    ExWidget *bar, *msg, *mode, *save, *poll, *words;
};

static void set_name(ExWidget *w, const char *name) {
    char *n = malloc(strlen(name) + 1);
    if (n) { strcpy(n, name); free(w->name); w->name = n; }
}

static ExWidget *item(ExUi *ui, const char *name) {
    ExWidget *l = ex_widget_label(ui, "");
    if (!l) return NULL;
    l->text_token = EX_COLOR_TEXT_MUTED;
    l->text_scale = 0.85f;
    l->pad_x = 1.0f; l->pad_y = 0.5f;
    l->hidden = 1;
    set_name(l, name);
    return l;
}

ExStatusBar *ex_statusbar_create(ExUi *ui) {
    ExStatusBar *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->ui = ui;
    s->bar = ex_widget_box(ui, 0, "Status");
    s->msg = ex_widget_label(ui, "");
    s->mode = item(ui, "Editing mode");
    s->save = item(ui, "Save state");
    s->poll = item(ui, "File watching");
    s->words = item(ui, "Word count");
    if (!s->bar || !s->msg || !s->mode || !s->save || !s->poll || !s->words) { free(s); return NULL; }
    s->bar->width = ex_size_grow();
    s->bar->bg_token = EX_COLOR_BACKGROUND_ALT;
    s->msg->width = ex_size_grow(); s->msg->clip = 1;                       /* a long message is cut at the items, not pushed over them */
    s->msg->text_token = EX_COLOR_TEXT_MUTED; s->msg->text_scale = 0.85f; s->msg->pad_x = 1.0f; s->msg->pad_y = 0.5f;
    ex_widget_add(s->bar, s->msg);
    ex_widget_add(s->bar, s->mode);
    ex_widget_add(s->bar, s->save);
    ex_widget_add(s->bar, s->poll);
    ex_widget_add(s->bar, s->words);
    return s;
}

void ex_statusbar_free(ExStatusBar *s) { free(s); }
ExWidget *ex_statusbar_widget(ExStatusBar *s) { return s ? s->bar : NULL; }
ExWidget *ex_statusbar_words(ExStatusBar *s) { return s ? s->words : NULL; }
ExWidget *ex_statusbar_message_label(ExStatusBar *s) { return s ? s->msg : NULL; }

void ex_statusbar_message(ExStatusBar *s, const char *text) { if (s) ex_widget_set_text(s->msg, text ? text : ""); }

void ex_statusbar_set_mode(ExStatusBar *s, const char *mode, int setting_on) {
    const char *t = "";
    if (!s) return;
    if (setting_on && mode && *mode) t = !strcmp(mode, "source") ? "Source mode" : !strcmp(mode, "reading") ? "Reading view" : "Live preview";
    ex_widget_set_text(s->mode, t);
    s->mode->hidden = !*t;
}

void ex_statusbar_set_save(ExStatusBar *s, ExSaveState state) {
    const char *t = state == EX_SAVE_SAVED ? "Saved" : state == EX_SAVE_EDITING ? "Editing" : state == EX_SAVE_FAILED ? "Not saved" : "";
    if (!s) return;
    ex_widget_set_text(s->save, t);
    s->save->text_token = state == EX_SAVE_FAILED ? EX_COLOR_ERROR : EX_COLOR_TEXT_MUTED;
    s->save->hidden = !*t;
}

void ex_statusbar_set_polling(ExStatusBar *s, int polling) {
    if (!s) return;
    ex_widget_set_text(s->poll, polling ? "Checking for outside changes by polling" : "");
    s->poll->hidden = !polling;
}

static ExWidget *by_item(const ExStatusBar *s, const char *name) {
    if (!s || !name) return NULL;
    return !strcmp(name, "message") ? s->msg : !strcmp(name, "mode") ? s->mode : !strcmp(name, "save") ? s->save : !strcmp(name, "polling") ? s->poll : !strcmp(name, "words") ? s->words : NULL;
}

const char *ex_statusbar_text(const ExStatusBar *s, const char *name) {
    ExWidget *w = by_item(s, name);
    return w && !w->hidden && w->text ? w->text : "";
}
int ex_statusbar_item_visible(const ExStatusBar *s, const char *name) { ExWidget *w = by_item(s, name); return w && !w->hidden && w->text && *w->text; }
