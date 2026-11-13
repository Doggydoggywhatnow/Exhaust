/* Takes links, headings, block ids, tasks, tags and line starts out of a note with the Markdown tokenizer. */
#include <stdlib.h>
#include <string.h>

#include "exhaust_extract.h"
#include "exhaust_markdown.h"
#include "extract_internal.h"

#define GROW(arr, n, cap, type) do { if ((n) == (cap)) { size_t nc_ = (cap) ? (cap) * 2 : 8; type *p_ = realloc((arr), nc_ * sizeof(type)); if (!p_) { c->oom = 1; return 1; } (arr) = p_; (cap) = nc_; } } while (0)

static char *dupn(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

uint32_t ex_extract_line_of(const ExExtract *x, size_t offset) {
    size_t lo = 0, hi = x->nlines;
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (x->line_starts[mid] <= offset) lo = mid; else hi = mid;
    }
    return (uint32_t)lo + 1;
}

static char *trim_dup(const char *s, size_t n) {
    while (n && (*s == ' ' || *s == '\t')) { s++; n--; }
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) n--;
    return dupn(s, n);
}

/* %XX decoding (a link destination as written in Markdown). */
static char *url_decode(const char *s, size_t n) {
    char *out = malloc(n + 1);
    size_t i, o = 0;
    if (!out) return NULL;
    for (i = 0; i < n; i++) {
        if (s[i] == '%' && i + 2 < n) {
            int h = s[i + 1], l = s[i + 2], hv = -1, lv = -1;
            hv = (h >= '0' && h <= '9') ? h - '0' : (h >= 'a' && h <= 'f') ? h - 'a' + 10 : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
            lv = (l >= '0' && l <= '9') ? l - '0' : (l >= 'a' && l <= 'f') ? l - 'a' + 10 : (l >= 'A' && l <= 'F') ? l - 'A' + 10 : -1;
            if (hv >= 0 && lv >= 0) { out[o++] = (char)(hv * 16 + lv); i += 2; continue; }
        }
        out[o++] = s[i];
    }
    out[o] = 0;
    return out;
}

static int has_scheme(const char *s, size_t n) {
    size_t i;
    if (n >= 2 && s[0] == '/' && s[1] == '/') return 1;
    if (n == 0 || !((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z'))) return 0;
    for (i = 1; i < n; i++) {
        if (s[i] == ':') return i >= 2;              /* one letter is a Windows drive */
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '+' || s[i] == '-' || s[i] == '.')) return 0;
    }
    return 0;
}

typedef struct Ctx {
    const char *t;
    size_t len;
    ExExtract *x;
    int oom;
    size_t lcap, hcap, bcap, tcap, gcap;
    long cur;                   /* the link whose parts are being read, or -1 */
    size_t cur_start;
    int cur_md;
    size_t tgt_s, tgt_e, head_s, head_e, blk_s, blk_e, alias_s, alias_e;
    int has_t, has_h, has_b, has_a;
    size_t front_end;
} Ctx;

static void flush_link(Ctx *c) {
    ExXLink *l;
    if (c->cur < 0) return;
    l = &c->x->links[c->cur];
    if (!c->cur_md) {
        l->target = c->has_t ? dupn(c->t + c->tgt_s, c->tgt_e - c->tgt_s) : dupn("", 0);
        if (l->target) { size_t n = strlen(l->target); if (n && l->target[n - 1] == '\\') l->target[n - 1] = 0; }   /* [[a\|b]] in a table */
        l->heading = c->has_h ? dupn(c->t + c->head_s, c->head_e - c->head_s) : NULL;
        l->block = c->has_b ? dupn(c->t + c->blk_s, c->blk_e - c->blk_s) : NULL;
        l->alias = c->has_a ? dupn(c->t + c->alias_s, c->alias_e - c->alias_s) : NULL;
    } else {
        /* a Markdown link: the destination decides; alias is the link text */
        const char *d = c->has_t ? c->t + c->tgt_s : "";
        size_t n = c->has_t ? c->tgt_e - c->tgt_s : 0, hash, i;
        char *dec;
        if (n >= 2 && d[0] == '<' && d[n - 1] == '>') { d++; n -= 2; }
        if (has_scheme(d, n)) { l->target = NULL; c->cur = -1; return; }      /* an external link: not a link of the vault */
        for (hash = 0; hash < n && d[hash] != '#'; hash++) {}
        dec = url_decode(d, hash);
        l->target = dec ? dec : dupn("", 0);
        if (l->target) {                                      /* "./a/b.md" -> "a/b.md" */
            char *p = l->target;
            while (p[0] == '.' && p[1] == '/') p += 2;
            if (p != l->target) memmove(l->target, p, strlen(p) + 1);
        }
        if (hash < n) {
            const char *h = d + hash + 1;
            size_t hn = n - hash - 1;
            if (hn && h[0] == '^') l->block = url_decode(h + 1, hn - 1);
            else l->heading = url_decode(h, hn);
        }
        for (i = 0; l->target && l->target[i]; i++) {}
        l->alias = c->has_a ? dupn(c->t + c->alias_s, c->alias_e - c->alias_s) : NULL;
    }
    if (!l->target) c->oom = 1;
    c->cur = -1;
}

static void drop_last_link(Ctx *c) {            /* an external link was seen: remove the half-made entry */
    if (c->x->nlinks) c->x->nlinks--;
    c->cur = -1;
}

static char *heading_text(const char *t, size_t s, size_t e, int *level_out) {
    const char *p = t + s, *end = t + e, *nl;
    (void)level_out;
    nl = memchr(p, '\n', (size_t)(end - p));
    if (*p == '#' || (end - p > 0 && (*p == ' ' || *p == '\t'))) {
        size_t n;
        const char *q = p;
        while (q < end && (*q == ' ' || *q == '\t')) q++;
        while (q < end && *q == '#') q++;
        end = nl ? nl : end;
        n = (size_t)(end - q);
        while (n && (q[n - 1] == ' ' || q[n - 1] == '\t' || q[n - 1] == '\r')) n--;
        { size_t m = n; while (m && q[m - 1] == '#') m--; if (m < n && (m == 0 || q[m - 1] == ' ' || q[m - 1] == '\t')) n = m; }
        return trim_dup(q, n);
    }
    return trim_dup(p, (size_t)((nl ? nl : end) - p));            /* setext: the first line */
}

static int visit(const ExMdSpan *sp, void *user) {
    Ctx *c = user;
    ExExtract *x = c->x;
    if (c->oom) return 1;
    if (c->cur >= 0 && (sp->start >= c->x->links[c->cur].end || sp->parent_start != c->cur_start ||
                         !(sp->parent_kind == EX_MD_WIKILINK || sp->parent_kind == EX_MD_EMBED || sp->parent_kind == EX_MD_LINK || sp->parent_kind == EX_MD_IMAGE)))
        flush_link(c);
    if (c->oom) return 1;
    switch (sp->kind) {
    case EX_MD_WIKILINK: case EX_MD_EMBED: case EX_MD_LINK: case EX_MD_IMAGE: {
        ExXLink *l;
        flush_link(c);
        GROW(x->links, x->nlinks, c->lcap, ExXLink);
        l = &x->links[x->nlinks++];
        memset(l, 0, sizeof *l);
        l->kind = sp->kind == EX_MD_WIKILINK ? EX_LINK_WIKI : sp->kind == EX_MD_EMBED ? EX_LINK_EMBED : sp->kind == EX_MD_LINK ? EX_LINK_MD : EX_LINK_MD_EMBED;
        l->start = sp->start; l->end = sp->end;
        l->line = ex_extract_line_of(x, sp->start);
        c->cur = (long)x->nlinks - 1;
        c->cur_start = sp->start;
        c->cur_md = sp->kind == EX_MD_LINK || sp->kind == EX_MD_IMAGE;
        c->has_t = c->has_h = c->has_b = c->has_a = 0;
        break;
    }
    case EX_MD_LINK_TARGET: case EX_MD_LINK_DEST:
        if (c->cur >= 0 && !c->has_t) { c->has_t = 1; c->tgt_s = sp->start; c->tgt_e = sp->end; }
        break;
    case EX_MD_LINK_HEADING: if (c->cur >= 0) { c->has_h = 1; c->head_s = sp->start; c->head_e = sp->end; } break;
    case EX_MD_LINK_BLOCK: if (c->cur >= 0) { c->has_b = 1; c->blk_s = sp->start; c->blk_e = sp->end; } break;
    case EX_MD_LINK_ALIAS: case EX_MD_LINK_TEXT: if (c->cur >= 0) { c->has_a = 1; c->alias_s = sp->start; c->alias_e = sp->end; } break;
    case EX_MD_HEADING: {
        ExXHeading *h;
        GROW(x->headings, x->nheadings, c->hcap, ExXHeading);
        h = &x->headings[x->nheadings++];
        memset(h, 0, sizeof *h);
        h->level = sp->level;
        h->start = sp->start; h->end = sp->end;
        h->line = ex_extract_line_of(x, sp->start);
        h->text = heading_text(c->t, sp->start, sp->end, NULL);
        if (!h->text) c->oom = 1;
        break;
    }
    case EX_MD_BLOCK_ID: {
        ExXBlock *b;
        size_t s = sp->start + 1;
        GROW(x->blocks, x->nblocks, c->bcap, ExXBlock);
        b = &x->blocks[x->nblocks++];
        memset(b, 0, sizeof *b);
        b->id = dupn(c->t + s, sp->end > s ? sp->end - s : 0);
        b->start = sp->parent_kind ? sp->parent_start : sp->start;
        b->end = sp->parent_kind ? sp->parent_end : sp->end;
        b->line = ex_extract_line_of(x, sp->start);
        if (!b->id) c->oom = 1;
        break;
    }
    case EX_MD_TASK: {
        ExXTask *k;
        size_t ls = sp->start, le = sp->end, ts;
        while (ls > 0 && c->t[ls - 1] != '\n') ls--;
        while (le < c->len && c->t[le] != '\n') le++;
        ts = sp->end;
        while (ts < le && (c->t[ts] == ' ' || c->t[ts] == '\t')) ts++;
        GROW(x->tasks, x->ntasks, c->tcap, ExXTask);
        k = &x->tasks[x->ntasks++];
        memset(k, 0, sizeof *k);
        k->state = sp->flags;
        k->start = ls; k->end = le;
        k->line = ex_extract_line_of(x, sp->start);
        k->text = trim_dup(c->t + ts, le - ts);
        if (!k->text) c->oom = 1;
        break;
    }
    case EX_MD_TAG: {
        ExXTag *g;
        GROW(x->tags, x->ntags, c->gcap, ExXTag);
        g = &x->tags[x->ntags++];
        memset(g, 0, sizeof *g);
        g->tag = dupn(c->t + sp->start + 1, sp->end - sp->start - 1);
        g->start = sp->start;
        g->line = ex_extract_line_of(x, sp->start);
        if (!g->tag) c->oom = 1;
        break;
    }
    case EX_MD_FRONTMATTER: x->front_start = sp->start; x->front_end = sp->end; break;
    default: break;
    }
    (void)drop_last_link;
    return 0;
}

void ex_extract_free(ExExtract *x) {
    size_t i;
    if (!x) return;
    for (i = 0; i < x->nlinks; i++) { free(x->links[i].target); free(x->links[i].heading); free(x->links[i].block); free(x->links[i].alias); }
    for (i = 0; i < x->nheadings; i++) free(x->headings[i].text);
    for (i = 0; i < x->nblocks; i++) free(x->blocks[i].id);
    for (i = 0; i < x->ntasks; i++) free(x->tasks[i].text);
    for (i = 0; i < x->ntags; i++) free(x->tags[i].tag);
    for (i = 0; i < x->nprops; i++) { free(x->props[i].name); free(x->props[i].value); }
    free(x->links); free(x->headings); free(x->blocks); free(x->tasks); free(x->tags); free(x->props); free(x->line_starts);
    free(x);
}

static int cmp_link(const void *a, const void *b) { size_t x = ((const ExXLink *)a)->start, y = ((const ExXLink *)b)->start; return x < y ? -1 : x > y; }
static int cmp_tag(const void *a, const void *b) { size_t x = ((const ExXTag *)a)->start, y = ((const ExXTag *)b)->start; return x < y ? -1 : x > y; }

ExExtract *ex_extract(const char *text, size_t len) {
    ExExtract *x = calloc(1, sizeof *x);
    ExMd *md;
    Ctx c;
    size_t i, cap = 0, k;
    if (!x) return NULL;
    /* line starts */
    x->line_starts = malloc(sizeof(uint32_t) * (len / 8 + 2));
    cap = len / 8 + 2;
    x->line_starts[x->nlines++] = 0;
    for (i = 0; i < len; i++) {
        if (text[i] == '\n') {
            if (x->nlines == cap) { uint32_t *p = realloc(x->line_starts, sizeof(uint32_t) * cap * 2); if (!p) { ex_extract_free(x); return NULL; } x->line_starts = p; cap *= 2; }
            x->line_starts[x->nlines++] = (uint32_t)(i + 1);
        }
    }
    memset(&c, 0, sizeof c);
    c.t = text; c.len = len; c.x = x; c.cur = -1;
    md = ex_md_create();
    if (!md) { ex_extract_free(x); return NULL; }
    ex_md_tokenize(md, text, len);
    ex_md_visit(md, 0, len, visit, &c);
    flush_link(&c);
    ex_md_free(md);
    if (c.oom) { ex_extract_free(x); return NULL; }
    /* drop external Markdown links whose target was set to NULL */
    for (i = 0, k = 0; i < x->nlinks; i++) {
        if (x->links[i].target) x->links[k++] = x->links[i];
        else { free(x->links[i].heading); free(x->links[i].block); free(x->links[i].alias); }
    }
    x->nlinks = k;
    /* the end of every section: the next heading of the same or a higher level */
    for (i = 0; i < x->nheadings; i++) {
        size_t j;
        x->headings[i].section_end = len;
        for (j = i + 1; j < x->nheadings; j++) if (x->headings[j].level <= x->headings[i].level) { x->headings[i].section_end = x->headings[j].start; break; }
    }
    ex_extract_props(x, text, len);
    if (x->nlinks > 1) qsort(x->links, x->nlinks, sizeof *x->links, cmp_link);       /* in the order of the note, properties first */
    if (x->ntags > 1) qsort(x->tags, x->ntags, sizeof *x->tags, cmp_tag);
    return x;
}
