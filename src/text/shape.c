#include <stdlib.h>
#include <string.h>

#include <SheenBidi/SheenBidi.h>
#include <hb.h>

#include "exhaust_shape.h"
#include "text_internal.h"

/* ---- small growable arrays ---- */

typedef struct Piece { size_t a, b; ExFont *font; hb_script_t script; int color; } Piece;

typedef struct Out {
    ExGlyph *g; size_t ng, capg;
    ExRun *r; size_t nr, capr;
    float width;
} Out;

static int grow_glyphs(Out *o, size_t extra) {
    if (o->ng + extra > o->capg) {
        size_t nc = o->capg ? o->capg * 2 : 64;
        ExGlyph *ng;
        while (nc < o->ng + extra) nc *= 2;
        ng = realloc(o->g, nc * sizeof *ng);
        if (!ng) return -1;
        o->g = ng;
        o->capg = nc;
    }
    return 0;
}

static ExRun *push_run(Out *o) {
    if (o->nr == o->capr) {
        size_t nc = o->capr ? o->capr * 2 : 8;
        ExRun *nr = realloc(o->r, nc * sizeof *nr);
        if (!nr) return NULL;
        o->r = nr;
        o->capr = nc;
    }
    return &o->r[o->nr++];
}

/* ---- font choice ---- */

static int is_ignorable(uint32_t cp) {
    return cp == 0x200C || cp == 0x200D || (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0x2060 || cp == 0xFEFF ||
           cp == 0x200B || (cp >= 0xE0100 && cp <= 0xE01EF) || (cp >= 0x180B && cp <= 0x180F) ||
           (cp >= 0xE0020 && cp <= 0xE007F);
}

static int emoji_cluster(const uint32_t *cp, size_t a, size_t b) {
    size_t i;
    if (cp[a] >= 0x1F1E6 && cp[a] <= 0x1F1FF) return 1; /* regional indicators: flags */
    if (!ex_is_extended_pictographic(cp[a]) && !(cp[a] >= '0' && cp[a] <= '9') && cp[a] != '#' && cp[a] != '*') return 0;
    for (i = a + 1; i < b; i++) if (cp[i] == 0xFE0E) return 0; /* text presentation asked for */
    for (i = a + 1; i < b; i++) if (cp[i] == 0xFE0F || cp[i] == 0x20E3) return 1; /* VS16, keycap */
    return ex_is_emoji_presentation(cp[a]);
}

static int covers(ExFont *f, const uint32_t *cp, size_t a, size_t b) {
    size_t i;
    for (i = a; i < b; i++)
        if (!is_ignorable(cp[i]) && ex_font_glyph_index(f, cp[i]) == 0) return 0;
    return 1;
}

/* Picks the font for one cluster. *color is set when it is a color font used
 * for an emoji sequence. */
static ExFont *choose_font(ExFontLib *lib, ExFont *primary, const uint32_t *cp, size_t a, size_t b, int *color) {
    ExFontDb *db = ex_fontlib_db(lib);
    unsigned want = ex_font_style_flags(primary);
    uint32_t base = 0;
    size_t i;
    int emoji = emoji_cluster(cp, a, b), nth;
    *color = 0;
    for (i = a; i < b && !base; i++) if (!is_ignorable(cp[i])) base = cp[i];
    if (!base) return primary;

    if (emoji && db) {
        for (nth = 0; nth < 6; nth++) {
            int idx = ex_fontdb_find_codepoint(db, base, want, 1, nth);
            ExFont *f;
            if (idx < 0) break;
            f = ex_fontlib_db_font(lib, idx);
            if (f && ex_font_has_color(f) && ex_font_glyph_index(f, base) != 0) { *color = 1; return f; }
        }
    }
    if (covers(primary, cp, a, b)) return primary;
    if (db) {
        ExFont *partial = NULL;
        for (nth = 0; nth < 8; nth++) {
            int idx = ex_fontdb_find_codepoint(db, base, want, 0, nth);
            ExFont *f;
            if (idx < 0) break;
            f = ex_fontlib_db_font(lib, idx);
            if (!f || ex_font_glyph_index(f, base) == 0) continue;
            if (covers(f, cp, a, b)) { *color = ex_font_has_color(f) && emoji; return f; }
            if (!partial) partial = f;
        }
        if (partial) return partial;
    }
    return primary;
}

/* ---- shaping one piece ---- */

static int shape_piece(Out *o, hb_buffer_t *buf, const uint32_t *cps, size_t ncps, const size_t *off, const Piece *p,
                       int rtl, int level, int px, uint32_t script_tag) {
    unsigned n, i;
    hb_glyph_info_t *info;
    hb_glyph_position_t *pos;
    float scale = (float)px / (float)ex_font_upem(p->font);
    ExRun *run;
    size_t first = o->ng;

    hb_buffer_clear_contents(buf);
    hb_buffer_set_flags(buf, HB_BUFFER_FLAG_REMOVE_DEFAULT_IGNORABLES);
    hb_buffer_set_direction(buf, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_set_script(buf, p->script);
    hb_buffer_add_utf32(buf, cps, (int)ncps, (unsigned)p->a, (int)(p->b - p->a));
    hb_shape(ex_font_hb(p->font), buf, NULL, 0);
    info = hb_buffer_get_glyph_infos(buf, &n);
    pos = hb_buffer_get_glyph_positions(buf, &n);
    if (grow_glyphs(o, n) < 0) return -1;
    run = push_run(o);
    if (!run) return -1;
    for (i = 0; i < n; i++) {
        ExGlyph *g = &o->g[o->ng++];
        g->font = p->font;
        g->glyph = info[i].codepoint;
        g->cluster = (uint32_t)off[info[i].cluster];
        g->advance = (float)pos[i].x_advance * scale;
        g->x_offset = (float)pos[i].x_offset * scale;
        g->y_offset = (float)pos[i].y_offset * scale;
        g->run = (uint32_t)(o->nr - 1);
        o->width += g->advance;
    }
    run->font = p->font;
    run->text_start = off[p->a];
    run->text_end = off[p->b];
    run->first_glyph = first;
    run->glyph_count = n;
    run->level = (uint8_t)level;
    run->script = script_tag;
    run->color = p->color;
    return 0;
}

static uint32_t script_tag(hb_script_t s) { return (uint32_t)s; }

/* ---- main entry ---- */

void ex_shaped_free(ExShaped *s) {
    if (!s) return;
    free(s->glyphs);
    free(s->runs);
    memset(s, 0, sizeof *s);
}

int ex_shape(ExFontLib *lib, ExFont *primary, int px, const char *s, size_t len, ExShaped *out) {
    uint32_t *cps = NULL;
    size_t *off = NULL, n = 0, i;
    unsigned char *cluster_start = NULL;
    hb_script_t *script = NULL;
    hb_buffer_t *buf = NULL;
    hb_unicode_funcs_t *uf = hb_unicode_funcs_get_default();
    Out o;
    int rc = -1;
    SBAlgorithmRef algo = NULL;
    SBCodepointSequence seq;
    size_t para_pos;
    int base_rtl = 0, first_para = 1;

    memset(out, 0, sizeof *out);
    memset(&o, 0, sizeof o);
    if (!lib || !primary || px < 1) return -1;
    if (len == 0) return 0;

    cps = malloc((len + 1) * sizeof *cps);
    off = malloc((len + 2) * sizeof *off);
    if (!cps || !off) goto done;
    for (i = 0; i < len;) {
        off[n] = i;
        cps[n] = ex_utf8_decode(s, len, &i);
        if (cps[n] < 0x20 || (cps[n] >= 0x7F && cps[n] < 0xA0)) cps[n] = 0x200B; /* controls draw nothing */
        n++;
    }
    off[n] = len;

    cluster_start = calloc(n + 1, 1);
    script = malloc((n + 1) * sizeof *script);
    buf = hb_buffer_create();
    if (!cluster_start || !script || !buf) goto done;
    {   /* grapheme cluster starts, as code point indexes */
        size_t b = 0, k = 0;
        while (b < len) {
            size_t nb = ex_grapheme_next(s, len, b);
            while (k < n && off[k] < b) k++;
            cluster_start[k] = 1;
            b = nb;
        }
        cluster_start[n] = 1;
    }
    {   /* scripts: Common and Inherited take the script of what precedes, or follows at the start */
        hb_script_t last = HB_SCRIPT_COMMON;
        for (i = 0; i < n; i++) {
            hb_script_t sc = hb_unicode_script(uf, cps[i]);
            if (sc == HB_SCRIPT_COMMON || sc == HB_SCRIPT_INHERITED || sc == HB_SCRIPT_UNKNOWN) sc = last;
            else last = sc;
            script[i] = sc;
        }
        last = HB_SCRIPT_COMMON;
        for (i = n; i-- > 0;) {
            if (script[i] == HB_SCRIPT_COMMON) script[i] = last; else last = script[i];
        }
    }

    seq.stringEncoding = SBStringEncodingUTF32;
    seq.stringBuffer = cps;
    seq.stringLength = n;
    algo = SBAlgorithmCreate(&seq);
    if (!algo) goto done;

    for (para_pos = 0; para_pos < n;) {
        SBUInteger plen = 0, sep = 0;
        SBParagraphRef para;
        SBLineRef line;
        const SBRun *runs;
        SBUInteger nruns, r;
        SBAlgorithmGetParagraphBoundary(algo, para_pos, n - para_pos, &plen, &sep);
        para = SBAlgorithmCreateParagraph(algo, para_pos, plen + sep, SBLevelDefaultLTR);
        if (!para) goto done;
        if (first_para) base_rtl = (SBParagraphGetBaseLevel(para) & 1) != 0;
        first_para = 0;
        line = SBParagraphCreateLine(para, SBParagraphGetOffset(para), SBParagraphGetLength(para));
        if (!line) { SBParagraphRelease(para); goto done; }
        runs = SBLineGetRunsPtr(line);
        nruns = SBLineGetRunCount(line);
        for (r = 0; r < nruns; r++) {
            size_t ra = runs[r].offset, rb = runs[r].offset + runs[r].length;
            int rtl = (runs[r].level & 1) != 0;
            Piece *pieces = malloc((rb - ra + 1) * sizeof *pieces);
            size_t np = 0, k, c;
            if (!pieces) { SBLineRelease(line); SBParagraphRelease(para); goto done; }
            for (c = ra; c < rb;) {
                size_t ce = c + 1;
                ExFont *f;
                int color = 0;
                while (ce < rb && !cluster_start[ce]) ce++;
                f = choose_font(lib, primary, cps, c, ce, &color);
                if (np > 0 && pieces[np - 1].font == f && pieces[np - 1].script == script[c] && pieces[np - 1].color == color &&
                    !color) {
                    pieces[np - 1].b = ce;
                } else {
                    pieces[np].a = c; pieces[np].b = ce; pieces[np].font = f; pieces[np].script = script[c];
                    pieces[np].color = color;
                    np++;
                }
                c = ce;
            }
            for (k = 0; k < np; k++) {
                const Piece *p = &pieces[rtl ? np - 1 - k : k];
                if (shape_piece(&o, buf, cps, n, off, p, rtl, runs[r].level, px, script_tag(p->script)) < 0) {
                    free(pieces); SBLineRelease(line); SBParagraphRelease(para); goto done;
                }
            }
            free(pieces);
        }
        {
            size_t consumed = SBParagraphGetLength(para);
            para_pos = SBParagraphGetOffset(para) + consumed;
            SBLineRelease(line);
            SBParagraphRelease(para);
            if (consumed == 0) break; /* cannot happen; guards against looping */
        }
    }
    out->glyphs = o.g;  out->nglyphs = o.ng;
    out->runs = o.r;    out->nruns = o.nr;
    out->width = o.width;
    out->base_rtl = base_rtl;
    o.g = NULL; o.r = NULL;
    rc = 0;
done:
    free(o.g);
    free(o.r);
    if (algo) SBAlgorithmRelease(algo);
    if (buf) hb_buffer_destroy(buf);
    free(script);
    free(cluster_start);
    free(cps);
    free(off);
    return rc;
}

/* ---- caret geometry ---- */

typedef struct Span { uint32_t cluster; float x0, x1; int rtl; } Span;

/* One span per cluster in visual order, from the glyph list. */
static size_t make_spans(const ExShaped *s, Span **out) {
    Span *sp = malloc((s->nglyphs + 1) * sizeof *sp);
    size_t i, n = 0, g;
    float pen = 0.0f;
    if (!sp) { *out = NULL; return 0; }
    for (g = 0; g < s->nglyphs; g++) {
        const ExGlyph *gl = &s->glyphs[g];
        int rtl = (s->runs[gl->run].level & 1) != 0;
        float x0 = pen, x1 = pen + gl->advance;
        for (i = n; i-- > 0 && i + 3 > n;)  /* join with a recent span of the same cluster (marks, ligature parts) */
            if (sp[i].cluster == gl->cluster && sp[i].rtl == rtl) {
                if (x0 < sp[i].x0) sp[i].x0 = x0;
                if (x1 > sp[i].x1) sp[i].x1 = x1;
                goto joined;
            }
        sp[n].cluster = gl->cluster; sp[n].x0 = x0; sp[n].x1 = x1; sp[n].rtl = rtl;
        n++;
    joined:
        pen += gl->advance;
    }
    *out = sp;
    return n;
}

float ex_shaped_x_for_offset(const ExShaped *s, size_t offset, size_t text_len) {
    Span *sp;
    size_t n = make_spans(s, &sp), i, best = (size_t)-1;
    float x = 0.0f;
    uint32_t best_cluster = 0;
    int have = 0;
    if (!sp || n == 0) { free(sp); return s->base_rtl ? s->width : 0.0f; }
    for (i = 0; i < n; i++)
        if (sp[i].cluster == offset) { /* leading edge of the cluster that starts here */
            float cand = sp[i].rtl ? sp[i].x1 : sp[i].x0;
            if (!have || (sp[i].rtl ? cand > x : cand < x)) x = cand;
            have = 1;
        }
    if (!have) {
        /* the end of the text, or inside a cluster: trailing edge of the last cluster before it */
        for (i = 0; i < n; i++)
            if (sp[i].cluster < offset || offset >= text_len) {
                if (best == (size_t)-1 || sp[i].cluster >= best_cluster) { best = i; best_cluster = sp[i].cluster; }
            }
        if (best != (size_t)-1) x = sp[best].rtl ? sp[best].x0 : sp[best].x1;
    }
    free(sp);
    return x;
}

size_t ex_shaped_offset_for_x(const ExShaped *s, float x, size_t text_len) {
    Span *sp;
    size_t n = make_spans(s, &sp), i, k, result = 0;
    if (!sp || n == 0) { free(sp); return 0; }
    /* the cluster that x is in, or the nearest one at either end */
    {
        size_t pick = 0;
        float best = 1e30f;
        for (i = 0; i < n; i++) {
            float d = x < sp[i].x0 ? sp[i].x0 - x : (x > sp[i].x1 ? x - sp[i].x1 : 0.0f);
            if (d < best) { best = d; pick = i; }
        }
        {
            /* its end in the text: the smallest cluster start above it */
            size_t start = sp[pick].cluster, end = text_len;
            float mid = (sp[pick].x0 + sp[pick].x1) / 2.0f;
            int second_half = x >= mid;
            for (k = 0; k < n; k++)
                if (sp[k].cluster > start && sp[k].cluster < end) end = sp[k].cluster;
            /* in a left to right cluster the second half is nearer its end; right to left is the reverse */
            result = (sp[pick].rtl ? !second_half : second_half) ? end : start;
        }
    }
    free(sp);
    return result;
}
