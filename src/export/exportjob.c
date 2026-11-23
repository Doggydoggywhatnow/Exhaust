/* The export to PDF as a job (sub-slice 6e-iv): see include/exhaust_exportjob.h. */
#include "exhaust_exportjob.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_platform.h"
#include "exhaust_reading.h"

struct ExExportJob {
    char *title, *text, *page_size, *margin, *out_path;
    size_t len;
    ExPdfExportOptions opt;
    char **names, **paths; size_t nres;
    volatile int cancel;
    ExWorker *worker;
    ExWorker *self;                      /* the thread's own handle, for sending */
    uint64_t last_progress; int last_permille;
};

static char *dup_s(const char *s) { char *d = malloc(strlen(s ? s : "") + 1); if (d) strcpy(d, s ? s : ""); return d; }

static int resolve(void *user, const char *name, char *path, size_t cap) {
    const ExExportJob *j = user;
    size_t i;
    for (i = 0; i < j->nres; i++) if (!strcmp(j->names[i], name)) { snprintf(path, cap, "%s", j->paths[i]); return 1; }
    return 0;
}

static void send_msg(ExWorker *self, const ExExportMsg *m) {
    ExExportMsg *p = malloc(sizeof *p);
    if (!p) return;
    *p = *m;
    if (ex_worker_send(self, EX_MSG_EXPORT, p, sizeof *p, free) != 0) free(p);
}

static void progress(void *user, int permille, int page) {
    ExExportJob *j = user;
    uint64_t now = ex_platform_now_ms();
    ExExportMsg m;
    if (now - j->last_progress < 120 || permille == j->last_permille) return;
    j->last_progress = now; j->last_permille = permille;
    memset(&m, 0, sizeof m);
    m.permille = permille; m.page = page;
    send_msg(j->self, &m);
}

static void job_fn(ExWorker *self, const ExMessage *msg, void *user) {
    ExExportJob *j = user;
    ExPdfExportIn in;
    ExPdfExportOut out;
    ExExportMsg m;
    (void)msg;
    j->self = self;
    memset(&in, 0, sizeof in);
    memset(&m, 0, sizeof m);
    m.done = 1;
    in.title = j->title; in.text = j->text; in.len = j->len; in.opt = j->opt;
    in.resolve = resolve; in.user = j; in.progress = progress; in.cancel = &j->cancel;
    if (ex_pdf_export(&in, &out) == 0 && out.pdf) {
        if (ex_platform_write_file_atomic(j->out_path, out.pdf, out.len) == 0) {
            m.ok = 1; m.pages = out.pages;
            snprintf(m.message, sizeof m.message, "Exported %d page%s to %.200s", out.pages, out.pages == 1 ? "" : "s", j->out_path);
        } else snprintf(m.message, sizeof m.message, "The PDF could not be saved to %.150s. Is the folder writable and the file not open elsewhere? An old file was not changed.", j->out_path);
    } else if (out.cancelled) { m.cancelled = 1; snprintf(m.message, sizeof m.message, "Export cancelled. No file was written."); }
    else snprintf(m.message, sizeof m.message, "The export failed: %.200s", out.err[0] ? out.err : "unknown error");
    ex_pdf_export_free(&out);
    send_msg(self, &m);
}

ExExportJob *ex_exportjob_start(const ExExportStart *s) {
    ExExportJob *j;
    ExMessage m;
    size_t i;
    if (!s || !s->text || !s->out_path || !s->results) return NULL;
    j = calloc(1, sizeof *j);
    if (!j) return NULL;
    j->title = dup_s(s->title); j->out_path = dup_s(s->out_path);
    j->text = malloc(s->len + 1);
    j->page_size = dup_s(s->opt.page_size); j->margin = dup_s(s->opt.margin);
    if (!j->title || !j->out_path || !j->text || !j->page_size || !j->margin) { ex_exportjob_free(j); return NULL; }
    memcpy(j->text, s->text, s->len); j->text[s->len] = 0; j->len = s->len;
    j->opt = s->opt; j->opt.page_size = j->page_size; j->opt.margin = j->margin;
    j->names = calloc(s->nres + 1, sizeof *j->names); j->paths = calloc(s->nres + 1, sizeof *j->paths);
    if (!j->names || !j->paths) { ex_exportjob_free(j); return NULL; }
    for (i = 0; i < s->nres; i++) { j->names[i] = dup_s(s->names[i]); j->paths[i] = dup_s(s->paths[i]); j->nres++; }
    j->worker = ex_worker_start("export", 9, job_fn, j, s->results);
    if (!j->worker) { ex_exportjob_free(j); return NULL; }
    memset(&m, 0, sizeof m);
    m.type = 1;
    if (ex_worker_post(j->worker, &m) != 0) { ex_exportjob_free(j); return NULL; }
    return j;
}

void ex_exportjob_cancel(ExExportJob *j) { if (j) j->cancel = 1; }

void ex_exportjob_free(ExExportJob *j) {
    size_t i;
    if (!j) return;
    j->cancel = 1;
    if (j->worker) ex_worker_stop(j->worker);                 /* joins: the export checks the cancel flag often */
    for (i = 0; i < j->nres; i++) { free(j->names[i]); free(j->paths[i]); }
    free(j->names); free(j->paths); free(j->title); free(j->text); free(j->page_size); free(j->margin); free(j->out_path);
    free(j);
}

/* ---- the files a note embeds ---- */

static void add_name(char ***v, size_t *n, size_t max, const char *s, size_t len) {
    char name[400];
    size_t i;
    if (*n >= max || len == 0 || len >= sizeof name) return;
    {   char tmp[400];
        memcpy(tmp, s, len); tmp[len] = 0;
        ex_embed_split(tmp, name, sizeof name, NULL, 0, NULL, 0);
    }
    if (!name[0] || !strncmp(name, "http://", 7) || !strncmp(name, "https://", 8) || !ex_embed_media_kind(name)) return;
    for (i = 0; i < *n; i++) if (!strcmp((*v)[i], name)) return;
    {   char **nv = realloc(*v, (*n + 1) * sizeof **v);
        if (!nv) return;
        *v = nv;
        (*v)[*n] = dup_s(name);
        if ((*v)[*n]) (*n)++; }
}

size_t ex_exportjob_embed_names(const char *text, size_t len, char ***names, size_t max) {
    size_t i, n = 0;
    *names = NULL;
    for (i = 0; i + 3 < len && n < max; i++) {
        if (text[i] != '!') continue;
        if (text[i + 1] == '[' && text[i + 2] == '[') {
            size_t j = i + 3;
            while (j + 1 < len && !(text[j] == ']' && text[j + 1] == ']') && text[j] != '\n') j++;
            if (j + 1 < len && text[j] == ']') add_name(names, &n, max, text + i + 3, j - i - 3);
            i = j;
        } else if (text[i + 1] == '[') {
            size_t j = i + 2;
            while (j < len && text[j] != ']' && text[j] != '\n') j++;
            if (j + 1 < len && text[j] == ']' && text[j + 1] == '(') {
                size_t k = j + 2, s = k;
                while (k < len && text[k] != ')' && text[k] != ' ' && text[k] != '\n') k++;
                if (k < len) add_name(names, &n, max, text + s, k - s);
                i = k;
            }
        }
    }
    return n;
}

void ex_exportjob_names_free(char **names, size_t n) { size_t i; for (i = 0; i < n; i++) free(names[i]); free(names); }
