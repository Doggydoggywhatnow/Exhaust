/* hunt.h: the small harness shared by every test in tests/hunt/.
 *
 * These tests are written to FIND bugs, not to stay green. They differ from the tests in tests/ in four ways:
 *   1. A failed check does not stop the test: every check runs, every failure is printed with its file, line and the
 *      step that was running, and the program exits 1 at the end if anything failed.
 *   2. Random work is seeded and the seed is printed first (HUNT_SEED=<n> repeats a run, HUNT_SEED=random picks a new
 *      one), so a failure found by chance can be found again.
 *   3. A crash (SIGSEGV, SIGABRT, SIGFPE...) or a hang (the alarm, HUNT_TIMEOUT seconds, default 300) prints the seed,
 *      the step the test had reached and, when the test registered one, the input that was being tried, as hex.
 *   4. Three kinds of check, so a person reading the output knows how much to trust a failure:
 *        HUNT_CHECK(c)          an invariant that must always hold (a crash, a lost byte, a broken round trip).
 *        HUNT_SPEC(c, "ref")    a rule the documentation states (ref names the file and section). A failure is either
 *                               a bug in the code or a bug in the documentation: both are worth an ISSUES entry.
 *        HUNT_SUSPECT(c, "why") a heuristic or an expectation the test author is not sure of. Printed, counted, and
 *                               it makes the test fail only when HUNT_STRICT=1. Read these by hand.
 *
 * Environment: HUNT_SEED, HUNT_SCALE (multiplies the amount of random work, default 1; HUNT_QUICK=1 divides it by 10),
 * HUNT_TIMEOUT (seconds), HUNT_STRICT, HUNT_VERBOSE (print every step), HUNT_MAXPRINT (failures printed, default 40).
 * Nothing here touches the network, and no test writes outside its own folder (see hunt_fs.h).
 *
 * Header-only on purpose: each test is one source file plus this header. */
#ifndef HUNT_H
#define HUNT_H

#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

#define HUNT_UNUSED_FN static inline

typedef struct HuntState {
    const char *name;
    uint64_t seed, rng;
    unsigned long checks, failures, suspects, printed;
    int strict, verbose, scale_num, scale_den, maxprint;
    const char *step;              /* what the test is doing, for a crash report */
    char stepbuf[200];
    const unsigned char *inp;      /* the input being tried, for a crash report */
    size_t inplen;
    char inpnote[120];
} HuntState;

static HuntState hunt_;

/* ---- reporting ---- */

HUNT_UNUSED_FN void hunt_hex_(FILE *f, const unsigned char *p, size_t n) {
    size_t i, show = n > 400 ? 400 : n;
    for (i = 0; i < show; i++) fprintf(f, "%02x", p[i]);
    if (show < n) fprintf(f, "...(+%lu bytes)", (unsigned long)(n - show));
}

HUNT_UNUSED_FN void hunt_report_state_(const char *what) {
    fprintf(stderr, "\nHUNT %s in %s: seed=%llu step=%s\n", what, hunt_.name ? hunt_.name : "?", (unsigned long long)hunt_.seed,
            hunt_.step ? hunt_.step : "(none)");
    if (hunt_.inp) {
        fprintf(stderr, "HUNT input (%s, %lu bytes): ", hunt_.inpnote, (unsigned long)hunt_.inplen);
        hunt_hex_(stderr, hunt_.inp, hunt_.inplen);
        fprintf(stderr, "\n");
    }
    fflush(stderr);
}

static void hunt_on_signal_(int sig) {
    const char *what = "CRASH";
#ifdef SIGALRM
    if (sig == SIGALRM) what = "HANG (timeout)";
#endif
    fprintf(stderr, "\nHUNT %s signal=%d\n", what, sig);
    hunt_report_state_(what);
    signal(sig, SIG_DFL);
    raise(sig);
}

/* The step is a pointer to a string that must outlive the next call (a literal), or a copy made by hunt_stepf. */
HUNT_UNUSED_FN void hunt_step(const char *s) {
    hunt_.step = s;
    if (hunt_.verbose) fprintf(stderr, "step: %s\n", s);
}
HUNT_UNUSED_FN void hunt_stepf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(hunt_.stepbuf, sizeof hunt_.stepbuf, fmt, ap);
    va_end(ap);
    hunt_.step = hunt_.stepbuf;
    if (hunt_.verbose) fprintf(stderr, "step: %s\n", hunt_.stepbuf);
}
/* Registers the input about to be tried (the bytes must stay valid until the next call or hunt_clear_input). */
HUNT_UNUSED_FN void hunt_set_input(const void *p, size_t n, const char *note) {
    hunt_.inp = (const unsigned char *)p;
    hunt_.inplen = n;
    snprintf(hunt_.inpnote, sizeof hunt_.inpnote, "%s", note ? note : "");
}
HUNT_UNUSED_FN void hunt_clear_input(void) { hunt_.inp = NULL; hunt_.inplen = 0; }

HUNT_UNUSED_FN void hunt_fail_(const char *kind, const char *file, int line, const char *what, const char *fmt, ...) {
    hunt_.failures += !strcmp(kind, "SUSPECT") ? 0 : 1;
    if (!strcmp(kind, "SUSPECT")) hunt_.suspects++;
    if ((int)hunt_.printed >= hunt_.maxprint) {
        if ((int)hunt_.printed == hunt_.maxprint) { fprintf(stderr, "HUNT more failures follow and are only counted (HUNT_MAXPRINT=%d)\n", hunt_.maxprint); hunt_.printed++; }
        return;
    }
    hunt_.printed++;
    fprintf(stderr, "HUNT %s %s:%d: %s [step=%s]\n", kind, file, line, what, hunt_.step ? hunt_.step : "-");
    if (fmt && *fmt) {
        va_list ap;
        fprintf(stderr, "    ");
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
        fprintf(stderr, "\n");
    }
    if (hunt_.inp) {
        fprintf(stderr, "    input (%s, %lu bytes): ", hunt_.inpnote, (unsigned long)hunt_.inplen);
        hunt_hex_(stderr, hunt_.inp, hunt_.inplen);
        fprintf(stderr, "\n");
    }
}

#define HUNT_CHECK(c) do { hunt_.checks++; if (!(c)) hunt_fail_("FAIL", __FILE__, __LINE__, #c, ""); } while (0)
#define HUNT_CHECKF(c, ...) do { hunt_.checks++; if (!(c)) hunt_fail_("FAIL", __FILE__, __LINE__, #c, __VA_ARGS__); } while (0)
#define HUNT_SPEC(c, ref) do { hunt_.checks++; if (!(c)) hunt_fail_("SPEC", __FILE__, __LINE__, #c, "documented in %s", ref); } while (0)
#define HUNT_SPECF(c, ref, ...) do { hunt_.checks++; if (!(c)) { hunt_fail_("SPEC", __FILE__, __LINE__, #c, "documented in %s", ref); hunt_note_(__VA_ARGS__); } } while (0)
#define HUNT_SUSPECT(c, why) do { hunt_.checks++; if (!(c)) hunt_fail_("SUSPECT", __FILE__, __LINE__, #c, "%s", why); } while (0)

/* Extra line under the failure just printed. */
HUNT_UNUSED_FN void hunt_note_(const char *fmt, ...) {
    va_list ap;
    if ((int)hunt_.printed > hunt_.maxprint) return;
    fprintf(stderr, "    ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

/* Comparing strings and numbers prints both sides. NULL is a value of its own. */
HUNT_UNUSED_FN int hunt_str_eq_(const char *a, const char *b) { return a == b || (a && b && !strcmp(a, b)); }
#define HUNT_EQ_STR(got, want) do { const char *g_ = (got), *w_ = (want); hunt_.checks++; \
    if (!hunt_str_eq_(g_, w_)) hunt_fail_("FAIL", __FILE__, __LINE__, #got " == " #want, "got  \"%s\"\n    want \"%s\"", g_ ? g_ : "(null)", w_ ? w_ : "(null)"); } while (0)
#define HUNT_EQ_INT(got, want) do { long long g_ = (long long)(got), w_ = (long long)(want); hunt_.checks++; \
    if (g_ != w_) hunt_fail_("FAIL", __FILE__, __LINE__, #got " == " #want, "got %lld, want %lld", g_, w_); } while (0)
#define HUNT_EQ_BYTES(got, glen, want, wlen) do { const void *g_ = (got), *w_ = (want); size_t gl_ = (glen), wl_ = (wlen); hunt_.checks++; \
    if (gl_ != wl_ || (gl_ && memcmp(g_, w_, gl_) != 0)) { size_t k_ = 0; while (k_ < gl_ && k_ < wl_ && ((const unsigned char *)g_)[k_] == ((const unsigned char *)w_)[k_]) k_++; \
    hunt_fail_("FAIL", __FILE__, __LINE__, #got " == " #want, "lengths %lu and %lu, first difference at byte %lu", (unsigned long)gl_, (unsigned long)wl_, (unsigned long)k_); } } while (0)

/* ---- random numbers: xorshift64*, seeded and repeatable ---- */

HUNT_UNUSED_FN uint64_t hunt_rand64(void) {
    uint64_t x = hunt_.rng;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    hunt_.rng = x;
    return x * 0x2545F4914F6CDD1Dull;
}
HUNT_UNUSED_FN uint32_t hunt_rnd(uint32_t n) { return n ? (uint32_t)((hunt_rand64() >> 11) % n) : 0; }
HUNT_UNUSED_FN int hunt_chance(int percent) { return (int)hunt_rnd(100) < percent; }
HUNT_UNUSED_FN size_t hunt_rnd_size(size_t n) { return n ? (size_t)((hunt_rand64() >> 7) % n) : 0; }

/* A time limit scaled for slow machines and sanitizer builds: HUNT_TIME_SCALE (default 1) multiplies it. */
HUNT_UNUSED_FN double hunt_time_limit(double seconds) {
    const char *e = getenv("HUNT_TIME_SCALE");
    double k = e && *e ? atof(e) : 1.0;
    return seconds * (k > 0 ? k : 1.0);
}
/* CPU seconds since `t0` (from clock()): a stalled thread on a busy machine does not count. */
HUNT_UNUSED_FN double hunt_cpu_since(clock_t t0) { return (double)(clock() - t0) / (double)CLOCKS_PER_SEC; }

/* The amount of random work: base times HUNT_SCALE (at least 1). */
HUNT_UNUSED_FN long hunt_iters(long base) {
    long n = base * hunt_.scale_num / hunt_.scale_den;
    return n < 1 ? 1 : n;
}

/* ---- random text ---- */

/* Fills out with n bytes of anything. */
HUNT_UNUSED_FN void hunt_random_bytes(unsigned char *out, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) out[i] = (unsigned char)hunt_rnd(256);
}

/* Pieces that make Markdown, Unicode and line-ending trouble. Used by hunt_random_text. */
static const char *const hunt_pieces_[] = {
    "a", "b", "word", " ", "  ", "\t", "\n", "\n\n", "\r\n", "\r", "\xC3\xA9", "e\xCC\x81", "\xE4\xBD\xA0", "\xF0\x9F\x98\x80", "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9",
    "\xEF\xBB\xBF", "\xE2\x80\xAE", "\xC2\xA0", "\xCC\x81", "\xED\x95\x9C", "\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB", "\xF0\x9F\x87\xA9\xF0\x9F\x87\xAA",
    "#", "# ", "## ", "###### ", "- ", "* ", "+ ", "1. ", "10) ", "> ", ">> ", "- [ ] ", "- [x] ", "- [-] ", "---", "***", "___", "===", "...", "```", "~~~", "``", "`", "$$", "$", "%%", "==", "~~", "**", "*", "__", "_",
    "[[", "]]", "![[", "[", "]", "(", ")", "![", "](", "|", "||", "|---|---|", "| a | b |\n", "\\", "\\|", "^", "^abc", "^id-1", "#tag", "#a/b", "#1", "#2024", "<", ">", "<div>", "</div>", "<br>", "<!--", "-->",
    "&amp;", "&#65;", "&#x1F600;", "&", "[^1]", "[^note]:", "^[inline]", "[!note]", "[!warning]- ", "[!tip]+ ", "---\n", "...\n", "title: x\n", "tags: [a, b]\n", "key: \"[[Link]]\"\n",
    "[[Note]]", "[[Note|alias]]", "[[Note#Heading]]", "[[Note#^abc]]", "![[img.png]]", "[text](Note.md)", "[t](<a b.md>)", "[t](a%20b.md)", "<https://example.com>", "https://example.com/a_b_c",
    ":", ": ", "'", "\"", "{", "}", ",", ";", "=", "?", "!", "@", "0", "1", "9999999999999999999", "-1", "1e5", "NaN", "\x01", "\x7f", "\xC0\x80", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xFF", "\xC3"
};
#define HUNT_NPIECES_ ((unsigned)(sizeof hunt_pieces_ / sizeof hunt_pieces_[0]))

/* Random text from the pieces above, at most cap - 1 bytes, NUL terminated; returns its length. */
HUNT_UNUSED_FN size_t hunt_random_text(char *out, size_t cap, int max_pieces) {
    size_t n = 0;
    int parts = 1 + (int)hunt_rnd((uint32_t)(max_pieces > 0 ? max_pieces : 1));
    if (cap == 0) return 0;
    while (parts--) {
        const char *p = hunt_pieces_[hunt_rnd(HUNT_NPIECES_)];
        size_t l = strlen(p);
        if (n + l + 1 > cap) break;
        memcpy(out + n, p, l);
        n += l;
    }
    out[n] = 0;
    return n;
}

/* Valid UTF-8 only (no NUL): for tests whose input must be text. */
HUNT_UNUSED_FN size_t hunt_random_valid_text(char *out, size_t cap, int max_pieces) {
    size_t n = 0;
    int parts = 1 + (int)hunt_rnd((uint32_t)(max_pieces > 0 ? max_pieces : 1));
    if (cap == 0) return 0;
    while (parts--) {
        const char *p = hunt_pieces_[hunt_rnd(HUNT_NPIECES_)];
        size_t l = strlen(p);
        int bad = 0;
        size_t i;
        for (i = 0; i < l; i++) { unsigned char c = (unsigned char)p[i]; if (c == 0xC0 || c == 0xFF || c == 0xED || c == 0xF4 || c == 0x01 || c == 0x7f) bad = 1; }
        if (!strcmp(p, "\xC3")) bad = 1;
        if (bad) continue;
        if (n + l + 1 > cap) break;
        memcpy(out + n, p, l);
        n += l;
    }
    out[n] = 0;
    return n;
}

/* Changes a buffer in place a few ways (flip, insert a piece, delete a range, duplicate a range, truncate). Returns the new length (at most cap). */
HUNT_UNUSED_FN size_t hunt_mutate(unsigned char *buf, size_t len, size_t cap) {
    int rounds = 1 + (int)hunt_rnd(4);
    while (rounds--) {
        switch (hunt_rnd(6)) {
        case 0: if (len) buf[hunt_rnd_size(len)] ^= (unsigned char)(1u << hunt_rnd(8)); break;
        case 1: if (len) buf[hunt_rnd_size(len)] = (unsigned char)hunt_rnd(256); break;
        case 2: { const char *p = hunt_pieces_[hunt_rnd(HUNT_NPIECES_)]; size_t l = strlen(p), at = hunt_rnd_size(len + 1);
                  if (len + l <= cap) { memmove(buf + at + l, buf + at, len - at); memcpy(buf + at, p, l); len += l; } break; }
        case 3: if (len) { size_t a = hunt_rnd_size(len), l = 1 + hunt_rnd_size(len - a); memmove(buf + a, buf + a + l, len - a - l); len -= l; } break;
        case 4: if (len) { size_t a = hunt_rnd_size(len), l = 1 + hunt_rnd_size(len - a > 64 ? 64 : len - a), at = hunt_rnd_size(len + 1);
                  if (len + l <= cap) { unsigned char tmp[64]; memcpy(tmp, buf + a, l); memmove(buf + at + l, buf + at, len - at); memcpy(buf + at, tmp, l); len += l; } } break;
        default: if (len) len = hunt_rnd_size(len + 1); break;
        }
    }
    return len;
}

/* ---- life cycle ---- */

HUNT_UNUSED_FN uint64_t hunt_parse_seed_(const char *s) {
    if (!strcmp(s, "random")) return ((uint64_t)time(NULL) * 0x9E3779B97F4A7C15ull) ^ (uint64_t)clock();
    return (uint64_t)strtoull(s, NULL, 0);
}

/* Call first. argv may carry `--seed N`. */
HUNT_UNUSED_FN void hunt_begin(const char *name, int argc, char **argv) {
    const char *e;
    int i;
    memset(&hunt_, 0, sizeof hunt_);
    hunt_.name = name;
    hunt_.seed = 0x5EED0000C0FFEEull;
    hunt_.maxprint = 40;
    hunt_.scale_num = 1; hunt_.scale_den = 1;
    e = getenv("HUNT_SEED");
    if (e && *e) hunt_.seed = hunt_parse_seed_(e);
    for (i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "--seed")) hunt_.seed = hunt_parse_seed_(argv[i + 1]);
    if (hunt_.seed == 0) hunt_.seed = 1;
    hunt_.rng = hunt_.seed * 0x9E3779B97F4A7C15ull + 1;
    for (i = 0; i < 8; i++) hunt_rand64();
    e = getenv("HUNT_SCALE");
    if (e && *e) { int s = atoi(e); if (s > 0) hunt_.scale_num = s; }
    e = getenv("HUNT_QUICK");
    if (e && *e && *e != '0') hunt_.scale_den = 10;
    e = getenv("HUNT_STRICT");
    hunt_.strict = e && *e && *e != '0';
    e = getenv("HUNT_VERBOSE");
    hunt_.verbose = e && *e && *e != '0';
    e = getenv("HUNT_MAXPRINT");
    if (e && *e && atoi(e) > 0) hunt_.maxprint = atoi(e);
    signal(SIGSEGV, hunt_on_signal_);
    signal(SIGABRT, hunt_on_signal_);
    signal(SIGFPE, hunt_on_signal_);
    signal(SIGILL, hunt_on_signal_);
#ifdef SIGBUS
    signal(SIGBUS, hunt_on_signal_);
#endif
#ifdef SIGALRM
    {
        int secs = 300;
        e = getenv("HUNT_TIMEOUT");
        if (e && *e && atoi(e) > 0) secs = atoi(e);
        signal(SIGALRM, hunt_on_signal_);
        alarm((unsigned)secs);
    }
#endif
    fprintf(stdout, "HUNT %s seed=%llu scale=%d/%d\n", name, (unsigned long long)hunt_.seed, hunt_.scale_num, hunt_.scale_den);
    fflush(stdout);
}

/* Call last: return its value from main. */
HUNT_UNUSED_FN int hunt_end(void) {
    int bad = hunt_.failures > 0 || (hunt_.strict && hunt_.suspects > 0);
    fprintf(stdout, "HUNT-RESULT %s %s checks=%lu failures=%lu suspects=%lu seed=%llu\n", hunt_.name, bad ? "FAILED" : "ok", hunt_.checks, hunt_.failures,
            hunt_.suspects, (unsigned long long)hunt_.seed);
    fflush(stdout);
    return bad ? 1 : 0;
}

/* Portable stand-ins for memmem and strcasecmp (strict C11 declares neither). */
HUNT_UNUSED_FN void *hunt_memmem(const void *hay, size_t hl, const void *needle, size_t nl) {
    const unsigned char *h = (const unsigned char *)hay;
    size_t i;
    if (nl == 0) return (void *)hay;
    if (nl > hl) return NULL;
    for (i = 0; i + nl <= hl; i++) if (h[i] == ((const unsigned char *)needle)[0] && memcmp(h + i, needle, nl) == 0) return (void *)(h + i);
    return NULL;
}
HUNT_UNUSED_FN int hunt_lc_(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
HUNT_UNUSED_FN int hunt_strncasecmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) { int x = hunt_lc_((unsigned char)*a), y = hunt_lc_((unsigned char)*b); if (x != y) return x < y ? -1 : 1; if (!x) return 0; }
    return 0;
}
HUNT_UNUSED_FN int hunt_strcasecmp(const char *a, const char *b) { return hunt_strncasecmp(a, b, (size_t)-1); }

/* A malloc'ed copy of n bytes with a NUL after them. */
HUNT_UNUSED_FN char *hunt_dup(const void *p, size_t n) {
    char *s = (char *)malloc(n + 1);
    if (!s) { fprintf(stderr, "HUNT out of memory in the test itself\n"); exit(2); }
    if (n) memcpy(s, p, n);
    s[n] = 0;
    return s;
}
HUNT_UNUSED_FN char *hunt_strdup(const char *s) { return hunt_dup(s, strlen(s)); }

#endif
