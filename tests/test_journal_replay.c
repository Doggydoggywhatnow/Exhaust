/* journal_replay: the edits of a random editing session, written as journal
 * records and replayed onto a fresh copy of the starting text, give the same
 * text. Also: damaged and truncated journals, saved records, other paths,
 * unknown record types and versions. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_buffer.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(uint32_t n) {
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17;
    return (uint32_t)((rng_state >> 11) % n);
}

typedef struct Rec { ExBytes j; const char *path; uint64_t ts; size_t edits; } Rec;

static void on_edit(void *user, size_t offset, size_t removed, const char *ins, size_t len) {
    Rec *r = user;
    CHECK(ex_journal_write_edit(&r->j, r->ts++, r->path, offset, removed, ins, len) == 0);
    r->edits++;
}

static int buffer_is(const ExBuffer *b, const char *s, size_t n) {
    size_t len = 0;
    char *t = ex_buffer_text(b, &len);
    int ok = t && len == n && memcmp(t, s, n) == 0;
    free(t);
    return ok;
}

int main(void) {
    static const char *WORDS[] = {"alpha", " ", "beta\n", "\xC3\xA9", "\xE4\xBD\xA0\xE5\xA5\xBD", "\xF0\x9F\x98\x80", "x", "\n\n", "gamma delta"};
    const char *start = "# Title\n\nSome text.\n";
    size_t start_len = strlen(start), i, applied = 0;
    ExDoc *d = ex_doc_create(start, start_len);
    Rec rec;
    uint8_t zero_hash[16] = {0};
    int op;

    memset(&rec, 0, sizeof rec);
    rec.path = "notes/a.md";
    CHECK(ex_journal_write_header(&rec.j) == 0);
    CHECK(ex_journal_write_mark(&rec.j, 'B', 1, rec.path, start_len, zero_hash) == 0);
    ex_doc_set_edit_listener(d, on_edit, &rec);

    /* a random session: typing, multi-cursor edits, deletes, undo, redo, jumps in the undo tree */
    for (op = 0; op < 3000; op++) {
        uint32_t kind = rnd(100);
        size_t sz = ex_buffer_size(ex_doc_buffer(d));
        if (kind < 60) {
            ExSel s[3];
            size_t k, nsel = 1 + rnd(3);
            const char *w = WORDS[rnd(sizeof WORDS / sizeof WORDS[0])];
            for (k = 0; k < nsel; k++) {
                size_t a = rnd((uint32_t)sz + 1);
                s[k].anchor = a;
                s[k].head = rnd(3) ? a : (a + rnd(6) > sz ? sz : a + rnd(6));
            }
            ex_doc_set_selections(d, s, nsel);
            if (sz > 3000) w = "";
            ex_doc_replace_selections(d, w, strlen(w), rnd(2));
        } else if (kind < 80) ex_doc_undo(d);
        else if (kind < 92) ex_doc_redo(d);
        else ex_doc_goto_node(d, rnd((uint32_t)ex_doc_node_count(d)));
    }
    fprintf(stderr, "journal: %zu edit records, %zu bytes\n", rec.edits, rec.j.len);
    CHECK(rec.edits > 1000);

    {   /* replay onto a fresh copy of the starting text */
        ExBuffer *fresh = ex_buffer_create(start, start_len);
        char *want = ex_buffer_text(ex_doc_buffer(d), NULL);
        CHECK(ex_journal_replay(rec.j.data, rec.j.len, rec.path, fresh, &applied) == 0);
        CHECK(applied == rec.edits);
        CHECK(buffer_is(fresh, want, strlen(want)));
        ex_buffer_free(fresh);

        /* another path's edits are not applied */
        fresh = ex_buffer_create(start, start_len);
        CHECK(ex_journal_replay(rec.j.data, rec.j.len, "other.md", fresh, &applied) == 0 && applied == 0);
        CHECK(buffer_is(fresh, start, start_len));
        ex_buffer_free(fresh);

        /* a truncated tail (power cut in mid-write) replays everything before it */
        for (i = 1; i < 40; i++) {
            size_t cut = rec.j.len - i;
            ExBuffer *f2 = ex_buffer_create(start, start_len);
            size_t n2 = 0;
            CHECK(ex_journal_replay(rec.j.data, cut, rec.path, f2, &n2) == 0);
            CHECK(n2 + 1 >= rec.edits - (i / 20) - 1 && n2 <= rec.edits);
            ex_buffer_free(f2);
        }

        /* a flipped byte ends replay at that record */
        {
            ExBytes copy;
            ExBuffer *f3 = ex_buffer_create(start, start_len);
            size_t n3 = 0, at = rec.j.len / 2;
            copy.data = malloc(rec.j.len);
            memcpy(copy.data, rec.j.data, rec.j.len);
            copy.len = copy.cap = rec.j.len;
            copy.data[at] ^= 0x40;
            CHECK(ex_journal_replay(copy.data, copy.len, rec.path, f3, &n3) == 0);
            CHECK(n3 > 0 && n3 < rec.edits);
            /* replaying the first n3 edits of the intact journal gives the same text */
            {
                ExBuffer *f4 = ex_buffer_create(start, start_len);
                ExJournalRecord r;
                size_t pos = 8, done = 0;
                while (done < n3 && ex_journal_next(rec.j.data, rec.j.len, &pos, &r) == 1) {
                    if (r.type != 'E') continue;
                    ex_buffer_delete(f4, (size_t)r.offset, (size_t)r.removed);
                    ex_buffer_insert(f4, (size_t)r.offset, r.inserted, r.inserted_len);
                    done++;
                }
                {
                    char *t3 = ex_buffer_text(f3, NULL);
                    CHECK(buffer_is(f4, t3, strlen(t3)));
                    free(t3);
                }
                ex_buffer_free(f4);
            }
            free(copy.data);
            ex_buffer_free(f3);
        }
        free(want);
    }

    /* saved records: only edits after the last saved record for the path are replayed */
    {
        ExBytes j = {0};
        ExBuffer *b = ex_buffer_create("abc", 3);
        size_t n = 0;
        ex_journal_write_header(&j);
        ex_journal_write_edit(&j, 1, "a.md", 3, 0, "d", 1);          /* before the save: already in the file */
        ex_journal_write_mark(&j, 'S', 2, "a.md", 4, zero_hash);
        ex_journal_write_edit(&j, 3, "b.md", 0, 0, "ZZ", 2);         /* another path */
        ex_journal_write_edit(&j, 4, "a.md", 0, 1, "X", 1);          /* "abc" -> "Xbc" */
        CHECK(ex_journal_replay(j.data, j.len, "a.md", b, &n) == 0 && n == 1 && buffer_is(b, "Xbc", 3));
        /* an edit that does not fit the text means the journal belongs to another file */
        ex_journal_write_edit(&j, 5, "a.md", 100, 0, "q", 1);
        ex_buffer_free(b);
        b = ex_buffer_create("abc", 3);
        CHECK(ex_journal_replay(j.data, j.len, "a.md", b, &n) == -1);
        ex_buffer_free(b);
        ex_bytes_free(&j);
    }

    /* versions and unknown record types */
    {
        ExBytes j = {0};
        uint8_t body[1 + 8 + 2 + 4] = {'Z', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'p', 'q', 'r', 's'};
        ExBuffer *b = ex_buffer_create("abc", 3);
        size_t n = 0;
        ExJournalRecord r;
        size_t pos = 8;
        uint8_t head[4], tail[4];
        uint32_t crc;
        ex_journal_write_header(&j);
        /* hand-built record of an unknown type 'Z', with a valid checksum */
        {
            uint32_t blen = (uint32_t)sizeof body + 4;
            ExBytes raw = {0};
            (void)raw;
            head[0] = (uint8_t)blen; head[1] = head[2] = head[3] = 0;
            crc = ex_crc32(0, body, sizeof body);
            tail[0] = (uint8_t)crc; tail[1] = (uint8_t)(crc >> 8); tail[2] = (uint8_t)(crc >> 16); tail[3] = (uint8_t)(crc >> 24);
            j.data = realloc(j.data, j.len + 4 + sizeof body + 4);
            memcpy(j.data + j.len, head, 4);
            memcpy(j.data + j.len + 4, body, sizeof body);
            memcpy(j.data + j.len + 4 + sizeof body, tail, 4);
            j.len += 4 + sizeof body + 4;
            j.cap = j.len; /* the buffer was resized by hand: keep its capacity honest */
        }
        ex_journal_write_edit(&j, 1, "a.md", 0, 0, ">", 1);
        CHECK(ex_journal_next(j.data, j.len, &pos, &r) == 1 && r.type == 'Z');   /* skipped by callers, not fatal */
        CHECK(ex_journal_replay(j.data, j.len, "a.md", b, &n) == 0 && n == 1 && buffer_is(b, ">abc", 4));
        /* a newer file version is refused, not replayed and not changed */
        j.data[4] = 2;
        CHECK(ex_journal_check_header(j.data, j.len) == -1);
        CHECK(ex_journal_replay(j.data, j.len, "a.md", b, &n) == -1 && buffer_is(b, ">abc", 4));
        free(j.data);
        ex_buffer_free(b);
    }

    CHECK(ex_crc32(0, "123456789", 9) == 0xCBF43926u); /* the standard check value */
    ex_bytes_free(&rec.j);
    ex_doc_free(d);
    return failures ? 1 : 0;
}
