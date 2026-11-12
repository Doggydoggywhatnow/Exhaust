/* The reading view: a note drawn as text laid out to the width of its pane. The reading model
 * (exhaust_reading.h) gives the blocks; this module wraps their text, draws it with the theme and
 * follows links and callout folds. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "reading_view.h"

typedef enum OpKind { OP_TEXT, OP_RECT, OP_BOX, OP_CHEVRON, OP_RULE } OpKind;

typedef struct Op {
    OpKind kind;
    float x, y, w, h;
    const char *text; size_t len;
    ExFont *font; float size;
    int fg, bg;               /* color tokens, -1 none */
    int alpha;                /* RECT: 0 means opaque */
    unsigned flags;           /* EX_RS_* for underline and strike */
    int link;                 /* index into links, or -1 */
    int toggle;               /* callout id for a title, or -1 */
    int checked;              /* BOX */
    int open;                 /* CHEVRON */
} Op;

typedef struct Link { ExRSpanKind kind; const char *target; } Link;

struct ExReadingView {
    ExReading *model;
    char *plain;
    float vx, vy, vw, vh, scroll;
    float laid_w, laid_scale, content_h;
    int dirty;
    Op *ops; size_t nops, capops;      /* backgrounds first (in `back`), then foreground */
    Op *back; size_t nback, capback;
    Link *links; size_t nlinks, caplinks;
    unsigned char *closed;             /* per callout id: 1 = folded shut */
    size_t nclosed;
    char **owned; size_t nowned, capowned;   /* texts made while laying out (placeholders), freed with the layout */
    ExReadingLinkFn on_link; void *link_user;
    int press_link, press_toggle;
    float base_size;
};

/* ---- storage helpers ---- */

static void push_op(Op **a, size_t *n, size_t *cap, const Op *o) {
    if (*n == *cap) {
        size_t nc = *cap ? *cap * 2 : 256;
        Op *na = realloc(*a, nc * sizeof **a);
        if (!na) return;
        *a = na; *cap = nc;
    }
    (*a)[(*n)++] = *o;
}

static int add_link(ExReadingView *v, ExRSpanKind kind, const char *target) {
    if (v->nlinks == v->caplinks) {
        size_t nc = v->caplinks ? v->caplinks * 2 : 32;
        Link *nl = realloc(v->links, nc * sizeof *nl);
        if (!nl) return -1;
        v->links = nl; v->caplinks = nc;
    }
    v->links[v->nlinks].kind = kind;
    v->links[v->nlinks].target = target;
    return (int)v->nlinks++;
}

static void reset_closed(ExReadingView *v) {
    size_t i, max_id = 0, nb = ex_reading_block_count(v->model);
    int f;
    for (i = 0; i < nb; i++) {
        const ExRBlock *b = ex_reading_block(v->model, i);
        for (f = 0; f < b->nframes; f++) if (b->frame_callout[f] >= 0 && (size_t)b->frame_id[f] + 1 > max_id) max_id = (size_t)b->frame_id[f] + 1;
    }
    free(v->closed);
    v->closed = calloc(max_id ? max_id : 1, 1);
    v->nclosed = max_id;
    for (i = 0; i < nb; i++) {
        const ExRBlock *b = ex_reading_block(v->model, i);
        for (f = 0; f < b->nframes; f++) if (b->frame_callout[f] >= 0 && b->frame_fold[f] == 2 && v->closed) v->closed[b->frame_id[f]] = 1;
    }
}

ExReadingView *ex_readingview_create(const char *text, size_t len) {
    ExReadingView *v = calloc(1, sizeof *v);
    if (!v) return NULL;
    v->press_link = v->press_toggle = -1;
    v->base_size = 16.0f;
    ex_readingview_set_text(v, text, len);
    return v;
}

void ex_readingview_set_text(ExReadingView *v, const char *text, size_t len) {
    ex_reading_free(v->model);
    free(v->plain);
    v->model = ex_reading_build(text, len);
    v->plain = v->model ? ex_reading_plain_text(v->model) : NULL;
    v->dirty = 1;
    if (v->model) reset_closed(v);
}

void ex_readingview_free(ExReadingView *v) {
    if (!v) return;
    ex_reading_free(v->model);
    { size_t i; for (i = 0; i < v->nowned; i++) free(v->owned[i]); free(v->owned); }
    free(v->plain); free(v->ops); free(v->back); free(v->links); free(v->closed);
    free(v);
}

void ex_readingview_set_link_handler(ExReadingView *v, ExReadingLinkFn fn, void *user) { v->on_link = fn; v->link_user = user; }
void ex_readingview_set_viewport(ExReadingView *v, float x, float y, float w, float h) { v->vx = x; v->vy = y; v->vw = w; v->vh = h; }
float ex_readingview_scroll_y(const ExReadingView *v) { return v->scroll; }
float ex_readingview_content_height(const ExReadingView *v) { return v->content_h; }
char *ex_readingview_plain_text(const ExReadingView *v) { char *p = malloc((v->plain ? strlen(v->plain) : 0) + 1); if (p) strcpy(p, v->plain ? v->plain : ""); return p; }

static void clamp_scroll(ExReadingView *v) {
    float max = v->content_h > v->vh ? v->content_h - v->vh : 0;
    if (v->scroll > max) v->scroll = max;
    if (v->scroll < 0) v->scroll = 0;
}
void ex_readingview_scroll(ExReadingView *v, float dy) { v->scroll += dy; clamp_scroll(v); }
void ex_readingview_set_scroll(ExReadingView *v, float y) { v->scroll = y; clamp_scroll(v); }

void ex_readingview_key(ExReadingView *v, ExKey key) {
    float page = v->vh * 0.9f, line = 40.0f;
    switch (key) {
    case EX_KEY_DOWN: v->scroll += line; break;
    case EX_KEY_UP: v->scroll -= line; break;
    case EX_KEY_PAGE_DOWN: case EX_KEY_SPACE: v->scroll += page; break;
    case EX_KEY_PAGE_UP: v->scroll -= page; break;
    case EX_KEY_HOME: v->scroll = 0; break;
    case EX_KEY_END: v->scroll = v->content_h; break;
    default: return;
    }
    clamp_scroll(v);
}

/* ---- layout ---- */

typedef struct Lay {
    ExReadingView *v;
    ExRenderer *r;
    float base, lh_mul;
    float x_left, x_right;     /* the line's edges for the current block */
    float x, y;                /* the cursor */
    float line_h;
    int line_started;
} Lay;

static ExFont *font_for(Lay *L, unsigned st) {
    int mono = (st & EX_RS_CODE) != 0, bold = (st & EX_RS_BOLD) != 0, italic = (st & EX_RS_ITALIC) != 0;
    ExBuiltinFont w = mono ? (bold ? (italic ? EX_FONT_MONO_BOLD_ITALIC : EX_FONT_MONO_BOLD) : (italic ? EX_FONT_MONO_ITALIC : EX_FONT_MONO_REGULAR))
                           : (bold ? (italic ? EX_FONT_UI_BOLD_ITALIC : EX_FONT_UI_BOLD) : (italic ? EX_FONT_UI_ITALIC : EX_FONT_UI_REGULAR));
    return ex_font_builtin(ex_renderer_fonts(L->r), w);
}

static float line_height(Lay *L, float size) {
    ExFontMetrics m;
    ExFont *f = font_for(L, 0);
    if (ex_text_metrics(L->r, f, size, &m) != 0) return size * 1.5f * ex_renderer_scale(L->r);
    return (float)m.line_height * L->lh_mul;
}

/* Lays out spans between x_left and x_right starting at the cursor; leaves the cursor at the start of the line after the last one. */
static void flow(Lay *L, const ExRSpan *sp, size_t n, unsigned base_style, float size_mul, int base_fg) {
    size_t i;
    float size = L->base * size_mul, lh = line_height(L, size);
    L->line_h = lh;
    L->x = L->x_left;
    for (i = 0; i < n; i++) {
        const ExRSpan *s = &sp[i];
        unsigned st = s->style | base_style;
        int fg = base_fg, bg = -1, link = -1;
        float sz = size;
        const char *t = s->text ? s->text : "";
        char buf[256];
        size_t pos = 0, tl = strlen(t);
        float yoff = 0;
        if (s->kind == EX_RSK_BREAK) {
            if (t[0] == '\n' || !t[0]) {
                if (!t[0]) { L->y += lh; L->x = L->x_left; }          /* a hard break */
                else L->x += size * 0.3f * ex_renderer_scale(L->r);       /* a soft break is a space */
            }
            continue;
        }
        if (s->kind == EX_RSK_LINK_EXTERNAL || s->kind == EX_RSK_LINK_INTERNAL) { fg = EX_COLOR_LINK; st |= EX_RS_UNDERLINE; link = add_link(L->v, s->kind, s->target); }
        else if (s->kind == EX_RSK_TAG) { fg = EX_COLOR_TAG; link = add_link(L->v, s->kind, s->target); }
        else if (s->kind == EX_RSK_FOOTNOTE_REF) { fg = EX_COLOR_LINK; sz = size * 0.75f; yoff = -size * 0.3f * ex_renderer_scale(L->r); link = add_link(L->v, s->kind, s->target); }
        else if (s->kind == EX_RSK_IMAGE) { snprintf(buf, sizeof buf, "[image: %s]", t); t = buf; tl = strlen(buf); fg = EX_COLOR_TEXT_MUTED; bg = EX_COLOR_CODE_BACKGROUND; }
        else if (s->kind == EX_RSK_EMBED) { snprintf(buf, sizeof buf, "[embedded: %s]", t); t = buf; tl = strlen(buf); fg = EX_COLOR_TEXT_MUTED; bg = EX_COLOR_CODE_BACKGROUND; }
        else if (s->kind == EX_RSK_MATH) { fg = EX_COLOR_CODE_TEXT; bg = EX_COLOR_CODE_BACKGROUND; st |= EX_RS_CODE; }
        if (st & EX_RS_CODE && s->kind == EX_RSK_TEXT) { fg = EX_COLOR_CODE_TEXT; bg = EX_COLOR_CODE_BACKGROUND; }
        if (st & EX_RS_SUB) { sz = size * 0.75f; yoff = size * 0.2f * ex_renderer_scale(L->r); }
        if (st & EX_RS_SUP && s->kind != EX_RSK_FOOTNOTE_REF) { sz = size * 0.75f; yoff = -size * 0.3f * ex_renderer_scale(L->r); }
        if (st & EX_RS_HIGHLIGHT) bg = EX_COLOR_HIGHLIGHT;
        /* a wikilink without its own label shows "Page > Heading" */
        if (s->kind == EX_RSK_LINK_INTERNAL && s->target && strcmp(t, s->target) == 0) {
            char *o = malloc(tl * 2 + 8), *w;
            size_t k;
            if (o) {
                w = o;
                for (k = 0; k < tl; k++) {
                    if (t[k] == '#' && t[k + 1] == '^') { memcpy(w, " > ", 3); w += 3; k++; }
                    else if (t[k] == '#') { memcpy(w, " > ", 3); w += 3; }
                    else *w++ = t[k];
                }
                *w = 0;
                snprintf(buf, sizeof buf, "%s", o);
                free(o);
                t = buf; tl = strlen(buf);
            }
        }
        {
            ExFont *font = font_for(L, st);
            while (pos < tl) {
                size_t e = pos, tok_end;
                float w;
                Op op;
                while (e < tl && t[e] != ' ' && t[e] != '\n') e++;                     /* a word */
                tok_end = e;
                while (tok_end < tl && (t[tok_end] == ' ' || t[tok_end] == '\n')) tok_end++;       /* and the spaces after it */
                if (tok_end == pos) tok_end = pos + 1;
                w = ex_text_measure(L->r, font, sz, t + pos, tok_end - pos);
                {
                    float vis = ex_text_measure(L->r, font, sz, t + pos, e - pos);
                    if (L->x + vis > L->x_right && L->x > L->x_left) { L->y += lh; L->x = L->x_left; }
                }
                memset(&op, 0, sizeof op);
                op.kind = OP_TEXT; op.x = L->x; op.y = L->y + yoff; op.w = w; op.h = lh;
                op.text = s->text ? t + pos : t + pos; op.len = tok_end - pos;
                op.font = font; op.size = sz; op.fg = fg; op.bg = bg; op.flags = st; op.link = link; op.toggle = -1;
                if (t == buf) {                                  /* a placeholder text lives in a local buffer: keep a copy that lasts as long as the layout */
                    char *copy = malloc(op.len + 1);
                    if (copy) {
                        memcpy(copy, t + pos, op.len); copy[op.len] = 0; op.text = copy;
                        if (L->v->nowned == L->v->capowned) { size_t nc = L->v->capowned ? L->v->capowned * 2 : 16; char **no = realloc(L->v->owned, nc * sizeof *no); if (no) { L->v->owned = no; L->v->capowned = nc; } }
                        if (L->v->nowned < L->v->capowned) L->v->owned[L->v->nowned++] = copy; else free(copy);
                    }
                }
                push_op(&L->v->ops, &L->v->nops, &L->v->capops, &op);
                L->x += w;
                pos = tok_end;
            }
        }
    }
    L->y += lh;
    L->x = L->x_left;
}

static const ExRBlock *blk(const ExReadingView *v, size_t i) { return ex_reading_block(v->model, i); }

static int hidden_by_fold(const ExReadingView *v, const ExRBlock *b) {
    int f;
    for (f = 0; f < b->nframes; f++) {
        int id = b->frame_id[f];
        if (b->frame_callout[f] >= 0 && id >= 0 && (size_t)id < v->nclosed && v->closed[id] && b->kind != EX_RBK_CALLOUT_TITLE) return 1;
        if (b->frame_callout[f] >= 0 && id >= 0 && (size_t)id < v->nclosed && v->closed[id] && b->kind == EX_RBK_CALLOUT_TITLE && f + 1 < b->nframes) return 0;
    }
    return 0;
}

static void layout(ExReadingView *v, ExRenderer *r, float avail_w) {
    Lay L;
    size_t i, nb = v->model ? ex_reading_block_count(v->model) : 0;
    float scale = ex_renderer_scale(r), pad = 18.0f * scale, col_w, x0, em;
    float readable = 720.0f * scale;
    free(v->ops); v->ops = NULL; v->nops = v->capops = 0;
    free(v->back); v->back = NULL; v->nback = v->capback = 0;
    for (i = 0; i < v->nowned; i++) free(v->owned[i]);
    v->nowned = 0;
    v->nlinks = 0;
    memset(&L, 0, sizeof L);
    L.v = v; L.r = r; L.base = v->base_size; L.lh_mul = 1.35f;
    em = L.base * scale;
    col_w = avail_w - 2 * pad;
    if (col_w > readable) col_w = readable;
    if (col_w < 120) col_w = 120;
    x0 = pad + (avail_w - 2 * pad - col_w) / 2.0f;
    if (x0 < pad) x0 = pad;
    L.y = pad;
    for (i = 0; i < nb; i++) {
        const ExRBlock *b = blk(v, i);
        float indent = (float)b->list_depth * em * 1.6f + (float)b->nframes * em * 1.0f, top = L.y, gap, bar_w = 3.0f * scale;
        int f;
        if (hidden_by_fold(v, b)) continue;
        L.x_left = x0 + indent;
        L.x_right = x0 + col_w;
        gap = b->list_depth > 0 && b->kind != EX_RBK_HEADING ? 0.25f * line_height(&L, L.base) : 0.6f * line_height(&L, L.base);
        L.x = L.x_left;
        switch (b->kind) {
        case EX_RBK_HEADING: {
            static const float sc[] = {1.0f, 1.8f, 1.5f, 1.3f, 1.15f, 1.05f, 1.0f};
            L.y += i > 0 ? 0.5f * line_height(&L, L.base) : 0;
            top = L.y;
            flow(&L, b->spans, b->nspans, EX_RS_BOLD, sc[b->level >= 1 && b->level <= 6 ? b->level : 1], EX_COLOR_TEXT);
            break;
        }
        case EX_RBK_PARAGRAPH: case EX_RBK_CALLOUT_TITLE: {
            float ty = L.y;
            if (b->marker) {                                           /* the bullet, number or box of a list item */
                Op m;
                float mw = em * 1.4f;
                memset(&m, 0, sizeof m);
                m.link = -1; m.toggle = -1;
                if (b->task) { m.kind = OP_BOX; m.checked = b->task != 1; m.x = L.x_left - mw; m.y = ty; m.w = mw; m.h = line_height(&L, L.base); m.fg = EX_COLOR_ACCENT; }
                else { m.kind = OP_TEXT; m.text = b->marker; m.len = strlen(b->marker); m.font = font_for(&L, 0); m.size = L.base; m.fg = EX_COLOR_TEXT_MUTED; m.h = line_height(&L, L.base);
                       m.w = ex_text_measure(r, m.font, m.size, m.text, m.len); m.x = L.x_left - m.w - em * 0.4f; m.y = ty; }
                push_op(&v->ops, &v->nops, &v->capops, &m);
            }
            if (b->kind == EX_RBK_CALLOUT_TITLE) {
                int id = b->nframes > 0 ? b->frame_id[b->nframes - 1] : -1, fold = b->nframes > 0 ? b->frame_fold[b->nframes - 1] : 0;
                int ct = b->nframes > 0 ? b->frame_callout[b->nframes - 1] : 0;
                size_t first = v->nops;
                flow(&L, b->spans, b->nspans, EX_RS_BOLD, 1.0f, EX_COLOR_CALLOUT_NOTE + (ct >= 0 && ct < 13 ? ct : 0));
                if (fold) {
                    Op ch;
                    memset(&ch, 0, sizeof ch);
                    ch.kind = OP_CHEVRON; ch.x = x0 + col_w - em * 1.5f; ch.y = ty; ch.w = em; ch.h = line_height(&L, L.base); ch.fg = EX_COLOR_TEXT_MUTED;
                    ch.open = !(id >= 0 && (size_t)id < v->nclosed && v->closed[id]); ch.toggle = id; ch.link = -1;
                    push_op(&v->ops, &v->nops, &v->capops, &ch);
                    { size_t k; for (k = first; k < v->nops - 1; k++) v->ops[k].toggle = id; }       /* the whole title toggles */
                }
            } else flow(&L, b->spans, b->nspans, 0, 1.0f, EX_COLOR_TEXT);
            break;
        }
        case EX_RBK_CODE: case EX_RBK_MATH: {
            const char *c = b->code ? b->code : "", *p = c;
            float size = L.base * 0.95f, lh = line_height(&L, size), padc = 0.5f * em, y = L.y + padc;
            ExFont *font = ex_font_builtin(ex_renderer_fonts(r), EX_FONT_MONO_REGULAR);
            Op bg;
            size_t lines = 1;
            const char *q;
            for (q = c; *q; q++) if (*q == '\n') lines++;
            memset(&bg, 0, sizeof bg);
            bg.kind = OP_RECT; bg.x = L.x_left; bg.y = L.y; bg.w = x0 + col_w - L.x_left; bg.h = (float)lines * lh + 2 * padc; bg.bg = EX_COLOR_CODE_BACKGROUND; bg.link = -1; bg.toggle = -1;
            push_op(&v->back, &v->nback, &v->capback, &bg);
            for (;;) {
                const char *e = strchr(p, '\n');
                size_t n = e ? (size_t)(e - p) : strlen(p);
                Op op;
                memset(&op, 0, sizeof op);
                op.kind = OP_TEXT; op.x = L.x_left + padc; op.y = y; op.w = x0 + col_w - L.x_left - 2 * padc; op.h = lh; op.text = p; op.len = n; op.font = font; op.size = size;
                op.fg = EX_COLOR_CODE_TEXT; op.link = -1; op.toggle = -1; op.flags = EX_RS_CODE | 0x1000;      /* 0x1000: clip to its width, never wrap */
                push_op(&v->ops, &v->nops, &v->capops, &op);
                y += lh;
                if (!e) break;
                p = e + 1;
            }
            L.y += (float)lines * lh + 2 * padc;
            break;
        }
        case EX_RBK_HR: {
            Op op;
            memset(&op, 0, sizeof op);
            op.kind = OP_RULE; op.x = x0; op.y = L.y + 0.4f * line_height(&L, L.base); op.w = col_w; op.h = 1.5f * scale; op.fg = EX_COLOR_BORDER; op.link = -1; op.toggle = -1;
            push_op(&v->ops, &v->nops, &v->capops, &op);
            L.y += 0.8f * line_height(&L, L.base);
            break;
        }
        case EX_RBK_FOOTNOTES_TITLE: {
            Op op;
            static const ExRSpan title = {EX_RSK_TEXT, EX_RS_BOLD, (char *)"Footnotes", NULL};
            L.y += line_height(&L, L.base);
            memset(&op, 0, sizeof op);
            op.kind = OP_RULE; op.x = x0; op.y = L.y; op.w = col_w; op.h = 1.5f * scale; op.fg = EX_COLOR_BORDER; op.link = -1; op.toggle = -1;
            push_op(&v->ops, &v->nops, &v->capops, &op);
            L.y += 0.4f * line_height(&L, L.base);
            top = L.y;
            flow(&L, &title, 1, EX_RS_BOLD, 1.0f, EX_COLOR_TEXT_MUTED);
            break;
        }
        case EX_RBK_TABLE_ROW: {
            /* the table's rows are laid out together, so the columns line up */
            size_t j = i, rows, k, cols = 0, c;
            float *cw, total = 0, rowh = line_height(&L, L.base) + 0.5f * em, y = L.y;
            while (j < nb && blk(v, j)->kind == EX_RBK_TABLE_ROW && blk(v, j)->nframes == b->nframes) j++;
            rows = j - i;
            for (k = i; k < j; k++) if (blk(v, k)->ncells > cols) cols = blk(v, k)->ncells;
            cw = calloc(cols ? cols : 1, sizeof *cw);
            if (!cw) break;
            for (k = i; k < j; k++) {
                const ExRBlock *rb = blk(v, k);
                for (c = 0; c < rb->ncells; c++) {
                    float wcell = 0;
                    size_t q;
                    for (q = 0; q < rb->cells[c].n; q++) {
                        const char *tx = rb->cells[c].spans[q].text ? rb->cells[c].spans[q].text : "";
                        wcell += ex_text_measure(r, font_for(&L, rb->cells[c].spans[q].style | (rb->header_row ? EX_RS_BOLD : 0)), L.base, tx, strlen(tx));
                    }
                    wcell += em;
                    if (wcell > cw[c]) cw[c] = wcell;
                }
            }
            for (c = 0; c < cols; c++) total += cw[c];
            if (total > x0 + col_w - L.x_left) { float k2 = (x0 + col_w - L.x_left) / total; for (c = 0; c < cols; c++) cw[c] *= k2; total = x0 + col_w - L.x_left; }
            for (k = i; k < j; k++) {
                const ExRBlock *rb = blk(v, k);
                float x = L.x_left;
                Op line;
                memset(&line, 0, sizeof line);
                line.kind = OP_RULE; line.x = L.x_left; line.y = y; line.w = total; line.h = 1.0f * scale; line.fg = EX_COLOR_BORDER; line.link = -1; line.toggle = -1;
                push_op(&v->ops, &v->nops, &v->capops, &line);
                for (c = 0; c < rb->ncells; c++) {
                    size_t q;
                    float xx = x + 0.5f * em / 2.0f;
                    for (q = 0; q < rb->cells[c].n; q++) {
                        const ExRSpan *s = &rb->cells[c].spans[q];
                        Op op;
                        unsigned st = s->style | (rb->header_row ? EX_RS_BOLD : 0);
                        const char *tx = s->text ? s->text : "";
                        float w = ex_text_measure(r, font_for(&L, st), L.base, tx, strlen(tx));
                        if (s->kind == EX_RSK_BREAK) continue;
                        memset(&op, 0, sizeof op);
                        op.kind = OP_TEXT; op.x = xx; op.y = y + 0.25f * em; op.w = w; op.h = rowh; op.text = tx; op.len = strlen(tx); op.font = font_for(&L, st); op.size = L.base;
                        op.fg = (s->kind == EX_RSK_LINK_EXTERNAL || s->kind == EX_RSK_LINK_INTERNAL) ? EX_COLOR_LINK : EX_COLOR_TEXT; op.flags = st | ((s->kind == EX_RSK_LINK_EXTERNAL || s->kind == EX_RSK_LINK_INTERNAL) ? EX_RS_UNDERLINE : 0) | 0x1000;
                        op.link = (s->kind == EX_RSK_LINK_EXTERNAL || s->kind == EX_RSK_LINK_INTERNAL) ? add_link(v, s->kind, s->target) : -1; op.toggle = -1;
                        if (rb->cells[c].align == 2) op.x = x + (cw[c] - w) / 2.0f; else if (rb->cells[c].align == 3) op.x = x + cw[c] - w - 0.25f * em;
                        if (xx + w > x + cw[c]) op.w = x + cw[c] - xx;
                        push_op(&v->ops, &v->nops, &v->capops, &op);
                        xx += w;
                    }
                    x += cw[c];
                }
                y += rowh;
            }
            { Op line; memset(&line, 0, sizeof line); line.kind = OP_RULE; line.x = L.x_left; line.y = y; line.w = total; line.h = 1.0f * scale; line.fg = EX_COLOR_BORDER; line.link = -1; line.toggle = -1; push_op(&v->ops, &v->nops, &v->capops, &line); }
            L.y = y;
            free(cw);
            (void)rows;
            i = j - 1;
            break;
        }
        }
        /* the quote bars and the tint of a callout behind this block */
        for (f = 0; f < b->nframes; f++) {
            Op d;
            float fx = x0 + (float)f * em * 1.0f;
            memset(&d, 0, sizeof d);
            d.link = -1; d.toggle = -1;
            if (b->frame_callout[f] >= 0) {
                d.kind = OP_RECT; d.x = fx; d.y = top - gap / 2.0f; d.w = x0 + col_w - fx; d.h = L.y - top + gap; d.bg = EX_COLOR_CALLOUT_NOTE + (b->frame_callout[f] % 13); d.alpha = 34;
                push_op(&v->back, &v->nback, &v->capback, &d);
            }
            d.kind = OP_RECT; d.x = fx; d.y = top - gap / 2.0f; d.w = bar_w; d.h = L.y - top + gap; d.alpha = 0;
            d.bg = b->frame_callout[f] >= 0 ? EX_COLOR_CALLOUT_NOTE + (b->frame_callout[f] % 13) : EX_COLOR_BORDER;
            push_op(&v->back, &v->nback, &v->capback, &d);
        }
        L.y += gap;
    }
    v->content_h = L.y + pad;
    v->laid_w = avail_w;
    v->laid_scale = scale;
    v->dirty = 0;
    clamp_scroll(v);
}

/* ---- drawing and mouse ---- */

void ex_readingview_draw(ExReadingView *v, ExRenderer *r, const ExTheme *t, float base_size) {
    size_t i;
    float top, bottom;
    if (!v->model || v->vw <= 0) return;
    v->base_size = base_size > 0 ? base_size : 16.0f;
    if (v->dirty || fabsf(v->laid_w - v->vw) > 0.5f || v->laid_scale != ex_renderer_scale(r)) layout(v, r, v->vw);
    top = v->scroll - 100.0f; bottom = v->scroll + v->vh + 100.0f;
    ex_set_clip(r, (int)v->vx, (int)v->vy, (int)v->vw, (int)v->vh);
    for (i = 0; i < v->nback; i++) {
        const Op *o = &v->back[i];
        ExColor c;
        if (o->y > bottom || o->y + o->h < top) continue;
        c = ex_theme_color(t, (ExColorToken)o->bg);
        if (o->alpha) c.a = (uint8_t)o->alpha;
        ex_fill_rect(r, v->vx + o->x, v->vy + o->y - v->scroll, o->w, o->h, c);
    }
    for (i = 0; i < v->nops; i++) {
        const Op *o = &v->ops[i];
        float x = v->vx + o->x, y = v->vy + o->y - v->scroll;
        if (o->y > bottom || o->y + o->h < top) continue;
        switch (o->kind) {
        case OP_TEXT: {
            ExColor col = ex_theme_color(t, (ExColorToken)o->fg);
            ExFontMetrics m;
            float ty = y;
            int clipped = (o->flags & 0x1000) != 0;
            if (o->bg >= 0) ex_fill_rect(r, x, y, o->w, o->h, ex_theme_color(t, (ExColorToken)o->bg));
            if (ex_text_metrics(r, o->font, o->size, &m) == 0) ty = y + (o->h - (float)m.line_height) / 2.0f;
            if (clipped) ex_set_clip(r, (int)x, (int)v->vy, (int)(o->w + 1), (int)v->vh);
            ex_text_draw(r, o->font, o->size, x, ty, o->text, o->len, col);
            if (clipped) ex_set_clip(r, (int)v->vx, (int)v->vy, (int)v->vw, (int)v->vh);
            if (o->flags & EX_RS_UNDERLINE) ex_fill_rect(r, x, y + o->h * 0.88f, o->w, (float)ex_px(r, 1), col);
            if (o->flags & EX_RS_STRIKE) ex_fill_rect(r, x, y + o->h * 0.5f, o->w, (float)ex_px(r, 1), col);
            break;
        }
        case OP_RULE: ex_fill_rect(r, x, y, o->w, o->h, ex_theme_color(t, (ExColorToken)o->fg)); break;
        case OP_BOX: {
            float side = o->h * 0.5f, bx = x + (o->w - side) / 2.0f - o->w * 0.1f, by = y + (o->h - side) / 2.0f, th = (float)ex_px(r, 1.5f);
            ExColor col = ex_theme_color(t, (ExColorToken)o->fg);
            ex_fill_rect(r, bx, by, side, th, col); ex_fill_rect(r, bx, by + side - th, side, th, col);
            ex_fill_rect(r, bx, by, th, side, col); ex_fill_rect(r, bx + side - th, by, th, side, col);
            if (o->checked) ex_fill_rect(r, bx + side * 0.25f, by + side * 0.25f, side * 0.5f, side * 0.5f, col);
            break;
        }
        case OP_CHEVRON: {
            ExColor col = ex_theme_color(t, (ExColorToken)o->fg);
            float s = o->h * 0.28f, cx = x + o->w / 2.0f, cy = y + o->h / 2.0f;
            if (o->open) ex_fill_triangle(r, cx - s, cy - s / 2, cx + s, cy - s / 2, cx, cy + s / 2, col);
            else ex_fill_triangle(r, cx - s / 2, cy - s, cx - s / 2, cy + s, cx + s / 2, cy, col);
            break;
        }
        case OP_RECT: break;
        }
    }
    ex_set_clip(r, 0, 0, -1, -1);
}

static const Op *hit(const ExReadingView *v, float x, float y) {
    size_t i;
    float px = x - v->vx, py = y - v->vy + v->scroll;
    for (i = 0; i < v->nops; i++) {
        const Op *o = &v->ops[i];
        if ((o->link < 0 && o->toggle < 0) || o->kind == OP_RECT) continue;
        if (px >= o->x && px < o->x + o->w && py >= o->y && py < o->y + o->h) return o;
    }
    return NULL;
}

int ex_readingview_mouse(ExReadingView *v, ExMouse kind, float x, float y) {
    const Op *o = hit(v, x, y);
    if (kind == EX_MOUSE_DOWN) {
        v->press_link = o ? o->link : -1;
        v->press_toggle = o ? o->toggle : -1;
        return o != NULL;
    }
    if (kind == EX_MOUSE_UP) {
        int link = v->press_link, tog = v->press_toggle;
        v->press_link = v->press_toggle = -1;
        if (o && o->link >= 0 && o->link == link && v->on_link) v->on_link(v->links[link].kind, v->links[link].target, v->link_user);
        else if (o && o->toggle >= 0 && o->toggle == tog && (size_t)tog < v->nclosed) { v->closed[tog] = !v->closed[tog]; v->dirty = 1; }
    }
    return 1;
}
