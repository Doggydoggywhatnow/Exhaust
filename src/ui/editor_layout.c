#include <stdlib.h>
#include <string.h>

#include "editor_layout.h"

/* ---- per-byte attributes ---- */

typedef struct B {
    uint16_t f;                  /* LF_* */
    int8_t fg, bg;
    uint8_t scale;               /* index into k_scale */
    uint8_t hide;
    uint8_t subst;               /* 0 none; 1 bullet; 2 arrow; 3 checkbox; 4 checked box; 5 quote bar; 6 callout bar */
    int8_t bar;                  /* the color token of a bar */
} B;

static const float k_scale[] = {1.0f, 1.3f, 1.25f, 1.2f, 1.1f, 1.05f, 1.0f, 0.75f};   /* 0 normal, 1..6 headings, 7 small */
#define SCALE_SMALL 7

typedef struct Ctx {
    const LEnv *env;
    B *b;
    size_t ls, le, len;
    LLine *line;
    size_t alias_end;            /* inside a wikilink with an alias: the end of that wikilink */
    int in_quote;                /* the line is inside a block quote or a callout */
    int callout_level;           /* the callout type of that callout, or -1 */
    size_t nsel;
} Ctx;

static size_t imin(size_t a, size_t b) { return a < b ? a : b; }
static size_t imax(size_t a, size_t b) { return a > b ? a : b; }

/* Is a cursor or selection touching the closed range [a, b]? */
static int touched(const Ctx *c, size_t a, size_t b) {
    size_t i;
    for (i = 0; i < c->nsel; i++) {
        ExSel s = ex_doc_sel(c->env->doc, i);
        size_t lo = imin(s.anchor, s.head), hi = imax(s.anchor, s.head);
        if (lo <= b && hi >= a) return 1;
    }
    return 0;
}

static void put(Ctx *c, size_t from, size_t to, int fg, int bg, unsigned flags) {
    size_t i;
    if (from < c->ls) from = c->ls;
    if (to > c->le) to = c->le;
    for (i = from; i < to; i++) {
        B *x = &c->b[i - c->ls];
        if (fg >= 0) x->fg = (int8_t)fg;
        if (bg >= 0) x->bg = (int8_t)bg;
        x->f |= (uint16_t)flags;
    }
}

static void hide(Ctx *c, size_t from, size_t to, int on) {
    size_t i;
    if (from < c->ls) from = c->ls;
    if (to > c->le) to = c->le;
    for (i = from; i < to; i++) { c->b[i - c->ls].hide = (uint8_t)on; if (!on) c->b[i - c->ls].subst = 0; }
}

static void subst(Ctx *c, size_t from, size_t to, int kind, int bar) {
    size_t i;
    if (from < c->ls) from = c->ls;
    if (to > c->le) to = c->le;
    for (i = from; i < to; i++) { c->b[i - c->ls].hide = 0; c->b[i - c->ls].subst = (uint8_t)kind; c->b[i - c->ls].bar = (int8_t)bar; }
}

static void scale(Ctx *c, size_t from, size_t to, int idx) {
    size_t i;
    if (from < c->ls) from = c->ls;
    if (to > c->le) to = c->le;
    for (i = from; i < to; i++) c->b[i - c->ls].scale = (uint8_t)idx;
}

/* The kinds whose syntax is shown as it is, even in live preview (until their renderers exist). */
static int keeps_syntax(ExMdKind parent) {
    switch (parent) {
    case EX_MD_MATH_INLINE: case EX_MD_MATH_BLOCK: case EX_MD_EMBED: case EX_MD_IMAGE: case EX_MD_TABLE_ROW: case EX_MD_COMMENT_INLINE:
    case EX_MD_COMMENT_BLOCK: case EX_MD_FRONTMATTER: case EX_MD_TABLE: case EX_MD_CODE_INFO: return 1;
    default: return 0;
    }
}

/* Constructs whose syntax follows the line the cursor is on, not the construct's whole range. */
static int line_based(ExMdKind parent) {
    return parent == EX_MD_BLOCKQUOTE || parent == EX_MD_CALLOUT || parent == EX_MD_LIST_ITEM || parent == EX_MD_LIST || parent == EX_MD_FOOTNOTE_DEF;
}

static int is_active(const Ctx *c, const ExMdSpan *sp) {
    if (!c->env->live) return 1;                                  /* source mode shows everything */
    if (line_based(sp->parent_kind)) return touched(c, c->ls, c->le);
    return touched(c, sp->parent_start, sp->parent_end);
}

static int callout_token(unsigned level) { return (int)(EX_COLOR_CALLOUT_NOTE + (level < EX_CALLOUT_COUNT ? level : 0)); }

static int apply(const ExMdSpan *sp, void *user) {
    Ctx *c = user;
    size_t a = sp->start, e = sp->end, i;
    int live = c->env->live;
    if (e <= c->ls && !(e == a && a == c->ls)) return 0;
    if (a > c->le) return 1;
    switch (sp->kind) {
    case EX_MD_HEADING:
        put(c, a, e, EX_COLOR_TEXT, -1, LF_BOLD);
        if (live) scale(c, a, e, sp->level >= 1 && sp->level <= 6 ? sp->level : 0);
        break;
    case EX_MD_STRONG: put(c, a, e, -1, -1, LF_BOLD); break;
    case EX_MD_EMPHASIS: put(c, a, e, -1, -1, LF_ITALIC); break;
    case EX_MD_STRIKE: put(c, a, e, -1, -1, LF_STRIKE); break;
    case EX_MD_HIGHLIGHT: put(c, a, e, -1, EX_COLOR_HIGHLIGHT, 0); break;
    case EX_MD_CODE_SPAN: put(c, a, e, EX_COLOR_CODE_TEXT, EX_COLOR_CODE_BACKGROUND, LF_MONO); break;
    case EX_MD_MATH_INLINE: put(c, a, e, EX_COLOR_CODE_TEXT, EX_COLOR_CODE_BACKGROUND, LF_MONO); break;
    case EX_MD_MATH_BLOCK: put(c, a, e, EX_COLOR_CODE_TEXT, -1, LF_MONO); c->line->code = 1; break;
    case EX_MD_FRONTMATTER: put(c, a, e, EX_COLOR_TEXT_MUTED, -1, LF_MONO); break;
    case EX_MD_COMMENT_BLOCK: case EX_MD_COMMENT_INLINE: put(c, a, e, EX_COLOR_TEXT_FAINT, -1, 0); break;
    case EX_MD_HTML_BLOCK: case EX_MD_HTML_INLINE: put(c, a, e, EX_COLOR_TEXT_MUTED, -1, LF_MONO); break;
    case EX_MD_CODE_BLOCK: c->line->code = 1; break;
    case EX_MD_CODE_BODY: put(c, a, e, EX_COLOR_CODE_TEXT, -1, LF_MONO); break;
    case EX_MD_CODE_INFO: put(c, a, e, EX_COLOR_TEXT_MUTED, -1, 0); break;
    case EX_MD_TAG: put(c, a, e, EX_COLOR_TAG, -1, 0); break;
    case EX_MD_BLOCK_ID: put(c, a, e, EX_COLOR_TEXT_FAINT, -1, 0); break;
    case EX_MD_AUTOLINK: put(c, a, e, EX_COLOR_LINK, -1, LF_UNDERLINE); break;
    case EX_MD_FOOTNOTE_REF: put(c, a, e, EX_COLOR_LINK, -1, 0); if (live) scale(c, a, e, SCALE_SMALL); break;
    case EX_MD_INLINE_FOOTNOTE: put(c, a, e, EX_COLOR_TEXT_MUTED, -1, 0); break;
    case EX_MD_TASK: break;
    case EX_MD_HR: put(c, a, e, EX_COLOR_TEXT_FAINT, -1, 0); break;
    case EX_MD_TABLE_ROW: put(c, a, e, (sp->flags & 2) ? EX_COLOR_TEXT_FAINT : -1, -1, (sp->flags & 1) ? LF_BOLD : 0); break;
    case EX_MD_LINK_TEXT:
        put(c, a, e, EX_COLOR_LINK, -1, LF_UNDERLINE);
        if (live && !touched(c, sp->parent_start, sp->parent_end)) hide(c, a, e, 0);
        break;
    case EX_MD_LINK: case EX_MD_WIKILINK:
        if (live && !touched(c, a, e)) hide(c, a, e, 1);                      /* the parts that are shown are un-hidden below */
        if (sp->kind == EX_MD_WIKILINK) {
            c->alias_end = 0;
            for (i = a > c->ls ? a : c->ls; i < e && i < c->le; i++) {
                size_t k = i - c->ls;
                char ch = c->env->line[k];
                if (ch == '|' || (ch == '\\' && i + 1 < e && c->env->line[k + 1] == '|')) { c->alias_end = e; break; }
            }
        }
        break;
    case EX_MD_LINK_TARGET: case EX_MD_LINK_HEADING: case EX_MD_LINK_BLOCK:
        put(c, a, e, EX_COLOR_LINK, -1, LF_UNDERLINE);
        if (live && c->alias_end == 0 && !touched(c, sp->parent_start, sp->parent_end)) hide(c, a, e, 0);
        break;
    case EX_MD_LINK_ALIAS:
        put(c, a, e, EX_COLOR_LINK, -1, LF_UNDERLINE);
        if (live && !touched(c, sp->parent_start, sp->parent_end)) hide(c, a, e, 0);
        break;
    case EX_MD_LINK_DEST: case EX_MD_LINK_TITLE: put(c, a, e, EX_COLOR_TEXT_MUTED, -1, 0); break;
    case EX_MD_EMBED: case EX_MD_IMAGE: put(c, a, e, EX_COLOR_LINK, -1, 0); break;
    case EX_MD_BLOCKQUOTE: c->in_quote = 1; break;
    case EX_MD_CALLOUT:
        c->line->callout = 1 + (int)sp->level;
        c->in_quote = 1;
        c->callout_level = (int)sp->level;
        break;
    case EX_MD_CALLOUT_TYPE:
        put(c, a, e, callout_token(0), -1, LF_BOLD);                              /* recolored for the real type after the visit */
        if (live && !touched(c, c->ls, c->le)) {                                  /* with a title, the title stands in for [!type] */
            size_t k = e - c->ls + 1;
            while (k < c->len && (c->env->line[k] == '+' || c->env->line[k] == '-' || c->env->line[k] == ' ' || c->env->line[k] == '\t')) k++;
            if (k < c->len) hide(c, a, e, 1);
        }
        break;
    case EX_MD_CALLOUT_TITLE: put(c, a, e, -1, -1, LF_BOLD); break;
    case EX_MD_ESCAPE:
        if (live && !touched(c, a, e)) hide(c, a, a + 1, 1);
        break;
    case EX_MD_HARD_BREAK:
        if (live && !touched(c, a, e)) hide(c, a, e, 1);
        break;
    case EX_MD_LIST_ITEM:
        break;
    case EX_MD_MARKER: {
        ExMdKind pk = sp->parent_kind;
        if (!live) { put(c, a, e, EX_COLOR_TEXT_FAINT, -1, 0); break; }
        if (keeps_syntax(pk)) { put(c, a, e, EX_COLOR_TEXT_FAINT, -1, 0); break; }
        if (c->in_quote && e - a == 1 && c->env->line[a - c->ls] == '>' && pk != EX_MD_BLOCKQUOTE && pk != EX_MD_CALLOUT &&
            (pk == EX_MD_PARAGRAPH || pk == EX_MD_LIST_ITEM || pk == EX_MD_LIST || pk == EX_MD_FOOTNOTE_DEF || pk == EX_MD_HEADING || pk == EX_MD_CODE_BLOCK || pk == EX_MD_CODE_BODY || pk == EX_MD_TABLE_CELL || pk == EX_MD_TABLE || pk == 0)) {
            /* a > of a quote's later line sits inside that line's paragraph (or list or code): it is still the quote's marker */
            if (touched(c, c->ls, c->le)) put(c, a, e, EX_COLOR_TEXT_FAINT, -1, 0);
            else subst(c, a, e, c->callout_level >= 0 ? 6 : 5, c->callout_level >= 0 ? callout_token((unsigned)c->callout_level) : EX_COLOR_BORDER);
            break;
        }
        if (is_active(c, sp)) { put(c, a, e, EX_COLOR_TEXT_FAINT, -1, 0); break; }
        /* the construct is not under the cursor: the syntax goes, or becomes a drawn stand-in */
        switch (pk) {
        case EX_MD_LIST_ITEM:
            if (e - a != 1) put(c, a, e, EX_COLOR_TEXT_MUTED, -1, 0);                      /* an ordered marker stays, quieter */
            else {
                size_t k = a - c->ls;
                char ch = c->env->line[k];
                if (ch == '-' || ch == '*' || ch == '+') {
                    int task = k + 4 < c->len && c->env->line[k + 1] == ' ' && c->env->line[k + 2] == '[' && c->env->line[k + 4] == ']';
                    if (task) hide(c, a, e + 1, 1); else subst(c, a, e, 1, -1);   /* a task shows its box, not a bullet too */
                } else put(c, a, e, EX_COLOR_TEXT_MUTED, -1, 0);
            }
            break;
        case EX_MD_TASK: subst(c, a, e, 3, -1); break;
        case EX_MD_BLOCKQUOTE: subst(c, a, e, 5, EX_COLOR_BORDER); break;
        case EX_MD_CALLOUT: {
            if (e - a == 1 && c->env->line[a - c->ls] == '>') subst(c, a, e, 6, callout_token(c->callout_level >= 0 ? (unsigned)c->callout_level : 0));
            else {
                char last = c->env->line[e - 1 - c->ls];
                size_t k = e;
                hide(c, a, e, 1);
                if (last == ']' || last == '+' || last == '-') {                      /* the space before the title goes with it */
                    while (k < c->le && (c->env->line[k - c->ls] == ' ' || c->env->line[k - c->ls] == '\t')) k++;
                    hide(c, e, k, 1);
                }
            }
            break;
        }
        case EX_MD_WIKILINK:
            if (e - a == 1 && c->env->line[a - c->ls] == '#' && c->alias_end == 0) subst(c, a, e, 2, -1);
            else hide(c, a, e, 1);
            break;
        case EX_MD_HR: hide(c, a, e, 1); c->line->rule = 1; break;
        default: hide(c, a, e, 1); break;
        }
        break;
    }
    default: break;
    }
    return 0;
}

/* The checkbox state and the callout color need the parent: fix them up after the visit. */
typedef struct Fix { Ctx *c; } Fix;

static int fix_cb(const ExMdSpan *sp, void *user) {
    Ctx *c = ((Fix *)user)->c;
    size_t a = sp->start, e = sp->end, i;
    if (e <= c->ls || a >= c->le + 1) return 0;
    if (sp->kind == EX_MD_TASK && c->env->live && !touched(c, c->ls, c->le)) {
        for (i = a; i < e && i < c->le; i++) if (i >= c->ls && c->b[i - c->ls].subst == 3) c->b[i - c->ls].subst = (uint8_t)(sp->flags == 1 ? 4 : 3);
    }
    if (sp->kind == EX_MD_CALLOUT) {
        for (i = a; i < e && i < c->le; i++) {
            if (i < c->ls) continue;
            if (c->b[i - c->ls].subst == 6) c->b[i - c->ls].bar = (int8_t)callout_token(sp->level);
            if (c->b[i - c->ls].f & LF_BOLD && c->b[i - c->ls].fg == EX_COLOR_CALLOUT_NOTE) c->b[i - c->ls].fg = (int8_t)callout_token(sp->level);
        }
        c->line->callout = 1 + (int)sp->level;
    }
    return 0;
}

/* ---- runs ---- */

static ExFont *pick_font(ExRenderer *r, unsigned f) {
    int mono = (f & LF_MONO) != 0, bold = (f & LF_BOLD) != 0, italic = (f & LF_ITALIC) != 0;
    ExBuiltinFont which = mono ? (bold ? (italic ? EX_FONT_MONO_BOLD_ITALIC : EX_FONT_MONO_BOLD) : (italic ? EX_FONT_MONO_ITALIC : EX_FONT_MONO_REGULAR))
                               : (bold ? (italic ? EX_FONT_UI_BOLD_ITALIC : EX_FONT_UI_BOLD) : (italic ? EX_FONT_UI_ITALIC : EX_FONT_UI_REGULAR));
    return ex_font_builtin(ex_renderer_fonts(r), which);
}

static int same(const B *x, const B *y) { return memcmp(x, y, sizeof *x) == 0; }

static int add_run(LLine *l, size_t *cap, const LRun *r) {
    if (l->n == *cap) {
        size_t nc = *cap ? *cap * 2 : 16;
        LRun *nr = realloc(l->runs, nc * sizeof *nr);
        if (!nr) return -1;
        l->runs = nr;
        *cap = nc;
    }
    l->runs[l->n++] = *r;
    return 0;
}

LLine *ll_build(const LEnv *env) {
    LLine *l = calloc(1, sizeof *l);
    Ctx c;
    size_t len = env->le - env->ls, i, cap = 0, x_cur_i;
    float x = 0;
    if (!l) return NULL;
    memset(&c, 0, sizeof c);
    c.env = env; c.ls = env->ls; c.le = env->le; c.len = len; c.line = l;
    c.nsel = env->doc ? ex_doc_sel_count(env->doc) : 0;
    c.callout_level = -1;
    c.b = calloc(len + 1, sizeof *c.b);
    l->text = malloc(len + 1);
    if (!c.b || !l->text) { free(c.b); free(l->text); free(l); return NULL; }
    memcpy(l->text, env->line, len);
    l->text[len] = 0;
    for (i = 0; i < len; i++) { c.b[i].fg = -1; c.b[i].bg = -1; }
    if (env->md) {
        Fix fx;
        ex_md_visit(env->md, env->ls, env->le, apply, &c);
        fx.c = &c;
        ex_md_visit(env->md, env->ls, env->le, fix_cb, &fx);
    }
    /* group the bytes into runs */
    x_cur_i = 0;
    while (x_cur_i < len) {
        size_t j = x_cur_i + 1;
        LRun r;
        const B *b = &c.b[x_cur_i];
        while (j < len && same(&c.b[j], b)) j++;
        memset(&r, 0, sizeof r);
        r.s = env->ls + x_cur_i; r.e = env->ls + j;
        r.size = env->size * k_scale[b->scale];
        r.fg = b->fg >= 0 ? b->fg : EX_COLOR_TEXT;
        r.bg = b->bg;
        r.flags = b->f;
        r.font = pick_font(env->r, b->f);
        if (b->hide) { r.kind = LRUN_HIDDEN; r.w = 0; }
        else if (b->subst == 1) { r.kind = LRUN_SUBST; r.text = "\xE2\x80\xA2"; r.tlen = 3; r.fg = EX_COLOR_TEXT_MUTED; }
        else if (b->subst == 2) { r.kind = LRUN_SUBST; r.text = " > "; r.tlen = 3; r.fg = EX_COLOR_TEXT_MUTED; }
        else if (b->subst == 3 || b->subst == 4) { r.kind = LRUN_CHECKBOX; r.checked = b->subst == 4; r.fg = EX_COLOR_ACCENT; r.w = env->size * 1.4f * ex_renderer_scale(env->r); }
        else if (b->subst == 5 || b->subst == 6) { r.kind = LRUN_BAR; r.fg = b->bar >= 0 ? b->bar : EX_COLOR_BORDER; r.w = env->size * 0.9f * ex_renderer_scale(env->r); l->quote_depth++; }
        else { r.kind = LRUN_TEXT; r.text = l->text + x_cur_i; r.tlen = j - x_cur_i; }
        if (r.kind == LRUN_TEXT || r.kind == LRUN_SUBST) {
            if (ex_shape(ex_renderer_fonts(env->r), r.font, ex_px(env->r, r.size), r.text, r.tlen, &r.sh) >= 0) { r.shaped = 1; r.w = r.sh.width; }
            else { memset(&r.sh, 0, sizeof r.sh); r.w = 0; }
        }
        r.x = x;
        x += r.w;
        if (add_run(l, &cap, &r) != 0) break;
        x_cur_i = j;
    }
    l->width = x;
    free(c.b);
    return l;
}

void ll_free(LLine *l) {
    size_t i;
    if (!l) return;
    for (i = 0; i < l->n; i++) if (l->runs[i].shaped) ex_shaped_free(&l->runs[i].sh);
    free(l->runs);
    free(l->text);
    free(l);
}

float ll_x_of(const LLine *l, size_t off) {
    size_t i;
    float x = 0;
    for (i = 0; i < l->n; i++) {
        const LRun *r = &l->runs[i];
        if (off >= r->e) { x = r->x + r->w; continue; }
        if (off <= r->s) return r->x;
        if (r->kind == LRUN_TEXT && r->shaped) return r->x + ex_shaped_x_for_offset(&r->sh, off - r->s, r->tlen);
        return (off - r->s) * 2 < (r->e - r->s) ? r->x : r->x + r->w;        /* inside a stand-in: its nearer edge */
    }
    return x;
}

size_t ll_offset_at(const LLine *l, float x, size_t ls, size_t le) {
    size_t i;
    for (i = 0; i < l->n; i++) {
        const LRun *r = &l->runs[i];
        if (r->w <= 0 && r->kind == LRUN_HIDDEN) continue;
        if (x < r->x + r->w || i + 1 == l->n) {
            if (r->kind == LRUN_TEXT && r->shaped) {
                size_t off = ex_shaped_offset_for_x(&r->sh, x - r->x, r->tlen);
                return r->s + off;
            }
            return x - r->x < r->w / 2 ? r->s : r->e;
        }
    }
    (void)ls;
    return le;
}

size_t ll_display(const LLine *l, char *out, size_t cap) {
    size_t i, n = 0;
    for (i = 0; i < l->n; i++) {
        const LRun *r = &l->runs[i];
        const char *t = NULL;
        size_t tl = 0;
        if (r->kind == LRUN_TEXT || r->kind == LRUN_SUBST) { t = r->text; tl = r->tlen; }
        else if (r->kind == LRUN_CHECKBOX) { t = r->checked ? "\xE2\x98\x91" : "\xE2\x98\x90"; tl = 3; }
        else if (r->kind == LRUN_BAR) { t = "\xE2\x94\x82"; tl = 3; }
        if (t && n + tl < cap) { memcpy(out + n, t, tl); n += tl; }
    }
    if (cap) out[n < cap ? n : cap - 1] = 0;
    return n;
}
