#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL.h>

#include "exhaust_notes.h"
#include "exhaust_platform.h"
#include "exhaust_util.h"
#include "exhaust_worker.h"

enum { JOB_SAVE = 1 };
enum { RES_OK = 1, RES_FAIL = 2, RES_CONFLICT = 3 };

typedef struct PostEdit { uint64_t offset, removed; char *text; size_t len; } PostEdit;

struct ExNote {
    ExNotes *m;
    struct ExNote *next;
    uint64_t id;
    char *rel, *abs;
    ExDoc *doc;
    ExNoteFormat fmt;
    int pending_new;            /* not on disk yet */
    int64_t disk_size;          /* what we last knew of the file: size and hash; size -1: absent */
    uint8_t disk_hash[16];
    unsigned long edit_seq;
    int dirty;
    uint64_t first_unsaved_ms, last_edit_ms, next_retry_ms;
    int saving; unsigned long saving_seq;
    PostEdit *post; size_t npost, cappost; /* edits made while a save is in flight */
    int has_error; char error[800];
    int conflict;
    uint8_t *conflict_bytes; size_t conflict_bytes_len;   /* the file on disk, raw */
    char *conflict_text; size_t conflict_text_len;        /* ...in editing form */
    int force_next;             /* KEEP_MINE: write over the file even though it differs from what we knew */
    int recovered;
    int suppress_journal;
    int check_disk;
};

struct ExNotes {
    ExVault *vault;
    ExNotesConfig cfg;
    char *data_dir, *journal_dir, *recovery_dir;
    ExJournalWriter *jw;
    ExWatcher *watcher;
    ExWorker *saver;
    ExQueue *results;
    ExNote *notes;
    uint64_t next_id;
    ExNoteEvent *events; size_t nev, cap_ev;
    ExRecovery recovery;        /* notes that had unsaved edits at launch */
    long journal_number;
};

typedef struct SaveJob {
    uint64_t note_id; unsigned long seq;
    char *abs; uint8_t *bytes; size_t len;
    int64_t expect_size; uint8_t expect_hash[16];
    int force;
} SaveJob;

typedef struct SaveResult {
    uint64_t note_id; unsigned long seq;
    int64_t size; uint8_t hash[16];
    char message[200];
    uint8_t *disk; size_t disk_len;
} SaveResult;

static char *dup_str(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }

static uint64_t now_ms(const ExNotes *m) { return m->cfg.clock ? m->cfg.clock(m->cfg.clock_user) : ex_platform_now_ms(); }

static void push_event(ExNotes *m, ExNoteEventKind kind, ExNote *n, const char *rel, const char *msg) {
    ExNoteEvent *e;
    if (m->nev == m->cap_ev) {
        size_t nc = m->cap_ev ? m->cap_ev * 2 : 16;
        ExNoteEvent *ne = realloc(m->events, nc * sizeof *ne);
        if (!ne) return;
        m->events = ne;
        m->cap_ev = nc;
    }
    e = &m->events[m->nev++];
    memset(e, 0, sizeof *e);
    e->kind = kind;
    e->note = n;
    snprintf(e->rel, sizeof e->rel, "%s", rel ? rel : (n ? n->rel : ""));
    snprintf(e->message, sizeof e->message, "%s", msg ? msg : "");
}

int ex_notes_poll_event(ExNotes *m, ExNoteEvent *ev) {
    if (m->nev == 0) return 0;
    *ev = m->events[0];
    memmove(m->events, m->events + 1, (m->nev - 1) * sizeof *m->events);
    m->nev--;
    return 1;
}

/* ---- reading a file and what we know about it ---- */

/* Reads a file; returns 0 with *bytes (malloc'ed), -1 if it cannot be read, 1 if it does not exist. */
static int read_disk(const char *abs, uint8_t **bytes, size_t *len) {
    ExPathInfo info;
    char *data;
    if (ex_platform_path_info(abs, &info) != 0) { *bytes = NULL; *len = 0; return 1; }
    if (!info.is_file) return -1;
    data = ex_platform_read_file(abs, len);
    if (!data) return -1;
    *bytes = (uint8_t *)data;
    return 0;
}

static void empty_hash(uint8_t out[16]) { ex_hash16("", 0, out); }

/* ---- the save worker ---- */

static void save_job(ExWorker *self, const ExMessage *msg, void *user) {
    SaveJob *j = msg->payload;
    SaveResult *r = calloc(1, sizeof *r);
    ExSaveResult sr;
    (void)user;
    if (!r) return;
    r->note_id = j->note_id;
    r->seq = j->seq;
    if (!j->force) {
        /* the file must still be what we last read or wrote: otherwise someone else changed it */
        uint8_t *cur = NULL, hash[16];
        size_t cur_len = 0;
        int rc = read_disk(j->abs, &cur, &cur_len);
        if (rc == 0) ex_hash16(cur, cur_len, hash);
        {
            int same = rc == 1 ? j->expect_size < 0 : (rc == 0 && (int64_t)cur_len == j->expect_size && memcmp(hash, j->expect_hash, 16) == 0);
            if (rc < 0) { snprintf(r->message, sizeof r->message, "cannot read the file to check it"); ex_worker_send(self, RES_FAIL, r, sizeof *r, free); free(cur); goto cleanup; }
            if (!same) { r->disk = cur; r->disk_len = cur_len; r->size = rc == 1 ? -1 : (int64_t)cur_len; if (rc == 0) memcpy(r->hash, hash, 16); ex_worker_send(self, RES_CONFLICT, r, sizeof *r, NULL); goto cleanup; }
        }
        free(cur);
    }
    if (ex_platform_save_file(j->abs, j->bytes, j->len, &sr) == 0) {
        r->size = (int64_t)j->len;
        ex_hash16(j->bytes, j->len, r->hash);
        ex_worker_send(self, RES_OK, r, sizeof *r, free);
    } else {
        snprintf(r->message, sizeof r->message, "%s", sr.message);
        ex_worker_send(self, RES_FAIL, r, sizeof *r, free);
    }
cleanup:
    free(j->abs); free(j->bytes); free(j);
}

/* ---- notes ---- */

static ExNote *find_by_id(ExNotes *m, uint64_t id) { ExNote *n; for (n = m->notes; n; n = n->next) if (n->id == id) return n; return NULL; }
static ExNote *find_by_rel(ExNotes *m, const char *rel) { ExNote *n; for (n = m->notes; n; n = n->next) if (strcmp(n->rel, rel) == 0) return n; return NULL; }

static void on_edit(void *user, size_t offset, size_t removed, const char *ins, size_t len) {
    ExNote *n = user;
    ExNotes *m = n->m;
    uint64_t t;
    if (n->suppress_journal) return;
    ex_jw_edit(m->jw, n->rel, offset, removed, ins, len);
    n->edit_seq++;
    t = now_ms(m);
    if (!n->dirty) { n->dirty = 1; n->first_unsaved_ms = t; }
    n->last_edit_ms = t;
    if (n->saving) { /* remember it: after the save finishes it is re-journaled behind the saved marker */
        if (n->npost == n->cappost) {
            size_t nc = n->cappost ? n->cappost * 2 : 16;
            PostEdit *np = realloc(n->post, nc * sizeof *np);
            if (!np) return;
            n->post = np;
            n->cappost = nc;
        }
        n->post[n->npost].offset = offset;
        n->post[n->npost].removed = removed;
        n->post[n->npost].len = len;
        n->post[n->npost].text = malloc(len + 1);
        if (n->post[n->npost].text) { memcpy(n->post[n->npost].text, ins, len); n->post[n->npost].text[len] = 0; n->npost++; }
    }
}

static void clear_post(ExNote *n) {
    size_t i;
    for (i = 0; i < n->npost; i++) free(n->post[i].text);
    n->npost = 0;
}

static void free_conflict(ExNote *n) {
    free(n->conflict_bytes); free(n->conflict_text);
    n->conflict_bytes = NULL; n->conflict_text = NULL; n->conflict_bytes_len = n->conflict_text_len = 0;
    n->conflict = 0;
}

static ExNote *new_note(ExNotes *m, const char *rel, const char *text, size_t len) {
    ExNote *n = calloc(1, sizeof *n);
    if (!n) return NULL;
    n->m = m;
    n->id = ++m->next_id;
    n->rel = dup_str(rel);
    n->abs = ex_vault_abs_path(m->vault, rel);
    n->doc = ex_doc_create(text, len);
    if (!n->rel || !n->abs || !n->doc) { free(n->rel); free(n->abs); ex_doc_free(n->doc); free(n); return NULL; }
    n->disk_size = -1;
    ex_doc_set_edit_listener(n->doc, on_edit, n);
    n->next = m->notes;
    m->notes = n;
    return n;
}

static void snapshot_to_recovery(ExNotes *m, const char *rel, const void *bytes, size_t len) {
    char name[700], *path;
    size_t i, k = 0;
    time_t t = (time_t)ex_platform_unix_time();
    struct tm *tmv = localtime(&t);
    char stamp[32];
    for (i = 0; rel[i] && k < 500; i++) name[k++] = rel[i] == '/' ? '_' : rel[i];
    if (k < 2) return;
    name[k] = 0;
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", tmv ? tmv : localtime(&t));
    path = malloc(strlen(m->recovery_dir) + strlen(name) + 64);
    if (!path) return;
    sprintf(path, "%s/%s.%s.bak", m->recovery_dir, name, stamp);
    ex_platform_make_dirs(m->recovery_dir);
    ex_platform_write_file_atomic(path, bytes, len);
    free(path);
}

/* ---- creating the manager ---- */

ExNotes *ex_notes_create(ExVault *vault, const ExNotesConfig *cfg) {
    ExNotes *m = calloc(1, sizeof *m);
    ExVaultOptions vo;
    char *base = NULL;
    if (!m) return NULL;
    m->vault = vault;
    if (cfg) m->cfg = *cfg;
    if (m->cfg.autosave_idle_ms <= 0) m->cfg.autosave_idle_ms = 1000;
    if (m->cfg.autosave_max_ms <= 0) m->cfg.autosave_max_ms = 5000;
    if (m->cfg.journal_flush_ms <= 0) m->cfg.journal_flush_ms = 200;
    if (m->cfg.retry_ms <= 0) m->cfg.retry_ms = 2000;
    if (m->cfg.poll_ms <= 0) m->cfg.poll_ms = 2000;
    if (m->cfg.data_dir) base = dup_str(m->cfg.data_dir);
    else {
        char *root = ex_platform_local_data_dir();
        if (root) { base = malloc(strlen(root) + 80); if (base) sprintf(base, "%s/vaults/%s", root, ex_vault_id(vault)); free(root); }
    }
    if (!base) { free(m); return NULL; }
    m->data_dir = base;
    m->journal_dir = malloc(strlen(base) + 16);
    m->recovery_dir = malloc(strlen(base) + 16);
    if (!m->journal_dir || !m->recovery_dir) { ex_notes_destroy(m); return NULL; }
    sprintf(m->journal_dir, "%s/journal", base);
    sprintf(m->recovery_dir, "%s/recovery", base);
    ex_platform_make_dirs(base);

    /* what the last run left behind, before this run's journal file exists */
    if (ex_recovery_scan(m->journal_dir, &m->recovery) != 0) memset(&m->recovery, 0, sizeof m->recovery);
    m->jw = ex_jw_create(m->journal_dir, m->cfg.journal_flush_ms);
    if (m->recovery.n == 0 && m->jw) ex_journal_compact(m->journal_dir, ex_jw_number(m->jw)); /* nothing pending: older journals can go; this run's file stays */
    m->results = ex_queue_create(NULL, NULL);
    m->saver = m->results ? ex_worker_start("save", 101, save_job, m, m->results) : NULL;
    if (!m->jw || !m->saver) { ex_notes_destroy(m); return NULL; }
    ex_vault_get_options(vault, &vo);
    m->watcher = ex_watcher_start(ex_vault_path(vault), m->cfg.watch_backend, vo.excluded, vo.nexcluded, m->cfg.poll_ms);
    return m;
}

int ex_notes_watcher_polling(const ExNotes *m) { return m->watcher ? ex_watcher_is_polling(m->watcher) : 0; }
size_t ex_notes_unsaved_count(ExNotes *m) { return m->recovery.n; }

/* ---- opening ---- */

static const ExRecoveredNote *recovered_for(ExNotes *m, const char *rel, size_t *index) {
    size_t i;
    for (i = 0; i < m->recovery.n; i++) if (strcmp(m->recovery.notes[i].path, rel) == 0) { if (index) *index = i; return &m->recovery.notes[i]; }
    return NULL;
}

static void drop_recovered(ExNotes *m, size_t i) {
    ExRecoveredNote *r = &m->recovery.notes[i];
    size_t k;
    for (k = 0; k < r->nedits; k++) free(r->edits[k].text);
    free(r->edits); free(r->path);
    memmove(r, r + 1, (m->recovery.n - i - 1) * sizeof *r);
    m->recovery.n--;
}

/* Applies journaled edits to a document; returns how many fitted before one did not. */
static size_t replay(ExNote *n, const ExRecoveredNote *r) {
    size_t i;
    n->suppress_journal = 1;
    for (i = 0; i < r->nedits; i++) {
        size_t size = ex_buffer_size(ex_doc_buffer(n->doc));
        ExChange c;
        if (r->edits[i].offset > size || r->edits[i].removed > size - r->edits[i].offset) break;
        c.offset = (size_t)r->edits[i].offset; c.remove_len = (size_t)r->edits[i].removed; c.text = r->edits[i].text; c.text_len = r->edits[i].len;
        if (ex_doc_apply(n->doc, &c, 1, 0, NULL, 0) != 0) break;
    }
    n->suppress_journal = 0;
    return i;
}

ExNote *ex_notes_open(ExNotes *m, const char *rel, ExOpenResult *result) {
    ExNote *n = find_by_rel(m, rel);
    ExOpenResult res = EX_OPEN_OK;
    char *abs, *text = NULL;
    size_t tlen = 0, ri;
    uint8_t *bytes = NULL;
    size_t blen = 0;
    ExNoteFormat fmt;
    const ExRecoveredNote *rec;
    int rd;
    if (n) { if (result) *result = EX_OPEN_OK; return n; }
    abs = ex_vault_abs_path(m->vault, rel);
    if (!abs) { if (result) *result = EX_OPEN_IO; return NULL; }
    rd = read_disk(abs, &bytes, &blen);
    rec = recovered_for(m, rel, &ri);
    memset(&fmt, 0, sizeof fmt);
    if (rd < 0) { free(abs); if (result) *result = EX_OPEN_IO; return NULL; }
    if (rd == 1 && !rec) { free(abs); if (result) *result = EX_OPEN_NOT_FOUND; return NULL; }
    if (rd == 0 && ex_note_decode(bytes, blen, &text, &tlen, &fmt) != 0) { free(bytes); free(abs); if (result) *result = EX_OPEN_INVALID_UTF8; return NULL; }
    if (rd == 1) { text = dup_str(""); tlen = 0; }
    n = new_note(m, rel, text, tlen);
    free(abs);
    free(text);
    if (!n) { free(bytes); if (result) *result = EX_OPEN_IO; return NULL; }
    n->fmt = fmt;
    if (rd == 0) { n->disk_size = (int64_t)blen; ex_hash16(bytes, blen, n->disk_hash); }
    else { n->disk_size = -1; n->pending_new = 1; }
    {
        uint8_t h[16];
        uint64_t sz = rd == 0 ? blen : 0;
        if (rd == 0) memcpy(h, n->disk_hash, 16); else empty_hash(h);
        if (rec) {
            int same_base = !rec->base_changed && rec->base_size == sz && memcmp(rec->base_hash, h, 16) == 0;
            size_t fitted = replay(n, rec);
            n->recovered = 1;
            n->dirty = n->edit_seq == 0 ? 1 : 1;                  /* the buffer differs from the disk: autosave writes it */
            n->first_unsaved_ms = n->last_edit_ms = now_ms(m);
            if (fitted < rec->nedits) {
                /* the edits do not fit this file: they stay in the journal; open what is on disk */
                snprintf(n->error, sizeof n->error, "Unsaved edits of %s could not be re-applied to the file on disk. They are kept in the recovery journal.", rel);
                n->has_error = 1;
                n->dirty = 0;
                res = EX_OPEN_CONFLICT;
            } else if (!same_base) {
                /* the file changed since: keep both versions (the buffer holds the recovered text) */
                n->conflict = 1;
                n->conflict_bytes = bytes; n->conflict_bytes_len = blen; bytes = NULL;
                {
                    char *t2 = NULL; size_t l2 = 0; ExNoteFormat f2;
                    if (n->conflict_bytes && ex_note_decode(n->conflict_bytes, n->conflict_bytes_len, &t2, &l2, &f2) == 0) { n->conflict_text = t2; n->conflict_text_len = l2; }
                }
                res = EX_OPEN_CONFLICT;
            } else res = EX_OPEN_RECOVERED;
            if (fitted == rec->nedits) drop_recovered(m, ri);
        } else if (rd == 0) {
            ex_jw_base(m->jw, rel, (uint64_t)blen, n->disk_hash);
        } else {
            ex_jw_base(m->jw, rel, 0, h);
        }
    }
    free(bytes);
    if (result) *result = res;
    return n;
}

ExNote *ex_notes_new(ExNotes *m, const char *parent_rel, const char *name) {
    char rel[600], abs_check[1];
    int k;
    ExNote *n = NULL;
    (void)abs_check;
    for (k = 1; k < 1000 && !n; k++) {
        char *abs;
        ExPathInfo info;
        if (k == 1) snprintf(rel, sizeof rel, "%s%s%s.md", parent_rel, parent_rel[0] ? "/" : "", name);
        else snprintf(rel, sizeof rel, "%s%s%s %d.md", parent_rel, parent_rel[0] ? "/" : "", name, k);
        abs = ex_vault_abs_path(m->vault, rel);
        if (abs && ex_platform_path_info(abs, &info) != 0 && !find_by_rel(m, rel)) {
            n = new_note(m, rel, "", 0);
            if (n) { n->pending_new = 1; n->disk_size = -1; empty_hash(n->disk_hash); }
        }
        free(abs);
    }
    return n;
}

ExDoc *ex_note_doc(ExNote *n) { return n->doc; }
const char *ex_note_path(const ExNote *n) { return n->rel; }
int ex_note_dirty(const ExNote *n) { return n->dirty; }
int ex_note_exists_on_disk(const ExNote *n) { return !n->pending_new; }
const char *ex_note_error(const ExNote *n) { return n->has_error ? n->error : NULL; }
int ex_note_has_conflict(const ExNote *n) { return n->conflict; }
const char *ex_note_conflict_disk_text(const ExNote *n) { return n->conflict_text; }
int ex_note_recovered(const ExNote *n) { return n->recovered; }
const ExNoteFormat *ex_note_format(const ExNote *n) { return &n->fmt; }

char *ex_note_conflict_diff(const ExNote *n) {
    char *mine = ex_buffer_text(ex_doc_buffer(n->doc), NULL), *d;
    if (!mine) return NULL;
    d = ex_diff_unified(mine, n->conflict_text ? n->conflict_text : "", "my version (this window)", "version on disk");
    free(mine);
    return d;
}

/* ---- saving ---- */

static int start_save(ExNote *n) {
    ExNotes *m = n->m;
    char *text;
    size_t tlen, blen = 0;
    uint8_t *bytes;
    SaveJob *j;
    ExMessage msg;
    if (n->saving) return 0;
    if (n->pending_new && ex_buffer_size(ex_doc_buffer(n->doc)) == 0) { n->dirty = 0; return 0; } /* nothing typed: no file */
    text = ex_buffer_text(ex_doc_buffer(n->doc), &tlen);
    if (!text) return -1;
    bytes = ex_note_encode(text, tlen, &n->fmt, &blen);
    free(text);
    if (!bytes) return -1;
    j = calloc(1, sizeof *j);
    if (!j) { free(bytes); return -1; }
    j->note_id = n->id;
    j->seq = n->edit_seq;
    j->abs = dup_str(n->abs);
    j->bytes = bytes;
    j->len = blen;
    j->expect_size = n->disk_size;
    memcpy(j->expect_hash, n->disk_hash, 16);
    j->force = n->force_next;
    n->force_next = 0;
    memset(&msg, 0, sizeof msg);
    msg.type = JOB_SAVE;
    msg.payload = j;
    msg.size = sizeof *j;
    if (!j->abs || ex_worker_post(m->saver, &msg) != 0) { free(j->abs); free(bytes); free(j); return -1; }
    n->saving = 1;
    n->saving_seq = n->edit_seq;
    clear_post(n);
    return 0;
}

static void finish_result(ExNotes *m, int type, SaveResult *r) {
    ExNote *n = find_by_id(m, r->note_id);
    if (!n) { free(r->disk); return; }
    n->saving = 0;
    if (type == RES_OK) {
        int was_new = n->pending_new;
        n->disk_size = r->size;
        memcpy(n->disk_hash, r->hash, 16);
        n->pending_new = 0;
        n->has_error = 0;
        n->error[0] = 0;
        ex_jw_saved(m->jw, n->rel, (uint64_t)r->size, r->hash);
        if (n->edit_seq == r->seq) { n->dirty = 0; n->recovered = n->recovered && 0; }
        else {
            size_t i;                                  /* typed meanwhile: re-journal those edits behind the saved marker */
            for (i = 0; i < n->npost; i++) ex_jw_edit(m->jw, n->rel, n->post[i].offset, n->post[i].removed, n->post[i].text, n->post[i].len);
        }
        clear_post(n);
        push_event(m, EX_NOTE_EVENT_SAVED, n, NULL, NULL);
        if (was_new) ex_vault_rescan(m->vault);
    } else if (type == RES_FAIL) {
        snprintf(n->error, sizeof n->error, "Cannot save %.200s: %.200s. Your text is safe in the recovery journal; saving will be retried.", n->rel, r->message);
        n->has_error = 1;
        n->next_retry_ms = now_ms(m) + (uint64_t)m->cfg.retry_ms;
        clear_post(n);
        push_event(m, EX_NOTE_EVENT_SAVE_FAILED, n, NULL, n->error);
    } else { /* conflict: the file changed on disk while we had unsaved edits */
        free_conflict(n);
        n->conflict = 1;
        n->conflict_bytes = r->disk; n->conflict_bytes_len = r->disk_len;
        r->disk = NULL;
        if (n->conflict_bytes) {
            char *t = NULL; size_t l = 0; ExNoteFormat f;
            if (ex_note_decode(n->conflict_bytes, n->conflict_bytes_len, &t, &l, &f) == 0) { n->conflict_text = t; n->conflict_text_len = l; }
        }
        clear_post(n);
        push_event(m, EX_NOTE_EVENT_CONFLICT, n, NULL, NULL);
    }
    free(r->disk);
}

static void pump_results(ExNotes *m) {
    ExMessage msg;
    while (ex_queue_pop(m->results, &msg)) {
        SaveResult *r = msg.payload;
        if (!r) continue;
        finish_result(m, (int)msg.type, r);
        if (msg.free_payload) free(r); else free(r);
    }
}

/* ---- outside changes ---- */

static void check_external(ExNote *n) {
    ExNotes *m = n->m;
    uint8_t *bytes = NULL, hash[16];
    size_t len = 0;
    int rc;
    n->check_disk = 0;
    rc = read_disk(n->abs, &bytes, &len);
    if (rc < 0) return;
    if (rc == 1) { /* deleted or not there */
        if (n->pending_new || n->disk_size < 0) return;
        if (!n->dirty) { n->disk_size = -1; return; }            /* the next save writes the buffer again */
        n->disk_size = -1;
        return;
    }
    ex_hash16(bytes, len, hash);
    if ((int64_t)len == n->disk_size && memcmp(hash, n->disk_hash, 16) == 0) { free(bytes); return; } /* our own save, or a touch */
    {
        char *text = NULL; size_t tlen = 0; ExNoteFormat fmt;
        int ok = ex_note_decode(bytes, len, &text, &tlen, &fmt) == 0;
        if (n->dirty || n->saving) {
            free_conflict(n);
            n->conflict = 1;
            n->conflict_bytes = bytes; n->conflict_bytes_len = len; bytes = NULL;
            if (ok) { n->conflict_text = text; n->conflict_text_len = tlen; text = NULL; }
            push_event(m, EX_NOTE_EVENT_CONFLICT, n, NULL, NULL);
        } else if (ok) {
            /* the buffer has no unsaved edits: show the new text, as one undoable edit that keeps the
             * unchanged start and end of the text (so the caret and the scroll position stay put) */
            char *old = ex_buffer_text(ex_doc_buffer(n->doc), NULL);
            size_t olen = old ? strlen(old) : 0, pre = 0, suf = 0;
            while (pre < olen && pre < tlen && old[pre] == text[pre]) pre++;
            while (suf < olen - pre && suf < tlen - pre && old[olen - 1 - suf] == text[tlen - 1 - suf]) suf++;
            while (pre > 0 && ((unsigned char)old[pre] & 0xC0) == 0x80) pre--;             /* stay on character boundaries */
            while (suf > 0 && ((unsigned char)text[tlen - suf] & 0xC0) == 0x80) suf--;
            {
                ExChange c;
                c.offset = pre; c.remove_len = olen - pre - suf; c.text = text + pre; c.text_len = tlen - pre - suf;
                n->suppress_journal = 1;
                if (c.remove_len || c.text_len) ex_doc_apply(n->doc, &c, 1, 0, NULL, 0);
                n->suppress_journal = 0;
            }
            free(old);
            n->fmt = fmt;
            n->disk_size = (int64_t)len;
            memcpy(n->disk_hash, hash, 16);
            ex_jw_base(m->jw, n->rel, (uint64_t)len, hash);
            push_event(m, EX_NOTE_EVENT_RELOADED, n, NULL, NULL);
        } else {
            snprintf(n->error, sizeof n->error, "%s was changed outside Exhaust and is no longer valid UTF-8, so it was not reloaded.", n->rel);
            n->has_error = 1;
            n->disk_size = (int64_t)len;
            memcpy(n->disk_hash, hash, 16);
        }
        free(text);
    }
    free(bytes);
}

/* ---- conflicts ---- */

int ex_note_resolve_conflict(ExNote *n, ExConflictChoice choice) {
    ExNotes *m = n->m;
    if (!n->conflict) return 0;
    if (choice == EX_KEEP_MINE) {
        if (n->conflict_bytes) snapshot_to_recovery(m, n->rel, n->conflict_bytes, n->conflict_bytes_len); /* the other side is kept first */
        if (n->conflict_bytes) { n->disk_size = (int64_t)n->conflict_bytes_len; ex_hash16(n->conflict_bytes, n->conflict_bytes_len, n->disk_hash); }
        else n->disk_size = -1;
        n->force_next = 1;
        n->next_retry_ms = 0;
        n->dirty = 1;
        n->first_unsaved_ms = n->last_edit_ms = now_ms(m) - (uint64_t)m->cfg.autosave_max_ms;
    } else {
        char *mine = ex_buffer_text(ex_doc_buffer(n->doc), NULL);
        if (mine) { snapshot_to_recovery(m, n->rel, mine, strlen(mine)); free(mine); }       /* my text is kept first */
        if (n->conflict_text) {
            ExChange c;
            size_t size = ex_buffer_size(ex_doc_buffer(n->doc));
            c.offset = 0; c.remove_len = size; c.text = n->conflict_text; c.text_len = n->conflict_text_len;
            n->suppress_journal = 1;
            ex_doc_apply(n->doc, &c, 1, 0, NULL, 0);
            n->suppress_journal = 0;
            {   ExNoteFormat f; char *t = NULL; size_t l; if (ex_note_decode(n->conflict_bytes, n->conflict_bytes_len, &t, &l, &f) == 0) { n->fmt = f; free(t); } }
        }
        if (n->conflict_bytes) { n->disk_size = (int64_t)n->conflict_bytes_len; ex_hash16(n->conflict_bytes, n->conflict_bytes_len, n->disk_hash); }
        n->dirty = 0;
        ex_jw_saved(m->jw, n->rel, (uint64_t)(n->disk_size < 0 ? 0 : n->disk_size), n->disk_hash); /* nothing of mine is pending any more */
    }
    free_conflict(n);
    return 0;
}

/* ---- the loop ---- */

void ex_notes_tick(ExNotes *m) {
    uint64_t t = now_ms(m);
    ExNote *n;
    char rel[512];
    pump_results(m);
    if (m->watcher) {
        int tree_changed = 0;
        while (ex_watcher_poll(m->watcher, rel, sizeof rel)) {
            ExNote *open = find_by_rel(m, rel);
            if (open) open->check_disk = 1;
            tree_changed = 1;
        }
        if (tree_changed) push_event(m, EX_NOTE_EVENT_TREE_CHANGED, NULL, "", NULL);
    }
    for (n = m->notes; n; n = n->next) {
        if (n->check_disk && !n->saving) check_external(n);
        if (n->dirty && !n->conflict && !n->saving && t >= n->next_retry_ms &&
            (t - n->last_edit_ms >= (uint64_t)m->cfg.autosave_idle_ms || t - n->first_unsaved_ms >= (uint64_t)m->cfg.autosave_max_ms))
            start_save(n);
    }
    ex_jw_tick(m->jw, t);
}

static void wait_saves(ExNotes *m, uint64_t timeout_ms) {
    uint64_t start = ex_platform_now_ms();
    for (;;) {
        ExNote *n;
        int busy = 0;
        pump_results(m);
        for (n = m->notes; n; n = n->next) if (n->saving) busy = 1;
        if (!busy || ex_platform_now_ms() - start > timeout_ms) return;
        SDL_Delay(2);
    }
}

int ex_note_save_now(ExNote *n) {
    ExNotes *m = n->m;
    wait_saves(m, 5000);
    if (n->dirty && !n->conflict) { n->next_retry_ms = 0; start_save(n); wait_saves(m, 5000); }
    return n->dirty || n->has_error ? -1 : 0;
}

int ex_notes_flush(ExNotes *m) {
    ExNote *n;
    int failed = 0;
    ex_jw_flush(m->jw);
    wait_saves(m, 5000);
    for (n = m->notes; n; n = n->next) if (n->dirty && !n->conflict) { n->next_retry_ms = 0; start_save(n); }
    wait_saves(m, 5000);
    for (n = m->notes; n; n = n->next) if (n->dirty) failed++;
    ex_jw_flush(m->jw);
    return failed;
}

int ex_notes_close(ExNotes *m, ExNote *n) {
    ExNote **pp;
    int rc = ex_note_save_now(n);
    for (pp = &m->notes; *pp && *pp != n; pp = &(*pp)->next) {}
    if (*pp) *pp = n->next;
    free_conflict(n);
    clear_post(n);
    free(n->post);
    ex_doc_free(n->doc);
    free(n->rel); free(n->abs);
    free(n);
    return rc;
}

static void destroy(ExNotes *m, int save) {
    ExNote *n, *next;
    if (!m) return;
    if (save && m->jw && m->saver) ex_notes_flush(m);
    ex_watcher_stop(m->watcher);
    ex_worker_stop(m->saver);
    pump_results(m);
    ex_queue_destroy(m->results);
    for (n = m->notes; n; n = next) {
        next = n->next;
        free_conflict(n); clear_post(n); free(n->post);
        ex_doc_free(n->doc);
        free(n->rel); free(n->abs); free(n);
    }
    if (m->jw) {
        long keep = -2;
        (void)keep;
        ex_jw_close(m->jw);
    }
    ex_recovery_free(&m->recovery);
    free(m->events); free(m->data_dir); free(m->journal_dir); free(m->recovery_dir);
    free(m);
}

void ex_notes_destroy(ExNotes *m) { destroy(m, 1); }
void ex_notes_destroy_without_saving(ExNotes *m) { destroy(m, 0); }
