#include "exhaust_importjob.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ExImportJob {
    ExImportFormat format;
    char *source, *vault_root, *folder, *note_name, *tpl, *date_format, *time_format;
    ExImportOptions opt;
    volatile int cancel;
    ExWorker *worker;
};

static char *dup_s(const char *s) { char *d = malloc(strlen(s ? s : "") + 1); if (d) strcpy(d, s ? s : ""); return d; }

static void job_fn(ExWorker *self, const ExMessage *msg, void *user) {
    ExImportJob *j = user;
    ExImportMsg *m = calloc(1, sizeof *m);
    int rc;
    (void)msg;
    if (!m) return;
    rc = ex_import_run(j->format, j->source, j->vault_root, &j->opt, &m->report, &j->cancel);
    m->cancelled = m->report.cancelled;
    m->ok = rc == 0 && !m->cancelled;
    if (rc != 0) snprintf(m->message, sizeof m->message, "The import failed: %.300s", m->report.first_problem[0] ? m->report.first_problem : "unknown problem");
    else if (m->cancelled) snprintf(m->message, sizeof m->message, "Import cancelled after %zu note%s.", m->report.notes, m->report.notes == 1 ? "" : "s");
    else {
        size_t n = (size_t)snprintf(m->message, sizeof m->message, "Imported %zu note%s and %zu file%s into \"%.100s\".", m->report.notes, m->report.notes == 1 ? "" : "s", m->report.files, m->report.files == 1 ? "" : "s", j->opt.folder && *j->opt.folder ? j->opt.folder : "the top folder");
        if (m->report.renamed && n < sizeof m->message) n += (size_t)snprintf(m->message + n, sizeof m->message - n, " %zu name%s already existed and got a number.", m->report.renamed, m->report.renamed == 1 ? "" : "s");
        if (m->report.skipped && n < sizeof m->message) n += (size_t)snprintf(m->message + n, sizeof m->message - n, " %zu left out%s%.120s%s.", m->report.skipped, m->report.first_problem[0] ? " (" : "", m->report.first_problem, m->report.first_problem[0] ? ")" : "");
    }
    if (ex_worker_send(self, EX_MSG_IMPORT, m, sizeof *m, free) != 0) free(m);
}

ExImportJob *ex_importjob_start(const ExImportStart *s) {
    ExImportJob *j;
    ExMessage m;
    if (!s || !s->source || !s->vault_root || !s->results) return NULL;
    j = calloc(1, sizeof *j);
    if (!j) return NULL;
    j->format = s->format;
    j->source = dup_s(s->source); j->vault_root = dup_s(s->vault_root);
    j->folder = dup_s(s->opt.folder); j->note_name = dup_s(s->opt.note_name); j->date_format = dup_s(s->opt.date_format); j->time_format = dup_s(s->opt.time_format);
    if (s->opt.template_text && s->opt.template_len) { j->tpl = malloc(s->opt.template_len + 1); if (j->tpl) { memcpy(j->tpl, s->opt.template_text, s->opt.template_len); j->tpl[s->opt.template_len] = 0; } }
    if (!j->source || !j->vault_root || !j->folder || !j->note_name || !j->date_format || !j->time_format) { ex_importjob_free(j); return NULL; }
    j->opt = s->opt;
    j->opt.folder = j->folder; j->opt.note_name = j->note_name; j->opt.date_format = j->date_format; j->opt.time_format = j->time_format;
    j->opt.template_text = j->tpl; j->opt.template_len = j->tpl ? s->opt.template_len : 0;
    j->worker = ex_worker_start("import", 10, job_fn, j, s->results);
    if (!j->worker) { ex_importjob_free(j); return NULL; }
    memset(&m, 0, sizeof m);
    m.type = 1;
    if (ex_worker_post(j->worker, &m) != 0) { ex_importjob_free(j); return NULL; }
    return j;
}

void ex_importjob_cancel(ExImportJob *j) { if (j) j->cancel = 1; }

void ex_importjob_free(ExImportJob *j) {
    if (!j) return;
    j->cancel = 1;
    if (j->worker) ex_worker_stop(j->worker);
    free(j->source); free(j->vault_root); free(j->folder); free(j->note_name); free(j->tpl); free(j->date_format); free(j->time_format);
    free(j);
}
