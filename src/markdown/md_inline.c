/* The inline tokenizer: the constructs inside a paragraph, a heading, a table cell and so on. The
 * lines of the run are joined with line breaks into one buffer; spans are found in it and mapped back
 * to byte offsets of the note. */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_html.h"
#include "md_internal.h"

/* Forward searches look at most this far, so a huge paragraph of unmatched brackets cannot take quadratic time. */
#define MD_SCAN_LIMIT 20000
static size_t lim(size_t i, size_t hi) { return hi - i > MD_SCAN_LIMIT ? i + MD_SCAN_LIMIT : hi; }

typedef struct Seg { size_t lstart, len, abs; } Seg;

typedef struct Inl {
    Ctx *c;
    char *buf;
    size_t blen;
    Seg *segs;
    size_t nsegs;
} Inl;

/* ---- mapping logical positions back to the note ---- */

static size_t seg_for(const Inl *in, size_t pos, int for_end) {
    size_t lo = 0, hi = in->nsegs;        /* the last segment whose lstart is <= pos (for an end: < pos) */
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if (for_end ? in->segs[mid].lstart < pos : in->segs[mid].lstart <= pos) lo = mid; else hi = mid;
    }
    return lo;
}

static size_t abs_start(const Inl *in, size_t pos) {
    const Seg *s = &in->segs[seg_for(in, pos, 0)];
    size_t off = pos - s->lstart;
    return s->abs + (off > s->len ? s->len : off);
}

static size_t abs_end(const Inl *in, size_t pos) {
    const Seg *s;
    size_t off;
    if (pos == 0) return in->segs[0].abs;
    s = &in->segs[seg_for(in, pos, 1)];
    off = pos - s->lstart;
    return s->abs + (off > s->len ? s->len : off);
}

static void emit(Inl *in, ExMdKind kind, size_t a, size_t b, int flags, int level) { md_emit(in->c, kind, abs_start(in, a), abs_end(in, b), flags, level); }
static void marker(Inl *in, size_t a, size_t b) { if (b > a) emit(in, EX_MD_MARKER, a, b, 0, 0); }

/* ---- character classes ---- */

static int is_ws(int ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v'; }
static int is_punct(int ch) { return ch > 0 && ch < 128 && ispunct(ch); }
static int is_word_byte(unsigned char ch) { return isalnum(ch) || ch >= 0x80; }

static void scan_range(Inl *in, size_t lo, size_t hi, int depth);

/* ---- emphasis (the delimiter run algorithm of CommonMark, plus ~~ and ==) ---- */

typedef struct Delim { size_t pos, len, rem; char ch; int open, close; } Delim;

static void resolve(Inl *in, Delim *d, size_t nd) {
    size_t c;
    for (c = 0; c < nd; c++) {
        if (!d[c].close || d[c].rem == 0) continue;
        while (d[c].rem > 0) {
            size_t o, found = (size_t)-1;
            for (o = c; o-- > 0 && c - o < 5000;) {
                if (d[o].ch != d[c].ch || !d[o].open || d[o].rem == 0) continue;
                if ((d[c].ch == '*' || d[c].ch == '_') && (d[o].close || d[c].open) && (d[o].len + d[c].len) % 3 == 0 && !(d[o].len % 3 == 0 && d[c].len % 3 == 0)) continue;
                found = o;
                break;
            }
            if (found == (size_t)-1) break;
            {
                size_t use, x, os, cs;
                ExMdKind kind;
                o = found;
                if (d[c].ch == '~' || d[c].ch == '=') { use = 2; kind = d[c].ch == '~' ? EX_MD_STRIKE : EX_MD_HIGHLIGHT; }
                else if (d[o].rem >= 2 && d[c].rem >= 2) { use = 2; kind = EX_MD_STRONG; }
                else { use = 1; kind = EX_MD_EMPHASIS; }
                if (d[o].rem < use || d[c].rem < use) break;
                os = d[o].pos + d[o].rem - use;                       /* the delimiter characters next to the text are used first */
                cs = d[c].pos + (d[c].len - d[c].rem);
                emit(in, kind, os, cs + use, 0, 0);
                marker(in, os, os + use);
                marker(in, cs, cs + use);
                d[o].rem -= use;
                d[c].rem -= use;
                for (x = o + 1; x < c; x++) d[x].rem = 0;           /* delimiters between the two can no longer match */
            }
        }
    }
}

/* ---- helpers for the constructs ---- */

/* The index of the matching ']' for the '[' at i (brackets nest; escapes and code spans are skipped), or (size_t)-1. */
static size_t match_bracket(const Inl *in, size_t i, size_t hi) {
    int depth = 0;
    size_t k, end = lim(i, hi);
    for (k = i; k < end; k++) {
        char ch = in->buf[k];
        if (ch == '\\') { k++; continue; }
        if (ch == '`') {
            size_t run = 1, j;
            while (k + run < end && in->buf[k + run] == '`') run++;
            for (j = k + run; j < end; j++) {
                size_t r2 = 0;
                if (in->buf[j] != '`') continue;
                while (j + r2 < end && in->buf[j + r2] == '`') r2++;
                if (r2 == run) { k = j + r2 - 1; break; }
                j += r2 - 1;
            }
            if (j >= end) k += run - 1;
            continue;
        }
        if (ch == '[') depth++;
        else if (ch == ']') { if (--depth == 0) return k; }
    }
    return (size_t)-1;
}

/* A wikilink or embed whose "[[" starts at i (embed: the '!' is at i-1... the caller passes the start of the span). */
static int try_wikilink(Inl *in, size_t start, size_t open, size_t hi, int is_embed) {
    size_t k, close = (size_t)-1, bar = (size_t)-1, hash = (size_t)-1, ts, te, bar_len = 1, end = lim(open, hi);
    for (k = open + 2; k + 1 < end; k++) {
        char ch = in->buf[k];
        if (ch == '\n') return 0;                                            /* a wikilink does not cross a line break */
        if (ch == '\\') {                                                    /* an escaped bar (in a table) separates the alias too */
            if (in->buf[k + 1] == '|' && bar == (size_t)-1) { bar = k; bar_len = 2; }
            k++;
            continue;
        }
        if (ch == '[' && in->buf[k + 1] == '[') return 0;
        if (ch == ']' && in->buf[k + 1] == ']') { close = k; break; }
        if (ch == '|' && bar == (size_t)-1) bar = k;
        if (ch == '#' && hash == (size_t)-1 && bar == (size_t)-1) hash = k;
    }
    if (close == (size_t)-1 || close == open + 2) return 0;
    emit(in, is_embed ? EX_MD_EMBED : EX_MD_WIKILINK, start, close + 2, 0, 0);
    if (is_embed) marker(in, start, start + 1);
    marker(in, open, open + 2);
    marker(in, close, close + 2);
    ts = open + 2;
    te = bar != (size_t)-1 ? bar : close;
    if (hash != (size_t)-1) {                                                /* page, then #heading or #^block */
        size_t hs = hash + 1;
        if (hash > ts) emit(in, EX_MD_LINK_TARGET, ts, hash, 0, 0);
        marker(in, hash, hash + 1);
        if (hs < te && in->buf[hs] == '^') { marker(in, hs, hs + 1); if (te > hs + 1) emit(in, EX_MD_LINK_BLOCK, hs + 1, te, 0, 0); }
        else if (te > hs) emit(in, EX_MD_LINK_HEADING, hs, te, 0, 0);
    } else if (te > ts) emit(in, EX_MD_LINK_TARGET, ts, te, 0, 0);
    if (bar != (size_t)-1) { marker(in, bar, bar + bar_len); if (close > bar + bar_len) emit(in, EX_MD_LINK_ALIAS, bar + bar_len, close, 0, 0); }
    return (int)(close + 2 - start);
}

/* "[text](dest "title")" or the image form; start is the '[' (or the '!'), open the '['. Returns the bytes used or 0. */
static size_t try_link(Inl *in, size_t start, size_t open, size_t hi, int is_image, int depth) {
    size_t close = match_bracket(in, open, hi), k, ds, de, ts = 0, te = 0, end;
    if (close == (size_t)-1 || close + 1 >= hi || in->buf[close + 1] != '(') return 0;
    k = close + 2;
    while (k < hi && (in->buf[k] == ' ' || in->buf[k] == '\t' || in->buf[k] == '\n')) k++;
    if (k < hi && in->buf[k] == '<') {                                       /* <dest with spaces> */
        ds = k + 1;
        for (de = ds; de < hi && in->buf[de] != '>' && in->buf[de] != '\n'; de++) if (in->buf[de] == '\\') de++;
        if (de >= hi || in->buf[de] != '>') return 0;
        k = de + 1;
    } else {
        int paren = 0;
        ds = k;
        for (de = ds; de < hi; de++) {
            char ch = in->buf[de];
            if (ch == '\\') { de++; continue; }
            if (is_ws(ch)) break;
            if (ch == '(') paren++;
            else if (ch == ')') { if (paren == 0) break; paren--; }
        }
        k = de;
    }
    while (k < hi && is_ws(in->buf[k])) k++;
    if (k < hi && (in->buf[k] == '"' || in->buf[k] == '\'' || in->buf[k] == '(')) {      /* an optional title */
        char q = in->buf[k] == '(' ? ')' : in->buf[k];
        size_t j;
        ts = k + 1;
        for (j = ts; j < hi && in->buf[j] != q; j++) if (in->buf[j] == '\\') j++;
        if (j >= hi) return 0;
        te = j;
        k = j + 1;
        while (k < hi && is_ws(in->buf[k])) k++;
    }
    if (k >= hi || in->buf[k] != ')') return 0;
    end = k + 1;
    emit(in, is_image ? EX_MD_IMAGE : EX_MD_LINK, start, end, 0, 0);
    if (is_image) marker(in, start, start + 1);
    marker(in, open, open + 1);
    if (close > open + 1) emit(in, EX_MD_LINK_TEXT, open + 1, close, 0, 0);
    marker(in, close, close + 2);
    if (de > ds) emit(in, EX_MD_LINK_DEST, ds, de, 0, 0);
    if (te > ts) emit(in, EX_MD_LINK_TITLE, ts, te, 0, 0);
    marker(in, end - 1, end);
    if (close > open + 1 && depth < MD_MAX_DEPTH) scan_range(in, open + 1, close, depth + 1);      /* emphasis and so on inside the text */
    return end - start;
}

static int is_tag_byte(unsigned char ch) { return isalnum(ch) || ch == '_' || ch == '-' || ch == '/' || ch >= 0x80; }

/* ---- the scan ---- */

static void scan_range(Inl *in, size_t lo, size_t hi, int depth) {
    Delim *ds = NULL;
    size_t nd = 0, capd = 0, i = lo;
    const char *b = in->buf;
    if (depth > MD_MAX_DEPTH) return;
    while (i < hi) {
        char ch = b[i];
        size_t used = 0;
        switch (ch) {
        case '\\':
            if (i + 1 < hi && b[i + 1] == '\n') { if (i + 2 < in->blen) emit(in, EX_MD_HARD_BREAK, i, i + 1, 0, 0); i += 2; }
            else if (i + 1 < hi && is_punct((unsigned char)b[i + 1])) { emit(in, EX_MD_ESCAPE, i, i + 2, 0, 0); i += 2; }
            else i++;
            break;
        case '`': {
            size_t run = 1, j;
            size_t end = lim(i, hi);
            while (i + run < hi && b[i + run] == '`') run++;
            for (j = i + run; j < end; j++) {
                size_t r2 = 0;
                if (b[j] != '`') continue;
                while (j + r2 < end && b[j + r2] == '`') r2++;
                if (r2 == run) {
                    emit(in, EX_MD_CODE_SPAN, i, j + run, 0, 0);
                    marker(in, i, i + run);
                    marker(in, j, j + run);
                    used = j + run - i;
                    break;
                }
                j += r2 - 1;
            }
            i += used ? used : run;
            break;
        }
        case '$': {
            size_t k, end = lim(i, hi);
            if (i + 1 < hi && b[i + 1] == '$') {                              /* $$ ... $$ inside a line of text */
                for (k = i + 2; k + 1 < end; k++) {
                    if (b[k] == '\\') { k++; continue; }
                    if (b[k] == '$' && b[k + 1] == '$') break;
                }
                if (k + 1 < end && b[k] == '$' && k > i + 2) { emit(in, EX_MD_MATH_INLINE, i, k + 2, 1, 0); marker(in, i, i + 2); marker(in, k, k + 2); i = k + 2; }
                else i += 2;
                break;
            }
            if (i + 1 < hi && !is_ws(b[i + 1])) {
                for (k = i + 1; k < end; k++) {
                    if (b[k] == '\\') { k++; continue; }
                    if (b[k] == '$' && !is_ws(b[k - 1]) && !(k + 1 < hi && isdigit((unsigned char)b[k + 1])) && k > i + 1) {
                        emit(in, EX_MD_MATH_INLINE, i, k + 1, 0, 0);
                        marker(in, i, i + 1);
                        marker(in, k, k + 1);
                        used = k + 1 - i;
                        break;
                    }
                }
            }
            i += used ? used : 1;
            break;
        }
        case '!':
            if (i + 2 < hi && b[i + 1] == '[' && b[i + 2] == '[') used = (size_t)try_wikilink(in, i, i + 1, hi, 1);
            else if (i + 1 < hi && b[i + 1] == '[') used = try_link(in, i, i + 1, hi, 1, depth);
            i += used ? used : 1;
            break;
        case '[':
            if (i + 1 < hi && b[i + 1] == '[') used = (size_t)try_wikilink(in, i, i, hi, 0);
            else if (i + 1 < hi && b[i + 1] == '^') {                         /* a footnote reference [^id] */
                size_t k = i + 2;
                while (k < hi && b[k] != ']' && !is_ws(b[k]) && b[k] != '[') k++;
                if (k < hi && b[k] == ']' && k > i + 2) { emit(in, EX_MD_FOOTNOTE_REF, i, k + 1, 0, 0); marker(in, i, i + 2); marker(in, k, k + 1); used = k + 1 - i; }
            } else used = try_link(in, i, i, hi, 0, depth);
            i += used ? used : 1;
            break;
        case '<': {
            size_t k;
            if (i + 1 >= hi) { i++; break; }
            if (i + 3 < hi && memcmp(b + i, "<!--", 4) == 0) {
                size_t end = lim(i, hi);
                for (k = i + 4; k + 2 < end && memcmp(b + k, "-->", 3) != 0; k++) {}
                if (k + 2 < end) { emit(in, EX_MD_HTML_INLINE, i, k + 3, 0, 0); used = k + 3 - i; }
            } else if (isalpha((unsigned char)b[i + 1])) {                    /* <https://...> or <a@b.c> */
                size_t colon = 0, at = 0;
                for (k = i + 1; k < hi && b[k] != '>' && !is_ws(b[k]) && b[k] != '<'; k++) { if (b[k] == ':' && !colon) colon = k; if (b[k] == '@' && !at) at = k; }
                if (k < hi && b[k] == '>' && ((colon && colon > i + 2 && colon - i - 1 <= 32) || at)) {
                    emit(in, EX_MD_AUTOLINK, i, k + 1, 0, 0);
                    marker(in, i, i + 1);
                    marker(in, k, k + 1);
                    used = k + 1 - i;
                }
            }
            if (!used && (isalpha((unsigned char)b[i + 1]) || b[i + 1] == '/')) {   /* a tag of the HTML subset */
                size_t ns = i + 1 + (b[i + 1] == '/'), ne = ns;
                char q = 0;
                while (ne < hi && (isalnum((unsigned char)b[ne]) || b[ne] == '-')) ne++;
                if (ne > ns && ex_html_tag_from_name(b + ns, ne - ns) != EX_HTML_TEXT) {
                    for (k = ne; k < lim(i, hi); k++) {
                        if (q) { if (b[k] == q) q = 0; }
                        else if (b[k] == '"' || b[k] == '\'') q = b[k];
                        else if (b[k] == '>') break;
                        else if (b[k] == '<') { k = hi; break; }
                    }
                    if (k < hi && b[k] == '>') { emit(in, EX_MD_HTML_INLINE, i, k + 1, 0, 0); used = k + 1 - i; }
                }
            }
            i += used ? used : 1;
            break;
        }
        case '%':
            if (i + 1 < hi && b[i + 1] == '%') {
                size_t k, end = lim(i, hi);
                for (k = i + 2; k + 1 < end && !(b[k] == '%' && b[k + 1] == '%'); k++) {}
                if (k + 1 < end) { emit(in, EX_MD_COMMENT_INLINE, i, k + 2, 0, 0); marker(in, i, i + 2); marker(in, k, k + 2); i = k + 2; }
                else i += 2;
            } else i++;
            break;
        case '#':
            if ((i == 0 || is_ws(b[i - 1]) || b[i - 1] == '(' || b[i - 1] == '[' || b[i - 1] == '{') && i + 1 < hi && is_tag_byte((unsigned char)b[i + 1])) {
                size_t k = i + 1;
                int has_letter = 0;
                while (k < hi && is_tag_byte((unsigned char)b[k])) { if (!isdigit((unsigned char)b[k])) has_letter = 1; k++; }
                if (has_letter) { emit(in, EX_MD_TAG, i, k, 0, 0); i = k; break; }
            }
            i++;
            break;
        case '^':
            if (i + 1 < hi && b[i + 1] == '[') {                              /* ^[inline footnote] */
                size_t close = match_bracket(in, i + 1, hi);
                if (close != (size_t)-1) {
                    emit(in, EX_MD_INLINE_FOOTNOTE, i, close + 1, 0, 0);
                    marker(in, i, i + 2);
                    marker(in, close, close + 1);
                    if (close > i + 2) scan_range(in, i + 2, close, depth + 1);
                    i = close + 1;
                    break;
                }
            } else if ((i == 0 || is_ws(b[i - 1])) && i + 1 < hi) {           /* ^block-id at the end of a line */
                size_t k = i + 1, e;
                while (k < hi && (isalnum((unsigned char)b[k]) || b[k] == '-')) k++;
                e = k;
                while (e < in->blen && (b[e] == ' ' || b[e] == '\t')) e++;
                if (k > i + 1 && (e >= in->blen || b[e] == '\n') && k <= hi) { emit(in, EX_MD_BLOCK_ID, i, k, 0, 0); i = k; break; }
            }
            i++;
            break;
        case 'h':
            if ((i == 0 || !is_word_byte((unsigned char)b[i - 1])) && (i + 7 < hi && (memcmp(b + i, "http://", 7) == 0 || (i + 8 < hi && memcmp(b + i, "https://", 8) == 0)))) {
                size_t k = i, skip = memcmp(b + i, "https://", 8) == 0 ? 8 : 7;
                int paren = 0;
                for (k = i + skip; k < hi && !is_ws(b[k]) && b[k] != '<' && b[k] != '>'; k++) { if (b[k] == '(') paren++; else if (b[k] == ')') { if (paren == 0) break; paren--; } }
                while (k > i + skip && strchr(".,;:!?'\"*_~", b[k - 1])) k--;     /* trailing punctuation is not part of the address */
                if (k > i + skip) { emit(in, EX_MD_AUTOLINK, i, k, 0, 0); i = k; break; }
            }
            i++;
            break;
        case '*': case '_': case '~': case '=': {
            size_t run = 1, a_before = i;
            int prev, next, left, right, pw, pp, nw, np, open, close;
            while (i + run < hi && b[i + run] == ch) run++;
            prev = a_before > 0 ? (unsigned char)b[a_before - 1] : ' ';
            next = i + run < in->blen ? (unsigned char)b[i + run] : ' ';
            pw = is_ws(prev); pp = is_punct(prev); nw = is_ws(next); np = is_punct(next);
            left = !nw && (!np || pw || pp);
            right = !pw && (!pp || nw || np);
            if (ch == '_') { open = left && (!right || pp); close = right && (!left || np); }
            else if (ch == '*') { open = left; close = right; }
            else { open = run == 2 && !nw; close = run == 2 && !pw; }
            if ((ch == '~' || ch == '=') && run != 2) { i += run; break; }
            if ((open || close)) {
                if (nd == capd) {
                    Delim *nds = realloc(ds, (capd ? capd * 2 : 16) * sizeof *nds);
                    if (!nds) { i += run; break; }
                    ds = nds;
                    capd = capd ? capd * 2 : 16;
                }
                ds[nd].pos = i; ds[nd].len = run; ds[nd].rem = run; ds[nd].ch = ch; ds[nd].open = open; ds[nd].close = close;
                nd++;
            }
            i += run;
            break;
        }
        case '\n': {
            size_t sp = 0;
            while (i > lo + sp && b[i - 1 - sp] == ' ') sp++;
            if (sp >= 2 && i + 1 < in->blen) emit(in, EX_MD_HARD_BREAK, i - sp, i, 0, 0);
            i++;
            break;
        }
        default:
            i++;
            break;
        }
    }
    resolve(in, ds, nd);
    free(ds);
}

void md_inline(Ctx *c, const VLine *lines, size_t n) {
    Inl in;
    size_t i, total = 0, pos = 0;
    if (n == 0) return;
    for (i = 0; i < n; i++) total += lines[i].e - lines[i].s + 1;
    in.c = c;
    in.buf = malloc(total + 1);
    in.segs = malloc(n * sizeof *in.segs);
    in.nsegs = n;
    if (!in.buf || !in.segs) { free(in.buf); free(in.segs); c->oom = 1; return; }
    for (i = 0; i < n; i++) {
        size_t len = lines[i].e - lines[i].s;
        in.segs[i].lstart = pos; in.segs[i].len = len; in.segs[i].abs = lines[i].s;
        memcpy(in.buf + pos, c->t + lines[i].s, len);
        pos += len;
        if (i + 1 < n) in.buf[pos++] = '\n';
    }
    in.blen = pos;
    in.buf[pos] = 0;
    scan_range(&in, 0, in.blen, 0);
    free(in.buf);
    free(in.segs);
}
