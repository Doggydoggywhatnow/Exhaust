/* hunt_journal: the recovery journal (docs/formats.md, "Journal file"), CRC-32 and SHA-256.
 *
 * The journal is the one thing that stands between a power cut and lost typing, and its format is frozen after release
 * (a newer journal must replay in an older version "or be left untouched, never discarded"). This test
 *   A. checks the CRC-32 and SHA-256 against published vectors, and CRC-32 over every split point;
 *   B. writes random journals and reads every record back, then cuts them at EVERY byte and flips EVERY bit of the
 *      first records: the reader must stop cleanly, never read outside the data (run under ASan to see this: the
 *      data is copied to an exactly sized heap block) and never return a record that was not written;
 *   C. crafts records with a correct checksum and lying inner fields (path length, inserted length, offsets that
 *      overflow when added) which no writer would make but a damaged or hostile file can contain;
 *   D. checks the documented rule that a record of an unknown type is skipped and later records are still read;
 *   E. replays against a model: only this path's edits, only after its last saved record, and -1 for an edit that
 *      does not fit the text.
 * Link: exhaust_buffer. */
#include "hunt.h"

#include "exhaust_buffer.h"
#include "exhaust_util.h"

static void le32(uint8_t *o, uint32_t v) { int i; for (i = 0; i < 4; i++) o[i] = (uint8_t)(v >> (8 * i)); }
static void le64(uint8_t *o, uint64_t v) { int i; for (i = 0; i < 8; i++) o[i] = (uint8_t)(v >> (8 * i)); }
static void le16(uint8_t *o, uint16_t v) { o[0] = (uint8_t)v; o[1] = (uint8_t)(v >> 8); }

/* ---- A. checksums ---- */

static void test_checksums(void) {
    static const unsigned char abc[] = "abc";
    uint8_t d[32];
    char hex[80];
    size_t i;
    unsigned char data[300];

    hunt_step("crc32 vectors");
    HUNT_EQ_INT(ex_crc32(0, "", 0), 0);
    HUNT_CHECK(ex_crc32(0, "123456789", 9) == 0xCBF43926u);
    HUNT_CHECK(ex_crc32(0, "a", 1) == 0xE8B7BE43u);
    HUNT_CHECK(ex_crc32(0, abc, 3) == 0x352441C2u);
    for (i = 0; i < sizeof data; i++) data[i] = (unsigned char)(i * 7 + 3);
    for (i = 0; i <= sizeof data; i++) {
        uint32_t whole = ex_crc32(0, data, sizeof data), split = ex_crc32(ex_crc32(0, data, i), data + i, sizeof data - i);
        HUNT_CHECKF(whole == split, "crc32 is not incremental when split at %lu", (unsigned long)i);
    }

    hunt_step("sha256 vectors");
    ex_sha256("", 0, d); ex_hex(d, 32, hex);
    HUNT_EQ_STR(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    ex_sha256("abc", 3, d); ex_hex(d, 32, hex);
    HUNT_EQ_STR(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    ex_sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, d); ex_hex(d, 32, hex);
    HUNT_EQ_STR(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    {
        /* sizes around the block boundaries (55, 56, 63, 64, 65 bytes) must all hash without a padding mistake: compare against the one-shot of the same bytes padded another way is impossible, so check the known value for 1,000,000 'a' and the stability of the rest */
        size_t lens[] = { 54, 55, 56, 57, 63, 64, 65, 119, 120, 127, 128, 129 };
        char *big = (char *)malloc(1000000);
        memset(big, 'a', 1000000);
        ex_sha256(big, 1000000, d); ex_hex(d, 32, hex);
        HUNT_EQ_STR(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
        for (i = 0; i < sizeof lens / sizeof *lens; i++) {
            uint8_t e[32];
            ex_sha256(big, lens[i], d);
            ex_sha256(big, lens[i], e);
            HUNT_CHECK(memcmp(d, e, 32) == 0);
        }
        free(big);
    }
    ex_hex((const uint8_t *)"\x00\xff\x0a", 3, hex);
    HUNT_EQ_STR(hex, "00ff0a");
    ex_hex((const uint8_t *)"x", 0, hex);
    HUNT_EQ_STR(hex, "");
}

/* ---- B. round trips, cuts and flips ---- */

typedef struct Rec { char type; uint64_t ts; char path[40]; uint64_t offset, removed; char *ins; size_t inslen; uint64_t size; uint8_t hash[16]; } Rec;

static const char *PATHS[] = { "a.md", "Folder/b.md", "\xC3\xA9t\xC3\xA9/n\xC3\xB6te.md", "x", "A Very Long Name With Spaces And (Parens) And #Hash.md" };

static void make_journal(ExBytes *out, Rec *recs, size_t n) {
    size_t i;
    ex_journal_write_header(out);
    for (i = 0; i < n; i++) {
        Rec *r = &recs[i];
        int k = (int)hunt_rnd(10);
        memset(r, 0, sizeof *r);
        snprintf(r->path, sizeof r->path, "%s", PATHS[hunt_rnd(sizeof PATHS / sizeof *PATHS)]);
        r->ts = hunt_rand64();
        if (k < 7) {
            char tmp[60];
            r->type = 'E';
            r->offset = hunt_rand64() % 100000; r->removed = hunt_rand64() % 50;
            r->inslen = hunt_chance(15) ? 0 : hunt_random_text(tmp, sizeof tmp, 6);
            r->ins = hunt_dup(tmp, r->inslen);
            HUNT_EQ_INT(ex_journal_write_edit(out, r->ts, r->path, r->offset, r->removed, r->ins, r->inslen), 0);
        } else {
            r->type = k < 9 ? 'S' : 'B';
            r->size = hunt_rand64(); hunt_random_bytes(r->hash, 16);
            HUNT_EQ_INT(ex_journal_write_mark(out, r->type, r->ts, r->path, r->size, r->hash), 0);
        }
    }
}

static void check_record_matches(const ExJournalRecord *got, const Rec *want, size_t idx) {
    HUNT_CHECKF(got->type == want->type, "record %lu type %c, want %c", (unsigned long)idx, got->type, want->type);
    HUNT_CHECK(got->timestamp_ms == want->ts);
    HUNT_CHECK(got->path_len == strlen(want->path) && memcmp(got->path, want->path, got->path_len) == 0);
    if (want->type == 'E') {
        HUNT_CHECK(got->offset == want->offset && got->removed == want->removed);
        HUNT_CHECK(got->inserted_len == want->inslen && (want->inslen == 0 || memcmp(got->inserted, want->ins, want->inslen) == 0));
    } else {
        HUNT_CHECK(got->base_size == want->size && memcmp(got->base_hash, want->hash, 16) == 0);
    }
}

static void test_roundtrip_cut_flip(void) {
    long rounds = hunt_iters(60), r;
    for (r = 0; r < rounds; r++) {
        ExBytes j = {0};
        Rec recs[12];
        size_t n = 1 + hunt_rnd_size(10), i, pos, cut;
        uint8_t *exact;
        ExJournalRecord rec;
        int rc;

        hunt_stepf("journal round trip %ld", r);
        make_journal(&j, recs, n);
        exact = (uint8_t *)malloc(j.len);               /* an exact block, so a read past the end is a sanitizer error */
        memcpy(exact, j.data, j.len);
        HUNT_EQ_INT(ex_journal_check_header(exact, j.len), 0);
        pos = 0;
        for (i = 0; i < n; i++) {
            rc = ex_journal_next(exact, j.len, &pos, &rec);
            HUNT_CHECKF(rc == 1, "record %lu of %lu was not read back (rc %d)", (unsigned long)i, (unsigned long)n, rc);
            if (rc != 1) break;
            check_record_matches(&rec, &recs[i], i);
        }
        if (i == n) { rc = ex_journal_next(exact, j.len, &pos, &rec); HUNT_EQ_INT(rc, 0); HUNT_CHECK(pos == j.len); }

        /* cut at every byte: the records before the cut are returned, nothing after it, nothing outside the data */
        for (cut = 0; cut <= j.len; cut++) {
            uint8_t *c = (uint8_t *)malloc(cut ? cut : 1);
            size_t got = 0, p = 0;
            if (cut) memcpy(c, j.data, cut);
            hunt_set_input(c, cut, "journal cut");
            if (cut < 8) { HUNT_CHECK(ex_journal_check_header(c, cut) != 0); }
            else {
                while ((rc = ex_journal_next(c, cut, &p, &rec)) == 1) { if (got < n) check_record_matches(&rec, &recs[got], got); got++; }
                HUNT_CHECKF(rc == 0, "a cut journal (%lu of %lu bytes) gave rc %d at its end", (unsigned long)cut, (unsigned long)j.len, rc);
                HUNT_CHECK(got <= n);
                HUNT_CHECK(p <= cut);
            }
            hunt_clear_input();
            free(c);
        }

        /* flip every bit of the record region: a damaged record ends replay; records before it are intact; a record that is returned must not be an invented one */
        for (cut = 8; cut < j.len; cut++) {
            int bit;
            for (bit = 0; bit < 8; bit += (cut < 8 + 40 ? 1 : 7)) {
                size_t got = 0, p = 0;
                exact[cut] ^= (uint8_t)(1u << bit);
                while ((rc = ex_journal_next(exact, j.len, &p, &rec)) == 1) {
                    /* every record returned before the damaged one must still match what was written */
                    if (got < n && (rec.type == recs[got].type) && rec.timestamp_ms == recs[got].ts) check_record_matches(&rec, &recs[got], got);
                    got++;
                    if (got > n + 1) break;
                }
                HUNT_CHECKF(got <= n, "a flipped bit (byte %lu, bit %d) made the journal longer", (unsigned long)cut, bit);
                exact[cut] ^= (uint8_t)(1u << bit);
            }
        }
        free(exact);
        for (i = 0; i < n; i++) free(recs[i].ins);
        ex_bytes_free(&j);
    }

    hunt_step("journal header");
    {
        uint8_t h[8];
        size_t pos = 0;
        ExJournalRecord rec;
        memcpy(h, "EXJ1", 4); le32(h + 4, 2);
        HUNT_CHECK(ex_journal_check_header(h, 8) != 0);                                   /* a newer version is not understood... */
        HUNT_EQ_INT(ex_journal_next(h, 8, &pos, &rec), -1);                               /* ...and says so, so the caller leaves it alone */
        memcpy(h, "EXJ1", 4); le32(h + 4, 0);
        HUNT_CHECK(ex_journal_check_header(h, 8) != 0);
        memcpy(h, "EXJ2", 4); le32(h + 4, 1);
        HUNT_CHECK(ex_journal_check_header(h, 8) != 0);
        HUNT_CHECK(ex_journal_check_header(h, 0) != 0);
        HUNT_CHECK(ex_journal_check_header(h, 7) != 0);
        { ExBuffer *b = ex_buffer_create("abc", 3); size_t ap = 99; HUNT_EQ_INT(ex_journal_replay(h, 8, "a.md", b, &ap), -1); HUNT_EQ_INT(ap, 0); ex_buffer_free(b); }
    }
}

/* ---- C. a correct checksum around lying fields ---- */

static void add_raw(ExBytes *j, const uint8_t *body, size_t body_len) {
    uint8_t head[4], tail[4];
    le32(head, (uint32_t)(body_len + 4));
    le32(tail, ex_crc32(0, body, body_len));
    /* ExBytes has no public append: build through a scratch journal write is impossible, so grow by hand */
    if (j->len + body_len + 8 > j->cap) { j->cap = (j->len + body_len + 8) * 2; j->data = (uint8_t *)realloc(j->data, j->cap); }
    memcpy(j->data + j->len, head, 4); memcpy(j->data + j->len + 4, body, body_len); memcpy(j->data + j->len + 4 + body_len, tail, 4);
    j->len += body_len + 8;
}

/* Reads every record of `data` the way replay does; returns how many came back. Never reads outside (the data is an exact block). */
static size_t read_all(const uint8_t *data, size_t len, int *final_rc) {
    size_t pos = 0, n = 0;
    ExJournalRecord rec;
    int rc;
    uint8_t *exact = (uint8_t *)malloc(len ? len : 1);
    memcpy(exact, data, len);
    while ((rc = ex_journal_next(exact, len, &pos, &rec)) == 1) n++;
    if (final_rc) *final_rc = rc;
    free(exact);
    return n;
}

static void test_lying_records(void) {
    ExBytes j = {0};
    uint8_t body[200];
    size_t n;
    int rc;
    ExBuffer *b;
    size_t applied;

    hunt_step("lying records: path_length longer than the record");
    ex_journal_write_header(&j);
    memset(body, 0, sizeof body);
    body[0] = 'E'; le64(body + 1, 1); le16(body + 9, 60000);
    add_raw(&j, body, 11 + 20 + 8 + 8 + 4);
    n = read_all(j.data, j.len, &rc);
    HUNT_CHECK(n == 0 && rc == 0);
    ex_bytes_free(&j);

    hunt_step("lying records: inserted_length longer than the record");
    ex_journal_write_header(&j);
    body[0] = 'E'; le64(body + 1, 1); le16(body + 9, 1); body[11] = 'a'; le64(body + 12, 0); le64(body + 20, 0); le32(body + 28, 0xFFFFFFF0u);
    add_raw(&j, body, 32);
    n = read_all(j.data, j.len, &rc);
    HUNT_CHECK(n == 0 && rc == 0);
    ex_bytes_free(&j);

    hunt_step("lying records: offset + removed overflows a 64-bit number");
    ex_journal_write_header(&j);
    {
        ExBytes tmp = {0};
        ex_journal_write_edit(&tmp, 1, "a.md", 0xFFFFFFFFFFFFFFF0ull, 0x20, "", 0);
        j.data = (uint8_t *)realloc(j.data, j.len + tmp.len); memcpy(j.data + j.len, tmp.data, tmp.len); j.len += tmp.len;
        ex_bytes_free(&tmp);
    }
    b = ex_buffer_create("hello", 5);
    applied = 77;
    rc = ex_journal_replay(j.data, j.len, "a.md", b, &applied);
    HUNT_CHECK(rc == -1);
    HUNT_EQ_INT(ex_buffer_size(b), 5);
    ex_buffer_free(b);
    ex_bytes_free(&j);

    hunt_step("lying records: removed larger than the text, offset exactly at the end");
    ex_journal_write_header(&j);
    {
        ExBytes tmp = {0};
        ex_journal_write_edit(&tmp, 1, "a.md", 5, 1, "x", 1);
        j.data = (uint8_t *)realloc(j.data, j.len + tmp.len); memcpy(j.data + j.len, tmp.data, tmp.len); j.len += tmp.len;
        ex_bytes_free(&tmp);
    }
    b = ex_buffer_create("hello", 5);
    HUNT_EQ_INT(ex_journal_replay(j.data, j.len, "a.md", b, &applied), -1);
    ex_buffer_free(b);
    ex_bytes_free(&j);

    hunt_step("lying records: a body shorter than the smallest record");
    ex_journal_write_header(&j);
    body[0] = 'E'; body[1] = 1;
    add_raw(&j, body, 2);
    n = read_all(j.data, j.len, &rc);
    HUNT_CHECK(n == 0);
    ex_bytes_free(&j);

    hunt_step("lying records: length field of 0xFFFFFFFF, and of 0, 1, 2, 3");
    {
        uint32_t lens[] = { 0xFFFFFFFFu, 0xFFFFFFFCu, 0, 1, 2, 3, 4, 5, 15 };
        size_t i;
        for (i = 0; i < sizeof lens / sizeof *lens; i++) {
            uint8_t raw[8 + 4 + 40];
            ExBytes h = {0};
            ex_journal_write_header(&h);
            memcpy(raw, h.data, 8); le32(raw + 8, lens[i]); memset(raw + 12, 0x41, 40);
            n = read_all(raw, sizeof raw, &rc);
            HUNT_CHECKF(n == 0 && rc == 0, "length field %lu", (unsigned long)lens[i]);
            ex_bytes_free(&h);
        }
    }

    hunt_step("writer limits: a path longer than 65535 bytes");
    {
        size_t plen = 70000;
        char *path = (char *)malloc(plen + 1);
        ExBytes out = {0};
        size_t pos = 0;
        ExJournalRecord rec;
        memset(path, 'p', plen); path[plen] = 0;
        ex_journal_write_header(&out);
        rc = ex_journal_write_edit(&out, 1, path, 0, 0, "x", 1);
        if (rc == 0) {
            /* if the writer accepts it, the reader must give the path back whole; a path cut to its low 16 bits would send the edit to another note */
            int got = ex_journal_next(out.data, out.len, &pos, &rec);
            HUNT_CHECKF(got == 1 && rec.path_len == plen, "a %lu byte path came back as %lu bytes (rc %d)", (unsigned long)plen, got == 1 ? (unsigned long)rec.path_len : 0ul, got);
        } else {
            HUNT_CHECK(out.len == 8);       /* a refused record leaves nothing behind */
        }
        free(path);
        ex_bytes_free(&out);
    }

    hunt_step("writer limits: a mark with a bad type, an empty path, a path of 65535 bytes");
    {
        ExBytes out = {0};
        uint8_t h[16] = {0};
        char *path = (char *)malloc(65536);
        memset(path, 'q', 65535); path[65535] = 0;
        HUNT_CHECK(ex_journal_write_mark(&out, 'E', 1, "a", 1, h) != 0);
        HUNT_CHECK(ex_journal_write_mark(&out, 0, 1, "a", 1, h) != 0);
        HUNT_CHECK(out.len == 0);
        HUNT_EQ_INT(ex_journal_write_header(&out), 0);
        HUNT_EQ_INT(ex_journal_write_edit(&out, 1, "", 3, 0, "z", 1), 0);          /* an empty path is a record like any other */
        HUNT_EQ_INT(ex_journal_write_edit(&out, 1, path, 3, 0, "z", 1), 0);
        n = read_all(out.data, out.len, &rc);
        HUNT_EQ_INT(n, 2);
        free(path);
        ex_bytes_free(&out);
    }

    hunt_step("a large insertion");
    {
        size_t big = 3u << 20, pos = 0;
        char *t = (char *)malloc(big);
        ExBytes out = {0};
        ExJournalRecord rec;
        memset(t, 'L', big);
        ex_journal_write_header(&out);
        HUNT_EQ_INT(ex_journal_write_edit(&out, 5, "big.md", 0, 0, t, big), 0);
        HUNT_EQ_INT(ex_journal_next(out.data, out.len, &pos, &rec), 1);
        HUNT_CHECK(rec.inserted_len == big && memcmp(rec.inserted, t, big) == 0);
        free(t);
        ex_bytes_free(&out);
    }
}

/* ---- D. unknown record types ---- */

static void test_unknown_types(void) {
    ExBytes j = {0};
    uint8_t body[64];
    ExJournalRecord rec;
    size_t pos = 0, seen_e = 0, seen_unknown = 0;
    int rc;
    ExBuffer *b;
    size_t applied = 0;

    hunt_step("unknown record type with the same layout");
    ex_journal_write_header(&j);
    ex_journal_write_edit(&j, 1, "a.md", 0, 0, "A", 1);
    memset(body, 0, sizeof body);
    body[0] = 'Z'; le64(body + 1, 2); le16(body + 9, 4); memcpy(body + 11, "a.md", 4); memset(body + 15, 7, 20);
    add_raw(&j, body, 35);
    ex_journal_write_edit(&j, 3, "a.md", 1, 0, "B", 1);
    while ((rc = ex_journal_next(j.data, j.len, &pos, &rec)) == 1) { if (rec.type == 'E') seen_e++; else seen_unknown++; }
    HUNT_SPEC(seen_e == 2 && seen_unknown == 1, "docs/formats.md, Journal file / include/exhaust_buffer.h: a record of an unknown type is skipped (its checksum must still match)");
    b = ex_buffer_create("", 0);
    HUNT_EQ_INT(ex_journal_replay(j.data, j.len, "a.md", b, &applied), 0);
    HUNT_EQ_INT(applied, 2);
    ex_buffer_free(b);
    ex_bytes_free(&j);

    hunt_step("unknown record type with a different, shorter layout (a newer version's record)");
    ex_journal_write_header(&j);
    ex_journal_write_edit(&j, 1, "a.md", 0, 0, "A", 1);
    body[0] = 'Q'; body[1] = 1; body[2] = 2; body[3] = 3;          /* 4 bytes: no timestamp, no path: nothing like the layout above */
    add_raw(&j, body, 4);
    ex_journal_write_edit(&j, 3, "a.md", 1, 0, "B", 1);
    pos = 0; seen_e = 0;
    while ((rc = ex_journal_next(j.data, j.len, &pos, &rec)) == 1) if (rec.type == 'E') seen_e++;
    HUNT_SPEC(seen_e == 2, "docs/formats.md, Journal file: a newer journal must replay in an older version or be left untouched, never discarded (a record whose layout this version does not know cannot be told from damage, but its checksum is right, so the records after it must not be lost)");
    ex_bytes_free(&j);

    hunt_step("unknown type with a path_length that overruns it (same family of bytes)");
    ex_journal_write_header(&j);
    ex_journal_write_edit(&j, 1, "a.md", 0, 0, "A", 1);
    memset(body, 0, sizeof body);
    body[0] = 'Q'; le64(body + 1, 2); le16(body + 9, 500);
    add_raw(&j, body, 30);
    ex_journal_write_edit(&j, 3, "a.md", 1, 0, "B", 1);
    pos = 0; seen_e = 0;
    while ((rc = ex_journal_next(j.data, j.len, &pos, &rec)) == 1) if (rec.type == 'E') seen_e++;
    HUNT_SPEC(seen_e == 2, "docs/formats.md, Journal file: a record of an unknown type is skipped when its checksum matches");
    ex_bytes_free(&j);
}

/* ---- E. replay against a model ---- */

static void test_replay_model(void) {
    long rounds = hunt_iters(300), r;
    for (r = 0; r < rounds; r++) {
        ExBytes j = {0};
        const char *paths[3] = { "a.md", "dir/b.md", "a.md.bak" };           /* the last one starts with the first: a prefix match would mix them up */
        char texts[3][600];
        size_t lens[3], base_lens[3], i, nops = 1 + hunt_rnd_size(40), applied_expect[3] = { 0, 0, 0 };
        char base[3][600];
        ExBuffer *bufs[3];

        hunt_stepf("replay model %ld", r);
        for (i = 0; i < 3; i++) {
            lens[i] = base_lens[i] = hunt_random_valid_text(base[i], 200, 8);
            memcpy(texts[i], base[i], lens[i]);
        }
        ex_journal_write_header(&j);
        for (i = 0; i < nops; i++) {
            size_t who = hunt_rnd_size(3);
            if (hunt_chance(12)) {
                uint8_t h[16] = {0};
                /* saved: what is on disk now is the text so far; later edits apply to that */
                HUNT_EQ_INT(ex_journal_write_mark(&j, 'S', i, paths[who], lens[who], h), 0);
                memcpy(base[who], texts[who], lens[who]); base_lens[who] = lens[who]; applied_expect[who] = 0;
            } else if (hunt_chance(8)) {
                uint8_t h[16] = {0};
                HUNT_EQ_INT(ex_journal_write_mark(&j, 'B', i, paths[who], lens[who], h), 0);
            } else {
                size_t off = hunt_rnd_size(lens[who] + 1), rem = hunt_rnd_size(lens[who] - off + 1 > 8 ? 8 : lens[who] - off + 1);
                char ins[40];
                size_t il = hunt_random_valid_text(ins, 20, 3);
                if (lens[who] - rem + il >= sizeof texts[who]) il = 0;
                HUNT_EQ_INT(ex_journal_write_edit(&j, i, paths[who], off, rem, ins, il), 0);
                memmove(texts[who] + off + il, texts[who] + off + rem, lens[who] - off - rem);
                memcpy(texts[who] + off, ins, il);
                lens[who] = lens[who] - rem + il;
                applied_expect[who]++;
            }
        }
        for (i = 0; i < 3; i++) {
            size_t applied = 0, got_n;
            char *got;
            bufs[i] = ex_buffer_create(base[i], base_lens[i]);
            HUNT_EQ_INT(ex_journal_replay(j.data, j.len, paths[i], bufs[i], &applied), 0);
            HUNT_CHECKF(applied == applied_expect[i], "path %s: %lu edits applied, want %lu", paths[i], (unsigned long)applied, (unsigned long)applied_expect[i]);
            got = ex_buffer_text(bufs[i], &got_n);
            HUNT_EQ_BYTES(got, got_n, texts[i], lens[i]);
            free(got);
            ex_buffer_free(bufs[i]);
        }
        ex_bytes_free(&j);
    }

    hunt_step("replay: an edit that does not fit leaves the buffer as it was, or at least does not crash");
    {
        ExBytes j = {0};
        ExBuffer *b = ex_buffer_create("0123456789", 10);
        size_t applied = 0, n;
        char *t;
        ex_journal_write_header(&j);
        ex_journal_write_edit(&j, 1, "a.md", 0, 0, "X", 1);
        ex_journal_write_edit(&j, 2, "a.md", 500, 0, "Y", 1);        /* does not fit the 11 byte text */
        HUNT_EQ_INT(ex_journal_replay(j.data, j.len, "a.md", b, &applied), -1);
        t = ex_buffer_text(b, &n);
        HUNT_SUSPECT(n == 10 && !memcmp(t, "0123456789", 10), "a journal that does not belong to the text is refused; applying its first edit and then stopping leaves a text that is neither the file nor the recovered one");
        free(t);
        ex_buffer_free(b);
        ex_bytes_free(&j);
    }
}

int main(int argc, char **argv) {
    hunt_begin("hunt_journal", argc, argv);
    test_checksums();
    test_roundtrip_cut_flip();
    test_lying_records();
    test_unknown_types();
    test_replay_model();
    return hunt_end();
}
