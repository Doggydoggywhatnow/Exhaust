#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_html.h"
#include "exhaust_markdown.h"
#include "exhaust_reading.h"
#include "md4c.h"

/* ---- growing buffers ---- */

typedef struct Str { char *p; size_t n, cap; } Str;

static void str_put(Str *s, const char *t, size_t n) {
    if (s->n + n + 1 > s->cap) {
        size_t nc = (s->n + n + 1) * 2;
        char *np = realloc(s->p, nc);
        if (!np) return;
        s->p = np; s->cap = nc;
    }
    memcpy(s->p + s->n, t, n);
    s->n += n;
    s->p[s->n] = 0;
}
static void str_cat(Str *s, const char *t) { str_put(s, t, strlen(t)); }
static char *dup_n(const char *s, size_t n) { char *p = malloc(n + 1); if (p) { memcpy(p, s, n); p[n] = 0; } return p; }

/* ---- the model ---- */

struct ExReading {
    ExRBlock *b; size_t n, cap;
};

static void free_spans(ExRSpan *sp, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) { free(sp[i].text); free(sp[i].target); }
    free(sp);
}

static void free_block(ExRBlock *b) {
    size_t i;
    free_spans(b->spans, b->nspans);
    free(b->code); free(b->lang); free(b->marker);
    for (i = 0; i < b->ncells; i++) free_spans(b->cells[i].spans, b->cells[i].n);
    free(b->cells);
}

void ex_reading_free(ExReading *r) {
    size_t i;
    if (!r) return;
    for (i = 0; i < r->n; i++) free_block(&r->b[i]);
    free(r->b);
    free(r);
}

size_t ex_reading_block_count(const ExReading *r) { return r->n; }
const ExRBlock *ex_reading_block(const ExReading *r, size_t i) { return i < r->n ? &r->b[i] : NULL; }

static int push_block(ExReading *r, const ExRBlock *b) {
    if (r->n == r->cap) {
        size_t nc = r->cap ? r->cap * 2 : 64;
        ExRBlock *nb = realloc(r->b, nc * sizeof *nb);
        if (!nb) return -1;
        r->b = nb; r->cap = nc;
    }
    r->b[r->n++] = *b;
    return 0;
}

/* ---- the preprocessing pass: what md4c does not know ---- */

typedef struct Foot { char *id; char *text; } Foot;
typedef struct Pre { Str out; Foot *foot; size_t nfoot, capfoot; } Pre;

static void hex_put(Str *s, const char *t, size_t n) {
    static const char *hx = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) { char c[2]; c[0] = hx[(unsigned char)t[i] >> 4]; c[1] = hx[(unsigned char)t[i] & 15]; str_put(s, c, 2); }
}

#define M_HL_OPEN "\xEE\x80\x80"
#define M_HL_CLOSE "\xEE\x80\x81"
#define M_FN_OPEN "\xEE\x80\x82"
#define M_FN_CLOSE "\xEE\x80\x83"
#define M_EMB_OPEN "\xEE\x80\x84"
#define M_EMB_CLOSE "\xEE\x80\x85"

static int is_fence_line(const char *l, size_t n, char *ch, size_t *run) {
    size_t i = 0, k;
    while (i < n && i < 4 && l[i] == ' ') i++;
    if (i >= 4 || i >= n || (l[i] != '`' && l[i] != '~')) return 0;
    for (k = i; k < n && l[k] == l[i]; k++) {}
    if (k - i < 3) return 0;
    *ch = l[i]; *run = k - i;
    return 1;
}

static int footnote_def_at(const char *l, size_t n, size_t *id_s, size_t *id_e, size_t *text_s) {
    size_t i = 0, k;
    while (i < n && i < 4 && l[i] == ' ') i++;
    if (i >= 4 || i + 3 >= n || l[i] != '[' || l[i + 1] != '^') return 0;
    for (k = i + 2; k < n && l[k] != ']' && !isspace((unsigned char)l[k]); k++) {}
    if (k >= n || l[k] != ']' || k == i + 2 || k + 1 >= n || l[k + 1] != ':') return 0;
    *id_s = i + 2; *id_e = k;
    k += 2;
    while (k < n && l[k] == ' ') k++;
    *text_s = k;
    return 1;
}

static void add_foot(Pre *pre, const char *id, size_t idn, Str *text);

/* One line of text: comments out, highlights, footnote references and embeds turned into markers, a block ID off the end. */
static void inline_pass(Pre *pre, const char *l, size_t n, int *in_comment) {
    Str o = {0};
    size_t i = 0, end = n;
    while (i < n) {
        if (*in_comment) {                                           /* inside a %% comment that began on an earlier line */
            while (i + 1 < n && !(l[i] == '%' && l[i + 1] == '%')) i++;
            if (i + 1 < n) { *in_comment = 0; i += 2; } else i = n;
            continue;
        }
        if (l[i] == '\\' && i + 1 < n) { str_put(&o, l + i, 2); i += 2; continue; }
        if (l[i] == '`') {                                           /* a code span is left alone */
            size_t run = 1, j;
            while (i + run < n && l[i + run] == '`') run++;
            for (j = i + run; j < n; j++) {
                size_t r2 = 0;
                if (l[j] != '`') continue;
                while (j + r2 < n && l[j + r2] == '`') r2++;
                if (r2 == run) { str_put(&o, l + i, j + run - i); i = j + run; goto next; }
                j += r2 - 1;
            }
            str_put(&o, l + i, run);
            i += run;
            continue;
        }
        if (l[i] == '%' && i + 1 < n && l[i + 1] == '%') {            /* a comment: gone */
            size_t j;
            for (j = i + 2; j + 1 < n && !(l[j] == '%' && l[j + 1] == '%'); j++) {}
            if (j + 1 < n) i = j + 2; else { *in_comment = 1; i = n; }
            continue;
        }
        if (l[i] == '=' && i + 1 < n && l[i + 1] == '=' && i + 2 < n && !isspace((unsigned char)l[i + 2]) && l[i + 2] != '=') {
            size_t j;
            for (j = i + 2; j + 1 < n; j++) if (l[j] == '=' && l[j + 1] == '=' && !isspace((unsigned char)l[j - 1])) break;
            if (j + 1 < n && l[j] == '=') {
                str_cat(&o, M_HL_OPEN);
                str_put(&o, l + i + 2, j - i - 2);
                str_cat(&o, M_HL_CLOSE);
                i = j + 2;
                continue;
            }
        }
        if (l[i] == '!' && i + 3 < n && l[i + 1] == '[' && l[i + 2] == '[') {      /* ![[embed]] */
            size_t j;
            for (j = i + 3; j + 1 < n && !(l[j] == ']' && l[j + 1] == ']'); j++) {}
            if (j + 1 < n && j > i + 3) { str_cat(&o, M_EMB_OPEN); hex_put(&o, l + i + 3, j - i - 3); str_cat(&o, M_EMB_CLOSE); i = j + 2; continue; }
        }
        if (l[i] == '^' && i + 2 < n && l[i + 1] == '[') {                          /* ^[an inline footnote] */
            size_t j;
            int depth = 0;
            for (j = i + 1; j < n; j++) {
                if (l[j] == '\\') { j++; continue; }
                if (l[j] == '[') depth++;
                else if (l[j] == ']' && --depth == 0) break;
            }
            if (j < n && l[j] == ']' && j > i + 2) {
                char id[32];
                Str body = {0};
                snprintf(id, sizeof id, "inline-%zu", pre->nfoot + 1);
                str_put(&body, l + i + 2, j - i - 2);
                add_foot(pre, id, strlen(id), &body);
                str_cat(&o, M_FN_OPEN); hex_put(&o, id, strlen(id)); str_cat(&o, M_FN_CLOSE);
                i = j + 1;
                continue;
            }
        }
        if (l[i] == '[' && i + 2 < n && l[i + 1] == '^') {                          /* [^id] */
            size_t j;
            for (j = i + 2; j < n && l[j] != ']' && !isspace((unsigned char)l[j]) && l[j] != '['; j++) {}
            if (j < n && l[j] == ']' && j > i + 2) { str_cat(&o, M_FN_OPEN); hex_put(&o, l + i + 2, j - i - 2); str_cat(&o, M_FN_CLOSE); i = j + 1; continue; }
        }
        str_put(&o, l + i, 1);
        i++;
next:
        ;
    }
    /* a block ID at the end of the line is not shown */
    if (o.p) {
        size_t e = o.n, k;
        while (e > 0 && (o.p[e - 1] == ' ' || o.p[e - 1] == '\t')) e--;
        for (k = e; k > 0 && (isalnum((unsigned char)o.p[k - 1]) || o.p[k - 1] == '-'); k--) {}
        if (k < e && k > 0 && o.p[k - 1] == '^' && (k == 1 || o.p[k - 2] == ' ' || o.p[k - 2] == '\t')) { o.n = k >= 2 ? k - 2 : 0; o.p[o.n] = 0; }
        end = o.n;
        str_put(&pre->out, o.p, end);
    }
    free(o.p);
    if (!*in_comment) str_put(&pre->out, "\n", 1);          /* a comment that goes on swallows the line break too */
}

static void add_foot(Pre *pre, const char *id, size_t idn, Str *text) {
    if (pre->nfoot == pre->capfoot) {
        size_t nc = pre->capfoot ? pre->capfoot * 2 : 8;
        Foot *nf = realloc(pre->foot, nc * sizeof *nf);
        if (!nf) { free(text->p); return; }
        pre->foot = nf; pre->capfoot = nc;
    }
    pre->foot[pre->nfoot].id = dup_n(id, idn);
    pre->foot[pre->nfoot].text = text->p ? text->p : dup_n("", 0);
    pre->nfoot++;
}

/* A task with any status character other than a space counts as done (as in Obsidian): the character becomes x for md4c. */
static void task_status(char *l, size_t n) {
    size_t i = 0;
    while (i < n && i < 3 && l[i] == ' ') i++;
    if (i < n && (l[i] == '-' || l[i] == '*' || l[i] == '+')) i++;
    else { size_t d = i; while (d < n && isdigit((unsigned char)l[d])) d++; if (d > i && d < n && (l[d] == '.' || l[d] == ')')) i = d + 1; else return; }
    if (i + 4 < n + 0 && l[i] == ' ' && l[i + 1] == '[' && l[i + 3] == ']' && (i + 4 >= n || l[i + 4] == ' ') && l[i + 2] != ' ' && l[i + 2] != 'x' && l[i + 2] != 'X') l[i + 2] = 'x';
}

static void preprocess(const char *t, size_t len, Pre *pre) {
    size_t pos = 0;
    int in_fence = 0, in_comment = 0, first = 1;
    char fch = 0;
    size_t frun = 0;
    Str ftext = {0};
    char fid[128] = "";
    int in_foot = 0;
    /* front matter */
    if (len >= 4 && memcmp(t, "---", 3) == 0 && (t[3] == '\n' || t[3] == '\r')) {
        size_t p = 3, e;
        while (p < len && t[p] != '\n') p++;
        p++;
        while (p < len) {
            e = p;
            while (e < len && t[e] != '\n') e++;
            if ((e - p == 3 || (e - p == 4 && t[p + 3] == '\r')) && (memcmp(t + p, "---", 3) == 0 || memcmp(t + p, "...", 3) == 0)) { pos = e < len ? e + 1 : len; break; }
            p = e + 1;
        }
    }
    (void)first;
    while (pos < len) {
        size_t e = pos, n, id_s, id_e, text_s;
        const char *l;
        while (e < len && t[e] != '\n') e++;
        n = e - pos;
        l = t + pos;
        if (n && l[n - 1] == '\r') n--;
        if (in_fence) {
            char c; size_t run;
            str_put(&pre->out, l, n); str_put(&pre->out, "\n", 1);
            if (is_fence_line(l, n, &c, &run) && c == fch && run >= frun) in_fence = 0;
        } else if (is_fence_line(l, n, &fch, &frun)) {
            if (in_foot) { add_foot(pre, fid, strlen(fid), &ftext); memset(&ftext, 0, sizeof ftext); in_foot = 0; }
            in_fence = 1;
            str_put(&pre->out, l, n); str_put(&pre->out, "\n", 1);
        } else if (!in_comment && footnote_def_at(l, n, &id_s, &id_e, &text_s)) {
            if (in_foot) { add_foot(pre, fid, strlen(fid), &ftext); memset(&ftext, 0, sizeof ftext); }
            snprintf(fid, sizeof fid, "%.*s", (int)(id_e - id_s < 127 ? id_e - id_s : 127), l + id_s);
            str_put(&ftext, l + text_s, n - text_s);
            in_foot = 1;
            str_put(&pre->out, "\n", 1);                              /* the line stays empty so line structure is kept */
        } else if (in_foot && n > 0 && (l[0] == '\t' || (n >= 4 && memcmp(l, "    ", 4) == 0))) {      /* a continuation of a footnote */
            size_t s = 0;
            while (s < n && (l[s] == ' ' || l[s] == '\t')) s++;
            str_put(&ftext, "\n", 1);
            str_put(&ftext, l + s, n - s);
            str_put(&pre->out, "\n", 1);
        } else {
            if (in_foot) { add_foot(pre, fid, strlen(fid), &ftext); memset(&ftext, 0, sizeof ftext); in_foot = 0; }
            if (!in_comment) { char *copy = dup_n(l, n); if (copy) { task_status(copy, n); inline_pass(pre, copy, n, &in_comment); free(copy); } }
            else inline_pass(pre, l, n, &in_comment);
        }
        pos = e + 1;
    }
    if (in_foot) add_foot(pre, fid, strlen(fid), &ftext);
}

/* ---- building the blocks from md4c's events ---- */

typedef struct Frame { int kind; int callout; int id; int fold; int maybe_callout; int ordered; unsigned counter; } Frame;   /* kind: 0 quote or callout, 1 list */

typedef struct Ctx {
    ExReading *r;
    int depth;
    Pre *pre;
    /* inline state */
    ExRSpan *sp; size_t nsp, capsp;
    unsigned style;
    int hl;
    ExRSpanKind link_kind; char *link_target;
    int last_char;
    int in_ref; Str ref;             /* collecting the payload of a footnote or embed marker */
    /* block state */
    int in_block;                    /* a leaf block is open (spans are being collected) */
    ExRBlockKind kind; int level;
    Frame frames[EX_READ_MAX_FRAMES * 2]; int nframes;
    char *pending_marker; int pending_task;
    int next_callout_id;
    int in_code; Str code; char *code_lang;
    int in_html; Str html;
    Str math; int in_math, math_display;
    int in_table, in_thead;
    ExRCell *cells; size_t ncells, capcells;
    int in_cell; int cell_align;
    char **fn_names; size_t nfn, capfn;   /* footnote names in order of first reference */
    int fail;
} Ctx;

static void spans_push(Ctx *c, ExRSpanKind kind, unsigned style, const char *text, size_t n, const char *target) {
    ExRSpan *last = c->nsp ? &c->sp[c->nsp - 1] : NULL;
    int same_link = last && last->kind == kind && (kind == EX_RSK_LINK_EXTERNAL || kind == EX_RSK_LINK_INTERNAL) && last->style == style && last->target && target && strcmp(last->target, target) == 0;
    if (last && ((kind == EX_RSK_TEXT && last->kind == EX_RSK_TEXT && last->style == style && !last->target && !target) || same_link)) {   /* adjacent equal pieces are one */
        size_t ol = strlen(last->text);
        char *t = realloc(last->text, ol + n + 1);
        if (t) { memcpy(t + ol, text, n); t[ol + n] = 0; last->text = t; }
        return;
    }
    if (c->nsp == c->capsp) {
        size_t nc = c->capsp ? c->capsp * 2 : 16;
        ExRSpan *ns = realloc(c->sp, nc * sizeof *ns);
        if (!ns) { c->fail = 1; return; }
        c->sp = ns; c->capsp = nc;
    }
    c->sp[c->nsp].kind = kind;
    c->sp[c->nsp].style = style;
    c->sp[c->nsp].text = dup_n(text, n);
    c->sp[c->nsp].target = target ? dup_n(target, strlen(target)) : NULL;
    c->nsp++;
}

static int footnote_number(Ctx *c, const char *name) {
    size_t i;
    for (i = 0; i < c->nfn; i++) if (strcmp(c->fn_names[i], name) == 0) return (int)i + 1;
    if (c->nfn == c->capfn) {
        size_t nc = c->capfn ? c->capfn * 2 : 8;
        char **nn = realloc(c->fn_names, nc * sizeof *nn);
        if (!nn) return 0;
        c->fn_names = nn; c->capfn = nc;
    }
    c->fn_names[c->nfn] = dup_n(name, strlen(name));
    return (int)++c->nfn;
}

static char *unhex(const char *h, size_t n) {
    char *o = malloc(n / 2 + 1);
    size_t i;
    if (!o) return NULL;
    for (i = 0; i + 1 < n; i += 2) {
        int a = isdigit((unsigned char)h[i]) ? h[i] - '0' : h[i] - 'a' + 10, b = isdigit((unsigned char)h[i + 1]) ? h[i + 1] - '0' : h[i + 1] - 'a' + 10;
        o[i / 2] = (char)(a * 16 + b);
    }
    o[n / 2] = 0;
    return o;
}

static int is_tag_char(unsigned char ch) { return isalnum(ch) || ch == '_' || ch == '-' || ch == '/' || ch >= 0x80; }

/* Plain text with tags found in it. */
static void text_with_tags(Ctx *c, const char *t, size_t n, unsigned style) {
    size_t i = 0, start = 0;
    while (i < n) {
        int prev = i > 0 ? (unsigned char)t[i - 1] : c->last_char;
        if (t[i] == '#' && c->link_kind == EX_RSK_TEXT && !(style & EX_RS_CODE) && (prev == 0 || isspace(prev) || prev == '(' || prev == '[' || prev == '{') && i + 1 < n && is_tag_char((unsigned char)t[i + 1])) {
            size_t k = i + 1;
            int has_letter = 0;
            while (k < n && is_tag_char((unsigned char)t[k])) { if (!isdigit((unsigned char)t[k])) has_letter = 1; k++; }
            if (has_letter) {
                char *target = dup_n(t + i + 1, k - i - 1);
                if (i > start) spans_push(c, EX_RSK_TEXT, style, t + start, i - start, NULL);
                spans_push(c, EX_RSK_TAG, style, t + i, k - i, target);
                free(target);
                start = i = k;
                continue;
            }
        }
        i++;
    }
    if (n > start) spans_push(c, c->link_kind, style, t + start, n - start, c->link_target);
}

static void add_text(Ctx *c, const char *t, size_t n, unsigned extra) {
    size_t i = 0, seg = 0;
    unsigned style = c->style | extra | (c->hl ? EX_RS_HIGHLIGHT : 0);
    if (n == 0) return;
    while (i < n) {
        if (c->in_ref) {                                                           /* inside a footnote or embed marker */
            if ((unsigned char)t[i] == 0xEE && i + 2 < n && (unsigned char)t[i + 1] == 0x80 && ((unsigned char)t[i + 2] == 0x83 || (unsigned char)t[i + 2] == 0x85)) {
                char *name = unhex(c->ref.p ? c->ref.p : "", c->ref.n);
                if (name) {
                    if (c->in_ref == 1) {
                        char num[16];
                        int k = footnote_number(c, name);
                        snprintf(num, sizeof num, "%d", k);
                        spans_push(c, EX_RSK_FOOTNOTE_REF, (style & ~(unsigned)EX_RS_SUP) | EX_RS_SUP, num, strlen(num), name);
                    } else {
                        spans_push(c, EX_RSK_EMBED, style, name, strlen(name), name);
                    }
                    free(name);
                }
                c->ref.n = 0;
                if (c->ref.p) c->ref.p[0] = 0;
                c->in_ref = 0;
                i += 3; seg = i;
                continue;
            }
            str_put(&c->ref, t + i, 1);
            i++; seg = i;
            continue;
        }
        if ((unsigned char)t[i] == 0xEE && i + 2 < n && (unsigned char)t[i + 1] == 0x80 && (unsigned char)t[i + 2] >= 0x80 && (unsigned char)t[i + 2] <= 0x85) {
            unsigned char m = (unsigned char)t[i + 2];
            if (i > seg) text_with_tags(c, t + seg, i - seg, style);
            if (m == 0x80) c->hl = 1;
            else if (m == 0x81) c->hl = 0;
            else if (m == 0x82) { c->in_ref = 1; c->ref.n = 0; }
            else if (m == 0x84) { c->in_ref = 2; c->ref.n = 0; }
            style = c->style | extra | (c->hl ? EX_RS_HIGHLIGHT : 0);
            i += 3; seg = i;
            continue;
        }
        i++;
    }
    if (n > seg) text_with_tags(c, t + seg, n - seg, style);
    c->last_char = (unsigned char)t[n - 1];
}

/* ---- blocks ---- */

static void take_frames(Ctx *c, ExRBlock *b) {
    int i, q = 0;
    b->list_depth = 0;
    for (i = 0; i < c->nframes && q < EX_READ_MAX_FRAMES; i++) {
        if (c->frames[i].kind == 0) { b->frame_callout[q] = c->frames[i].callout; b->frame_id[q] = c->frames[i].id; b->frame_fold[q] = c->frames[i].fold; q++; }
        else b->list_depth++;
    }
    b->nframes = q;
}

static void emit(Ctx *c, ExRBlock *b) {
    take_frames(c, b);
    if (c->pending_marker) { b->marker = c->pending_marker; b->task = c->pending_task; c->pending_marker = NULL; c->pending_task = 0; }
    if (push_block(c->r, b) != 0) c->fail = 1;
}

static void clear_inline(Ctx *c) { c->sp = NULL; c->nsp = c->capsp = 0; c->style = 0; c->hl = 0; c->link_kind = EX_RSK_TEXT; free(c->link_target); c->link_target = NULL; c->last_char = 0; c->in_ref = 0; c->ref.n = 0; }

/* A trailing space or line break is not content. */
static void trim_spans(ExRSpan *sp, size_t *n) {
    while (*n > 0 && sp[*n - 1].kind == EX_RSK_BREAK) { free(sp[*n - 1].text); free(sp[*n - 1].target); (*n)--; }
}

static int quote_frame_top(Ctx *c) { return c->nframes > 0 && c->frames[c->nframes - 1].kind == 0 ? c->nframes - 1 : -1; }

/* Plain text of spans up to the first break (for the first line of a callout). */
static char *first_line_text(const ExRSpan *sp, size_t n, size_t *upto) {
    Str s = {0};
    size_t i;
    for (i = 0; i < n && sp[i].kind != EX_RSK_BREAK; i++) str_cat(&s, sp[i].text ? sp[i].text : "");
    *upto = i;
    if (!s.p) s.p = dup_n("", 0);
    return s.p;
}

static void finish_paragraph(Ctx *c) {
    ExRBlock b;
    int qf = quote_frame_top(c);
    memset(&b, 0, sizeof b);
    trim_spans(c->sp, &c->nsp);
    if (qf >= 0 && c->frames[qf].maybe_callout && c->nsp > 0) {                    /* is this quote a callout: [!type][+-] title */
        size_t upto = 0;
        char *line = first_line_text(c->sp, c->nsp, &upto);
        c->frames[qf].maybe_callout = 0;
        if (line[0] == '[' && line[1] == '!') {
            size_t te = 2;
            while (line[te] && line[te] != ']' && !isspace((unsigned char)line[te])) te++;
            if (line[te] == ']' && te > 2) {
                int known, fold = 0;
                ExCalloutType ct = ex_callout_type(line + 2, te - 2, &known);
                size_t k = te + 1;
                ExRBlock tb;
                ExRSpan *rest;
                size_t i, nrest = 0;
                if (line[k] == '+' || line[k] == '-') { fold = line[k] == '+' ? 1 : 2; k++; }
                if (line[k] == 0 || isspace((unsigned char)line[k])) {
                    while (line[k] && isspace((unsigned char)line[k])) k++;
                    c->frames[qf].callout = (int)ct;
                    c->frames[qf].fold = fold;
                    c->frames[qf].id = c->next_callout_id++;
                    memset(&tb, 0, sizeof tb);
                    tb.kind = EX_RBK_CALLOUT_TITLE;
                    tb.spans = calloc(1, sizeof *tb.spans);
                    if (tb.spans) {
                        tb.spans[0].kind = EX_RSK_TEXT; tb.spans[0].style = EX_RS_BOLD;
                        tb.spans[0].text = dup_n(line[k] ? line + k : ex_callout_name(ct), strlen(line[k] ? line + k : ex_callout_name(ct)));
                        tb.nspans = 1;
                    }
                    emit(c, &tb);
                    /* what comes after the first line is the callout's first paragraph */
                    rest = c->sp + (upto < c->nsp ? upto + 1 : c->nsp);
                    nrest = c->nsp - (upto < c->nsp ? upto + 1 : c->nsp);
                    for (i = 0; i < (upto < c->nsp ? upto + 1 : c->nsp); i++) { free(c->sp[i].text); free(c->sp[i].target); }
                    if (nrest > 0) {
                        b.kind = EX_RBK_PARAGRAPH;
                        b.spans = malloc(nrest * sizeof *b.spans);
                        if (b.spans) { memcpy(b.spans, rest, nrest * sizeof *b.spans); b.nspans = nrest; emit(c, &b); }
                    }
                    free(c->sp); c->sp = NULL; c->nsp = c->capsp = 0;
                    free(line);
                    return;
                }
            }
        }
        free(line);
    }
    if (qf >= 0) c->frames[qf].maybe_callout = 0;
    /* a paragraph that is only display math is a math block; one that is only a block id or empty is dropped */
    if (c->nsp == 1 && c->sp[0].kind == EX_RSK_MATH && c->sp[0].target && strcmp(c->sp[0].target, "display") == 0) {
        b.kind = EX_RBK_MATH;
        b.code = c->sp[0].text; c->sp[0].text = NULL;
        free_spans(c->sp, c->nsp);
        c->sp = NULL; c->nsp = c->capsp = 0;
        emit(c, &b);
        return;
    }
    if (c->nsp == 0) { free(c->sp); c->sp = NULL; c->nsp = c->capsp = 0; return; }
    b.kind = EX_RBK_PARAGRAPH;
    b.spans = c->sp; b.nspans = c->nsp;
    c->sp = NULL; c->nsp = c->capsp = 0;
    emit(c, &b);
}

static void ensure_leaf(Ctx *c, ExRBlockKind kind) {
    if (!c->in_block) { c->in_block = 1; c->kind = kind; c->level = 0; clear_inline(c); }
}

static void end_leaf(Ctx *c) {
    ExRBlock b;
    if (!c->in_block) return;
    c->in_block = 0;
    if (c->kind == EX_RBK_HEADING) {
        memset(&b, 0, sizeof b);
        trim_spans(c->sp, &c->nsp);
        b.kind = EX_RBK_HEADING; b.level = c->level;
        b.spans = c->sp; b.nspans = c->nsp;
        c->sp = NULL; c->nsp = c->capsp = 0;
        emit(c, &b);
    } else finish_paragraph(c);
}

/* ---- md4c callbacks ---- */

static int enter_block(MD_BLOCKTYPE type, void *detail, void *user) {
    Ctx *c = user;
    switch (type) {
    case MD_BLOCK_QUOTE:
        end_leaf(c);
        if (c->nframes < (int)(sizeof c->frames / sizeof c->frames[0])) { Frame f; memset(&f, 0, sizeof f); f.kind = 0; f.callout = -1; f.maybe_callout = 1; c->frames[c->nframes++] = f; }
        break;
    case MD_BLOCK_UL: case MD_BLOCK_OL:
        end_leaf(c);
        if (c->nframes < (int)(sizeof c->frames / sizeof c->frames[0])) {
            Frame f;
            memset(&f, 0, sizeof f);
            f.kind = 1; f.ordered = type == MD_BLOCK_OL; f.counter = type == MD_BLOCK_OL ? ((MD_BLOCK_OL_DETAIL *)detail)->start : 0;
            c->frames[c->nframes++] = f;
        }
        { int qf = quote_frame_top(c); (void)qf; }
        break;
    case MD_BLOCK_LI: {
        MD_BLOCK_LI_DETAIL *d = detail;
        Frame *lf = NULL;
        char buf[32];
        int i;
        end_leaf(c);
        for (i = c->nframes - 1; i >= 0; i--) if (c->frames[i].kind == 1) { lf = &c->frames[i]; break; }
        if (lf) {
            if (lf->ordered) { snprintf(buf, sizeof buf, "%u.", lf->counter); lf->counter++; } else snprintf(buf, sizeof buf, "\xE2\x80\xA2");
            free(c->pending_marker);
            c->pending_marker = dup_n(buf, strlen(buf));
        }
        c->pending_task = d && d->is_task ? (d->task_mark == ' ' ? 1 : (d->task_mark == 'x' || d->task_mark == 'X') ? 2 : 3) : 0;
        { int qf = quote_frame_top(c); if (qf >= 0) c->frames[qf].maybe_callout = 0; }
        break;
    }
    case MD_BLOCK_HR: {
        ExRBlock b;
        end_leaf(c);
        memset(&b, 0, sizeof b);
        b.kind = EX_RBK_HR;
        emit(c, &b);
        break;
    }
    case MD_BLOCK_H: ensure_leaf(c, EX_RBK_HEADING); c->level = (int)((MD_BLOCK_H_DETAIL *)detail)->level; break;
    case MD_BLOCK_CODE: {
        MD_BLOCK_CODE_DETAIL *d = detail;
        end_leaf(c);
        c->in_code = 1;
        c->code.n = 0;
        if (c->code.p) c->code.p[0] = 0;
        free(c->code_lang);
        c->code_lang = d->lang.text ? dup_n(d->lang.text, d->lang.size) : NULL;
        { int qf = quote_frame_top(c); if (qf >= 0) c->frames[qf].maybe_callout = 0; }
        break;
    }
    case MD_BLOCK_HTML: end_leaf(c); c->in_html = 1; c->html.n = 0; if (c->html.p) c->html.p[0] = 0; break;
    case MD_BLOCK_P: ensure_leaf(c, EX_RBK_PARAGRAPH); break;
    case MD_BLOCK_TABLE: end_leaf(c); c->in_table = 1; { int qf = quote_frame_top(c); if (qf >= 0) c->frames[qf].maybe_callout = 0; } break;
    case MD_BLOCK_THEAD: c->in_thead = 1; break;
    case MD_BLOCK_TBODY: break;
    case MD_BLOCK_TR: c->cells = NULL; c->ncells = c->capcells = 0; break;
    case MD_BLOCK_TH: case MD_BLOCK_TD:
        c->in_cell = 1;
        c->cell_align = (int)((MD_BLOCK_TD_DETAIL *)detail)->align;       /* MD_ALIGN_DEFAULT 0, LEFT 1, CENTER 2, RIGHT 3 */
        clear_inline(c);
        break;
    default: break;
    }
    return c->fail ? -1 : 0;
}

static int leave_block(MD_BLOCKTYPE type, void *detail, void *user) {
    Ctx *c = user;
    (void)detail;
    switch (type) {
    case MD_BLOCK_QUOTE: case MD_BLOCK_UL: case MD_BLOCK_OL:
        end_leaf(c);
        if (c->nframes > 0) c->nframes--;
        break;
    case MD_BLOCK_LI: end_leaf(c); free(c->pending_marker); c->pending_marker = NULL; c->pending_task = 0; break;
    case MD_BLOCK_H: case MD_BLOCK_P: end_leaf(c); break;
    case MD_BLOCK_CODE: {
        ExRBlock b;
        size_t n = c->code.n;
        memset(&b, 0, sizeof b);
        while (n > 0 && c->code.p[n - 1] == '\n') n--;
        b.kind = EX_RBK_CODE;
        b.code = dup_n(c->code.p ? c->code.p : "", n);
        b.lang = c->code_lang; c->code_lang = NULL;
        c->in_code = 0;
        emit(c, &b);
        break;
    }
    case MD_BLOCK_HTML: {
        ExHtml *h;
        char *text;
        ExRBlock b;
        c->in_html = 0;
        h = ex_html_parse(c->html.p ? c->html.p : "", c->html.n);
        text = h ? ex_html_text(h) : NULL;
        if (text) {
            size_t i = 0, o = 0;
            int sp = 1;
            for (; text[i]; i++) {                                         /* collapse white space: the layout is the reader's */
                if (isspace((unsigned char)text[i])) { if (!sp) text[o++] = ' '; sp = 1; } else { text[o++] = text[i]; sp = 0; }
            }
            while (o > 0 && text[o - 1] == ' ') o--;
            text[o] = 0;
            if (o > 0) {
                memset(&b, 0, sizeof b);
                b.kind = EX_RBK_PARAGRAPH;
                b.spans = calloc(1, sizeof *b.spans);
                if (b.spans) { b.spans[0].kind = EX_RSK_TEXT; b.spans[0].text = dup_n(text, o); b.nspans = 1; emit(c, &b); }
            }
        }
        free(text);
        ex_html_free(h);
        break;
    }
    case MD_BLOCK_THEAD: c->in_thead = 0; break;
    case MD_BLOCK_TH: case MD_BLOCK_TD:
        if (c->ncells == c->capcells) {
            size_t nc = c->capcells ? c->capcells * 2 : 8;
            ExRCell *nn = realloc(c->cells, nc * sizeof *nn);
            if (!nn) { c->fail = 1; break; }
            c->cells = nn; c->capcells = nc;
        }
        c->cells[c->ncells].spans = c->sp; c->cells[c->ncells].n = c->nsp; c->cells[c->ncells].align = c->cell_align;
        c->ncells++;
        c->sp = NULL; c->nsp = c->capsp = 0;
        c->in_cell = 0;
        break;
    case MD_BLOCK_TR: {
        ExRBlock b;
        memset(&b, 0, sizeof b);
        b.kind = EX_RBK_TABLE_ROW; b.header_row = c->in_thead;
        b.cells = c->cells; b.ncells = c->ncells;
        c->cells = NULL; c->ncells = c->capcells = 0;
        emit(c, &b);
        break;
    }
    case MD_BLOCK_TABLE: c->in_table = 0; break;
    default: break;
    }
    return c->fail ? -1 : 0;
}

static char *attr_dup(const MD_ATTRIBUTE *a) { return dup_n(a->text ? a->text : "", a->text ? a->size : 0); }

/* A wikilink shows its target as "Page > Heading" when it has no label of its own. */
static int enter_span(MD_SPANTYPE type, void *detail, void *user) {
    Ctx *c = user;
    switch (type) {
    case MD_SPAN_EM: c->style |= EX_RS_ITALIC; break;
    case MD_SPAN_STRONG: c->style |= EX_RS_BOLD; break;
    case MD_SPAN_DEL: c->style |= EX_RS_STRIKE; break;
    case MD_SPAN_U: c->style |= EX_RS_UNDERLINE; break;
    case MD_SPAN_CODE: c->style |= EX_RS_CODE; break;
    case MD_SPAN_A:
        free(c->link_target);
        c->link_kind = EX_RSK_LINK_EXTERNAL;
        c->link_target = attr_dup(&((MD_SPAN_A_DETAIL *)detail)->href);
        break;
    case MD_SPAN_WIKILINK:
        free(c->link_target);
        c->link_kind = EX_RSK_LINK_INTERNAL;
        c->link_target = attr_dup(&((MD_SPAN_WIKILINK_DETAIL *)detail)->target);
        break;
    case MD_SPAN_IMG: {
        free(c->link_target);
        c->link_kind = EX_RSK_IMAGE;
        c->link_target = attr_dup(&((MD_SPAN_IMG_DETAIL *)detail)->src);
        break;
    }
    case MD_SPAN_LATEXMATH: case MD_SPAN_LATEXMATH_DISPLAY: c->in_math = 1; c->math_display = type == MD_SPAN_LATEXMATH_DISPLAY; c->math.n = 0; if (c->math.p) c->math.p[0] = 0; break;
    default: break;
    }
    return 0;
}

static int leave_span(MD_SPANTYPE type, void *detail, void *user) {
    Ctx *c = user;
    (void)detail;
    switch (type) {
    case MD_SPAN_EM: c->style &= ~(unsigned)EX_RS_ITALIC; break;
    case MD_SPAN_STRONG: c->style &= ~(unsigned)EX_RS_BOLD; break;
    case MD_SPAN_DEL: c->style &= ~(unsigned)EX_RS_STRIKE; break;
    case MD_SPAN_U: c->style &= ~(unsigned)EX_RS_UNDERLINE; break;
    case MD_SPAN_CODE: c->style &= ~(unsigned)EX_RS_CODE; break;
    case MD_SPAN_WIKILINK:
        if (c->nsp > 0 && c->sp[c->nsp - 1].kind == EX_RSK_LINK_INTERNAL && c->sp[c->nsp - 1].target) {     /* an escaped bar in a table cell: target|label */
            ExRSpan *l = &c->sp[c->nsp - 1];
            char *bar = strchr(l->target, '|');
            if (bar) {
                char *label = dup_n(bar + 1, strlen(bar + 1));
                *bar = 0;
                if (label && label[0]) { free(l->text); l->text = label; } else free(label);
            }
        }
        free(c->link_target); c->link_target = NULL; c->link_kind = EX_RSK_TEXT;
        break;
    case MD_SPAN_A: case MD_SPAN_IMG: free(c->link_target); c->link_target = NULL; c->link_kind = EX_RSK_TEXT; break;
    case MD_SPAN_LATEXMATH: case MD_SPAN_LATEXMATH_DISPLAY: {
        size_t a = 0, e = c->math.n;                                              /* md4c hands the equation over in pieces: one span, trimmed */
        while (a < e && isspace((unsigned char)c->math.p[a])) a++;
        while (e > a && isspace((unsigned char)c->math.p[e - 1])) e--;
        spans_push(c, EX_RSK_MATH, c->style | EX_RS_CODE, c->math.p ? c->math.p + a : "", e - a, c->math_display ? "display" : "inline");
        c->in_math = 0;
        break;
    }
    default: break;
    }
    return 0;
}

/* The inline html tags that change the style. */
static void html_inline(Ctx *c, const char *t, size_t n) {
    size_t i = 1, ns;
    int closing = 0;
    char name[16];
    unsigned bit = 0;
    if (n < 3 || t[0] != '<') return;
    if (t[i] == '/') { closing = 1; i++; }
    ns = i;
    while (i < n && isalnum((unsigned char)t[i]) && i - ns < 15) { name[i - ns] = (char)tolower((unsigned char)t[i]); i++; }
    name[i - ns] = 0;
    if (!strcmp(name, "b") || !strcmp(name, "strong")) bit = EX_RS_BOLD;
    else if (!strcmp(name, "i") || !strcmp(name, "em")) bit = EX_RS_ITALIC;
    else if (!strcmp(name, "u") || !strcmp(name, "ins")) bit = EX_RS_UNDERLINE;
    else if (!strcmp(name, "s") || !strcmp(name, "del")) bit = EX_RS_STRIKE;
    else if (!strcmp(name, "mark")) bit = EX_RS_HIGHLIGHT;
    else if (!strcmp(name, "code") || !strcmp(name, "kbd") || !strcmp(name, "samp")) bit = EX_RS_CODE;
    else if (!strcmp(name, "sub")) bit = EX_RS_SUB;
    else if (!strcmp(name, "sup")) bit = EX_RS_SUP;
    else if (!strcmp(name, "br")) { spans_push(c, EX_RSK_BREAK, 0, "", 0, NULL); return; }
    if (bit) { if (closing) c->style &= ~bit; else c->style |= bit; }
}

static int text_cb(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *user) {
    Ctx *c = user;
    if (c->in_code) {
        if (type == MD_TEXT_NULLCHAR) str_cat(&c->code, "\xEF\xBF\xBD"); else str_put(&c->code, text, size);
        return 0;
    }
    if (c->in_html) { str_put(&c->html, text, size); return 0; }
    if (!c->in_block && !c->in_cell) ensure_leaf(c, EX_RBK_PARAGRAPH);          /* a tight list item has text without a paragraph */
    switch (type) {
    case MD_TEXT_NORMAL: add_text(c, text, size, 0); break;
    case MD_TEXT_NULLCHAR: add_text(c, "\xEF\xBF\xBD", 3, 0); break;
    case MD_TEXT_BR: spans_push(c, EX_RSK_BREAK, 0, "", 0, NULL); break;
    case MD_TEXT_SOFTBR: spans_push(c, EX_RSK_BREAK, 0, "\n", 1, NULL); c->last_char = '\n'; break;
    case MD_TEXT_ENTITY: {
        char *d = ex_html_decode(text, size);
        if (d) { add_text(c, d, strlen(d), 0); free(d); }
        break;
    }
    case MD_TEXT_CODE: spans_push(c, EX_RSK_TEXT, c->style | EX_RS_CODE, text, size, NULL); break;
    case MD_TEXT_HTML: html_inline(c, text, size); break;
    case MD_TEXT_LATEXMATH: str_put(&c->math, text, size); break;
    }
    return 0;
}

/* ---- the whole note ---- */

static ExReading *build(const char *text, size_t len, int depth);

/* The footnotes that were referenced, numbered in order of first reference, as a section at the end. */
static void append_footnotes(Ctx *c, ExReading *r, const Pre *pre, int depth) {
    size_t i, k;
    ExRBlock t;
    if (c->nfn == 0 || depth > 0) return;
    memset(&t, 0, sizeof t);
    t.kind = EX_RBK_FOOTNOTES_TITLE;
    if (push_block(r, &t) != 0) return;
    for (i = 0; i < c->nfn; i++) {
        const char *body = "";
        ExReading *sub;
        char marker[24];
        for (k = 0; k < pre->nfoot; k++) if (strcmp(pre->foot[k].id, c->fn_names[i]) == 0) { body = pre->foot[k].text; break; }
        sub = build(body, strlen(body), depth + 1);
        snprintf(marker, sizeof marker, "%zu.", i + 1);
        if (!sub || sub->n == 0) {
            ExRBlock e;
            memset(&e, 0, sizeof e);
            e.kind = EX_RBK_PARAGRAPH;
            e.marker = dup_n(marker, strlen(marker));
            e.list_depth = 1;
            e.spans = calloc(1, sizeof *e.spans);
            if (e.spans) { e.spans[0].kind = EX_RSK_TEXT; e.spans[0].text = dup_n("(no text)", 9); e.nspans = 1; }
            push_block(r, &e);
        } else {
            for (k = 0; k < sub->n; k++) {
                ExRBlock *b = &sub->b[k];
                b->list_depth += 1;
                if (k == 0) { free(b->marker); b->marker = dup_n(marker, strlen(marker)); }
                push_block(r, b);
            }
            free(sub->b);                                   /* the blocks moved into r */
            free(sub);
            continue;
        }
        ex_reading_free(sub);
    }
}

static ExReading *build(const char *text, size_t len, int depth) {
    ExReading *r = calloc(1, sizeof *r);
    Ctx c;
    Pre pre;
    MD_PARSER parser;
    size_t i;
    if (!r) return NULL;
    memset(&c, 0, sizeof c);
    memset(&pre, 0, sizeof pre);
    preprocess(text, len, &pre);
    c.r = r; c.depth = depth; c.pre = &pre; c.link_kind = EX_RSK_TEXT;
    memset(&parser, 0, sizeof parser);
    parser.abi_version = 0;
    parser.flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS | MD_FLAG_LATEXMATHSPANS | MD_FLAG_WIKILINKS | MD_FLAG_PERMISSIVEURLAUTOLINKS;
    parser.enter_block = enter_block; parser.leave_block = leave_block; parser.enter_span = enter_span; parser.leave_span = leave_span; parser.text = text_cb;
    md_parse(pre.out.p ? pre.out.p : "", (MD_SIZE)pre.out.n, &parser, &c);
    end_leaf(&c);
    append_footnotes(&c, r, &pre, depth);
    free(c.sp); free(c.math.p); free(c.link_target); free(c.pending_marker); free(c.code.p); free(c.code_lang); free(c.html.p); free(c.ref.p); free(c.cells);
    for (i = 0; i < c.nfn; i++) free(c.fn_names[i]);
    free(c.fn_names);
    for (i = 0; i < pre.nfoot; i++) { free(pre.foot[i].id); free(pre.foot[i].text); }
    free(pre.foot); free(pre.out.p);
    return r;
}

ExReading *ex_reading_build(const char *text, size_t len) { return build(text, len, 0); }

/* ---- plain text and the dump ---- */

static void spans_plain(const ExRSpan *sp, size_t n, Str *o) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (sp[i].kind == EX_RSK_BREAK) str_cat(o, "\n");
        else if (sp[i].kind == EX_RSK_FOOTNOTE_REF) { str_cat(o, "["); str_cat(o, sp[i].text); str_cat(o, "]"); }
        else if (sp[i].kind == EX_RSK_EMBED) { str_cat(o, "[embedded: "); str_cat(o, sp[i].text); str_cat(o, "]"); }
        else if (sp[i].kind == EX_RSK_IMAGE) { str_cat(o, "[image: "); str_cat(o, sp[i].text); str_cat(o, "]"); }
        else str_cat(o, sp[i].text ? sp[i].text : "");
    }
}

char *ex_reading_plain_text(const ExReading *r) {
    Str o = {0};
    size_t i, k;
    for (i = 0; i < r->n; i++) {
        const ExRBlock *b = &r->b[i];
        if (b->marker) { str_cat(&o, b->marker); str_cat(&o, " "); }
        switch (b->kind) {
        case EX_RBK_CODE: case EX_RBK_MATH: str_cat(&o, b->code ? b->code : ""); break;
        case EX_RBK_HR: break;
        case EX_RBK_FOOTNOTES_TITLE: str_cat(&o, "Footnotes"); break;
        case EX_RBK_TABLE_ROW:
            for (k = 0; k < b->ncells; k++) { if (k) str_cat(&o, "\t"); spans_plain(b->cells[k].spans, b->cells[k].n, &o); }
            break;
        default: spans_plain(b->spans, b->nspans, &o); break;
        }
        str_cat(&o, "\n");
    }
    if (!o.p) o.p = dup_n("", 0);
    return o.p;
}

static void dump_spans(const ExRSpan *sp, size_t n, Str *o) {
    size_t i;
    for (i = 0; i < n; i++) {
        const ExRSpan *s = &sp[i];
        unsigned st = s->style;
        const char *pre_[8], *post_[8];
        int np = 0, k;
        if (s->kind == EX_RSK_BREAK) { str_cat(o, s->text && s->text[0] ? " " : "\\n"); continue; }
        if (st & EX_RS_BOLD) { pre_[np] = "**"; post_[np++] = "**"; }
        if (st & EX_RS_ITALIC) { pre_[np] = "*"; post_[np++] = "*"; }
        if (st & EX_RS_STRIKE) { pre_[np] = "~~"; post_[np++] = "~~"; }
        if (st & EX_RS_HIGHLIGHT) { pre_[np] = "=="; post_[np++] = "=="; }
        if (st & EX_RS_UNDERLINE) { pre_[np] = "<u>"; post_[np++] = "</u>"; }
        if (st & EX_RS_SUB) { pre_[np] = "<sub>"; post_[np++] = "</sub>"; }
        if (st & EX_RS_SUP && s->kind != EX_RSK_FOOTNOTE_REF) { pre_[np] = "<sup>"; post_[np++] = "</sup>"; }
        if ((st & EX_RS_CODE) && s->kind == EX_RSK_TEXT) { pre_[np] = "`"; post_[np++] = "`"; }
        for (k = 0; k < np; k++) str_cat(o, pre_[k]);
        switch (s->kind) {
        case EX_RSK_LINK_EXTERNAL: str_cat(o, "["); str_cat(o, s->text); str_cat(o, "]("); str_cat(o, s->target ? s->target : ""); str_cat(o, ")"); break;
        case EX_RSK_LINK_INTERNAL: str_cat(o, "[["); str_cat(o, s->target ? s->target : ""); if (s->text && s->target && strcmp(s->text, s->target) != 0) { str_cat(o, "|"); str_cat(o, s->text); } str_cat(o, "]]"); break;
        case EX_RSK_TAG: str_cat(o, "#"); str_cat(o, s->target ? s->target : ""); break;
        case EX_RSK_FOOTNOTE_REF: str_cat(o, "[^"); str_cat(o, s->text); str_cat(o, "]"); break;
        case EX_RSK_IMAGE: str_cat(o, "![img: "); str_cat(o, s->text); str_cat(o, " -> "); str_cat(o, s->target ? s->target : ""); str_cat(o, "]"); break;
        case EX_RSK_EMBED: str_cat(o, "![["); str_cat(o, s->text); str_cat(o, "]]"); break;
        case EX_RSK_MATH: str_cat(o, "$"); str_cat(o, s->text); str_cat(o, "$"); break;
        default: str_cat(o, s->text ? s->text : ""); break;
        }
        for (k = np; k-- > 0;) str_cat(o, post_[k]);
    }
}

char *ex_reading_dump(const ExReading *r) {
    Str o = {0};
    size_t i, k;
    static const char *const names[] = {"note", "abstract", "info", "todo", "tip", "success", "question", "warning", "failure", "danger", "bug", "example", "quote"};
    for (i = 0; i < r->n; i++) {
        const ExRBlock *b = &r->b[i];
        int f;
        char tmp[64];
        for (f = 0; f < b->nframes; f++) {
            if (b->frame_callout[f] >= 0) { snprintf(tmp, sizeof tmp, "[!%s%s]", names[b->frame_callout[f] % 13], b->frame_fold[f] == 1 ? "+" : b->frame_fold[f] == 2 ? "-" : ""); str_cat(&o, tmp); }
            else str_cat(&o, ">");
            str_cat(&o, " ");
        }
        for (f = 0; f < b->list_depth - (b->marker ? 1 : 0); f++) str_cat(&o, "  ");
        if (b->marker) { str_cat(&o, b->marker); if (b->task) str_cat(&o, b->task == 1 ? " [ ]" : b->task == 2 ? " [x]" : " [?]"); str_cat(&o, " "); }
        switch (b->kind) {
        case EX_RBK_HEADING: snprintf(tmp, sizeof tmp, "H%d ", b->level); str_cat(&o, tmp); dump_spans(b->spans, b->nspans, &o); break;
        case EX_RBK_PARAGRAPH: str_cat(&o, "P "); dump_spans(b->spans, b->nspans, &o); break;
        case EX_RBK_CALLOUT_TITLE: str_cat(&o, "TITLE "); dump_spans(b->spans, b->nspans, &o); break;
        case EX_RBK_CODE: { char *c = b->code; str_cat(&o, "CODE"); if (b->lang && b->lang[0]) { str_cat(&o, " "); str_cat(&o, b->lang); } str_cat(&o, ": "); for (; c && *c; c++) { if (*c == '\n') str_cat(&o, "\\n"); else str_put(&o, c, 1); } break; }
        case EX_RBK_MATH: { char *c = b->code; str_cat(&o, "MATH: "); for (; c && *c; c++) { if (*c == '\n') str_cat(&o, "\\n"); else str_put(&o, c, 1); } break; }
        case EX_RBK_HR: str_cat(&o, "HR"); break;
        case EX_RBK_FOOTNOTES_TITLE: str_cat(&o, "FOOTNOTES"); break;
        case EX_RBK_TABLE_ROW:
            str_cat(&o, b->header_row ? "ROW(header)" : "ROW");
            for (k = 0; k < b->ncells; k++) { static const char *al[] = {"", ":l", ":c", ":r"}; str_cat(&o, " |"); str_cat(&o, al[b->cells[k].align & 3]); str_cat(&o, " "); dump_spans(b->cells[k].spans, b->cells[k].n, &o); }
            break;
        }
        str_cat(&o, "\n");
    }
    if (!o.p) o.p = dup_n("", 0);
    return o.p;
}
