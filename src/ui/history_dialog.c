/* Questions the application asks, and the list of earlier versions of a note (sub-slice 7e). The dialogs themselves are in dialogs.c; this
 * file connects them to the application's callbacks and to the service thread, which reads the snapshot folder. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_history.h"
#include "exhaust_panels.h"
#include "panels_internal.h"
#include "service.h"

typedef struct Ask {
    void (*text)(const char *, void *);
    void (*choice)(size_t, void *);
    void (*confirm)(int, void *);
    void *arg;
} Ask;

static Ask *new_ask(ExPanels *p) {
    Ask *a = calloc(1, sizeof *a);
    free(p->ask);                              /* an earlier question that was cancelled */
    p->ask = a;
    return a;
}

static void text_done(ExPanels *p, const char *text, void *arg) {
    Ask a;
    if (!p->ask || p->ask != arg) return;
    a = *(Ask *)p->ask;
    free(p->ask); p->ask = NULL;
    if (a.text) a.text(text, a.arg);
}
static void choice_done(ExPanels *p, size_t index, const char *key, void *arg) {
    Ask a;
    (void)key;
    if (!p->ask || p->ask != arg) return;
    a = *(Ask *)p->ask;
    free(p->ask); p->ask = NULL;
    if (a.choice) a.choice(index, a.arg);
}
static void confirm_done(ExPanels *p, int button, void *arg) {
    Ask a;
    if (!p->ask || p->ask != arg) return;
    a = *(Ask *)p->ask;
    free(p->ask); p->ask = NULL;
    if (a.confirm) a.confirm(button, a.arg);
}

void ex_panels_ask_text(ExPanels *p, const char *title, const char *label, const char *initial, void (*cb)(const char *text, void *arg), void *arg) {
    Ask *a = new_ask(p);
    if (!a) return;
    a->text = cb; a->arg = arg;
    dialog_prompt(p, title, label, initial ? initial : "", text_done, a);
}

void ex_panels_ask_choice_at(ExPanels *p, const char *title, const char *const *labels, size_t n, size_t selected, void (*cb)(size_t index, void *arg), void *arg) {
    Ask *a = new_ask(p);
    if (!a) return;
    a->choice = cb; a->arg = arg;
    dialog_choice(p, title, (const char **)labels, n, selected, choice_done, a);
}

void ex_panels_ask_choice(ExPanels *p, const char *title, const char *const *labels, size_t n, void (*cb)(size_t index, void *arg), void *arg) {
    ex_panels_ask_choice_at(p, title, labels, n, 0, cb, arg);
}

void ex_panels_ask_confirm(ExPanels *p, const char *title, const char *message, const char *b1, const char *b2, const char *b3, void (*cb)(int button, void *arg), void *arg) {
    Ask *a = new_ask(p);
    if (!a) return;
    a->confirm = cb; a->arg = arg;
    dialog_confirm(p, title, message, b1, b2, b3, confirm_done, a);
}

/* ---- earlier versions of a note ---- */

static void drop_list(ExPanels *p) {
    size_t i;
    for (i = 0; i < p->hist_n; i++) { free(p->hist_files[i]); free(p->hist_labels[i]); }
    free(p->hist_files); free(p->hist_labels);
    p->hist_files = NULL; p->hist_labels = NULL; p->hist_n = 0;
}

void history_free(ExPanels *p) {
    drop_list(p);
    free(p->hist_dir); free(p->hist_rel); free(p->ask);
    p->hist_dir = p->hist_rel = NULL; p->ask = NULL;
}

static QJob *history_job(ExPanels *p, int op, const char *a, unsigned long *req) {
    QJob *j = qjob_new(QJ_HISTORY, *req = ++p->next_req);
    size_t len = 0;
    if (!j) return NULL;
    j->i1 = op;
    j->a = dup_s(a ? a : "");
    j->b = dup_s(p->hist_dir ? p->hist_dir : "");
    j->text = p->in.host.active_text ? p->in.host.active_text(p->in.host.user, &len) : NULL;
    j->len = len;
    if (!j->text) { j->text = dup_s(""); j->len = 0; }
    return j;
}

void ex_panels_history_open(ExPanels *p, const char *dir, const char *rel) {
    QJob *j;
    if (!dir || !rel || !*rel) return;
    free(p->hist_dir); free(p->hist_rel);
    p->hist_dir = dup_s(dir); p->hist_rel = dup_s(rel);
    j = history_job(p, 0, rel, &p->hist_req);
    if (j) send_job(p, j);
}

static void version_picked(ExPanels *p, size_t index, const char *key, void *arg) {
    QJob *j;
    (void)key; (void)arg;
    if (index >= p->hist_n) return;
    j = history_job(p, 1, p->hist_files[index], &p->hist_read_req);
    if (j) send_job(p, j);
}

void ex_panels_history_clear(ExPanels *p, const char *dir) {
    QJob *j;
    unsigned long req;
    free(p->hist_dir);
    p->hist_dir = dup_s(dir ? dir : "");
    j = history_job(p, 2, "", &req);
    if (j) send_job(p, j);
}

void history_result(ExPanels *p, QRes *r) {
    if (r->req == p->hist_req) {
        size_t i;
        drop_list(p);
        if (!r->ok || r->rows.n == 0) {
            if (p->in.host.message) p->in.host.message(p->in.host.user, "There are no earlier versions of this note yet.");
            return;
        }
        p->hist_files = calloc(r->rows.n, sizeof *p->hist_files);
        p->hist_labels = calloc(r->rows.n, sizeof *p->hist_labels);
        if (!p->hist_files || !p->hist_labels) { drop_list(p); return; }
        for (i = 0; i < r->rows.n; i++) { p->hist_files[i] = dup_s(r->rows.v[i].key); p->hist_labels[i] = dup_s(r->rows.v[i].text); }
        p->hist_n = r->rows.n;
        dialog_choice(p, "Earlier versions of this note", (const char **)p->hist_labels, p->hist_n, 0, version_picked, NULL);
    } else if (r->req == p->hist_read_req) {
        if (!r->ok || r->rows2.n == 0) { if (p->in.host.message) p->in.host.message(p->in.host.user, "That version could not be read."); return; }
        {
            size_t i, idx = p->hist_n;
            for (i = 0; i < p->hist_n; i++) if (r->rows2.v[0].key && !strcmp(r->rows2.v[0].key, p->hist_files[i])) idx = i;
            if (p->in.host.history_version)
                p->in.host.history_version(p->in.host.user, p->hist_rel ? p->hist_rel : "", idx < p->hist_n ? p->hist_labels[idx] : "", r->rows2.v[0].text ? r->rows2.v[0].text : "", r->rows2.v[0].aux1, r->text ? r->text : "");
        }
    } else if (r->ok && r->summary[0] && r->rows.n == 0 && r->rows2.n == 0 && !r->text) {   /* clearing the history */
        if (p->in.host.message) p->in.host.message(p->in.host.user, r->summary);
    }
}
