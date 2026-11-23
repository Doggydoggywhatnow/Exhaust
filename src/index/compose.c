/* The note composer (6e-ii). */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_compose.h"
#include "exhaust_platform.h"
#include "exhaust_templates.h"

/* ---- headings ---- */
int ex_compose_heading_span(const char *t, size_t n, size_t at, size_t *start, size_t *end, char *heading, size_t cap) {
    size_t p = 0, front_end = 0, i0, i1, after;
    size_t cur_start = 0, cur_level = 0, cur_text0 = 0, cur_text1 = 0;
    int have = 0, fence = 0;
    char fence_ch = 0;
    size_t fence_len = 0;
    if (ex_tpl_front(t, n, &i0, &i1, &after)) front_end = after;
    if (at < front_end) return -1;
    p = front_end;
    while (p < n) {
        size_t le = p, i;
        size_t ls = p;
        while (le < n && t[le] != '\n') le++;
        i = p;
        while (i < le && i - p < 4 && t[i] == ' ') i++;
        if (i - p < 4) {
            if (i + 2 < le && (t[i] == '`' || t[i] == '~') && t[i + 1] == t[i] && t[i + 2] == t[i]) {   /* a fence opens or closes */
                size_t k = i;
                while (k < le && t[k] == t[i]) k++;
                if (!fence) { fence = 1; fence_ch = t[i]; fence_len = k - i; }
                else if (t[i] == fence_ch && k - i >= fence_len) fence = 0;
            } else if (!fence && i < le && t[i] == '#') {
                size_t h = i, level;
                while (h < le && t[h] == '#') h++;
                level = h - i;
                if (level <= 6 && (h == le || t[h] == ' ' || t[h] == '\t' || t[h] == '\r')) {
                    size_t x0 = h, x1 = le;
                    while (x0 < x1 && (t[x0] == ' ' || t[x0] == '\t')) x0++;
                    while (x1 > x0 && (t[x1 - 1] == ' ' || t[x1 - 1] == '\t' || t[x1 - 1] == '\r')) x1--;
                    while (x1 > x0 && t[x1 - 1] == '#') { x1--; }
                    while (x1 > x0 && (t[x1 - 1] == ' ' || t[x1 - 1] == '\t')) x1--;
                    if (have && ls > at) {     /* the next heading after the cursor: it ends the section if it is not deeper */
                        if (level <= cur_level) {
                            *start = cur_start; *end = ls;
                            if (heading && cap) { size_t l = cur_text1 - cur_text0; if (l >= cap) l = cap - 1; memcpy(heading, t + cur_text0, l); heading[l] = 0; }
                            return 0;
                        }
                    } else if (ls <= at) {     /* a heading at or before the cursor: the nearest one governs; a shallower or equal one replaces a deeper one only by being later */
                        cur_start = ls; cur_level = level; cur_text0 = x0; cur_text1 = x1; have = 1;
                    }
                }
            }
        }
        p = le < n ? le + 1 : le;
    }
    if (!have) return -1;
    *start = cur_start; *end = n;
    if (heading && cap) { size_t l = cur_text1 - cur_text0; if (l >= cap) l = cap - 1; memcpy(heading, t + cur_text0, l); heading[l] = 0; }
    return 0;
}

void ex_compose_suggest_name(const char *text, size_t len, char *out, size_t cap) {
    size_t i = 0, o = 0;
    if (!out || cap < 2) return;
    while (i < len && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r' || text[i] == '#')) i++;
    for (; i < len && text[i] != '\n' && text[i] != '\r' && o < 60 && o + 4 < cap; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == ' ' && o && out[o - 1] == ' ') continue;
        if (c < 0x20 || c == '#' || c == '^' || c == '[' || c == ']' || c == '|' || c == ':' || c == '/' || c == '\\' || c == '*' || c == '?' || c == '<' || c == '>' || c == '"') { if (o && out[o - 1] != ' ') out[o++] = ' '; continue; }
        out[o++] = (char)c;
    }
    while (o && (out[o - 1] == ' ' || out[o - 1] == '.')) o--;
    if (o == 0) { snprintf(out, cap, "Untitled"); return; }
    out[o] = 0;
}

/* ---- extraction ---- */
int ex_compose_extract(ExVault *v, const char *source_rel, const char *content, size_t content_len, const char *name, const ExComposeConfig *cfg, char **new_rel, char **replacement) {
    char parent[700] = "", fname[400], *made = NULL, *text = NULL, *rep;
    const char *slash = source_rel ? strrchr(source_rel, '/') : NULL;
    size_t xl = 0, rl;
    ExVaultResult r;
    ExTplContext ctx;
    char from_title[300] = "";
    *new_rel = NULL; *replacement = NULL;
    if (!v || !name || !ex_name_valid(name)) return EX_VAULT_INVALID;
    snprintf(fname, sizeof fname, "%s.md", name);
    if (!ex_name_valid(fname)) return EX_VAULT_INVALID;
    if (slash) snprintf(parent, sizeof parent, "%.*s", (int)(slash - source_rel), source_rel);
    if (source_rel) {
        const char *b = slash ? slash + 1 : source_rel;
        size_t bl = strlen(b);
        snprintf(from_title, sizeof from_title, "%.*s", (int)(bl > 3 && !strcmp(b + bl - 3, ".md") ? bl - 3 : bl), b);
    }
    memset(&ctx, 0, sizeof ctx);
    {
        char *cc = malloc(content_len + 1);
        if (!cc) return EX_VAULT_IO;
        memcpy(cc, content, content_len); cc[content_len] = 0;
        ctx.title = name; ctx.content = cc; ctx.from_title = from_title; ctx.new_title = name;
        if (cfg) { ctx.now_ms = cfg->now_ms; ctx.tz_min = cfg->tz_min; ctx.date_format = cfg->date_format; ctx.time_format = cfg->time_format; }
        if (cfg && cfg->template_text && cfg->template_len) {
            int has_content = 0;
            size_t i;
            for (i = 0; i + 9 <= cfg->template_len; i++) if (!strncmp(cfg->template_text + i, "{{content", 9)) { has_content = 1; break; }
            text = ex_tpl_expand(cfg->template_text, cfg->template_len, &ctx, &xl);
            if (text && !has_content) {                       /* a template without {{content}}: the content follows it */
                char *t2 = realloc(text, xl + content_len + 2);
                if (t2) { text = t2; if (xl && text[xl - 1] != '\n') text[xl++] = '\n'; memcpy(text + xl, content, content_len); xl += content_len; text[xl] = 0; }
            }
        } else { text = cc; xl = content_len; cc = NULL; }
        free(cc);
    }
    r = ex_vault_create_note(v, parent, name, &made);
    if (r != EX_VAULT_OK) { free(text); free(made); return (int)r; }
    if (text && xl) {
        char *abs = ex_vault_abs_path(v, made);
        int rc = abs ? ex_platform_write_file_atomic(abs, text, xl) : -1;
        free(abs);
        if (rc) { free(text); free(made); return EX_VAULT_IO; }
    }
    free(text);
    rl = strlen(name) + 8;
    rep = malloc(rl);
    if (!rep) { free(made); return EX_VAULT_IO; }
    if (!cfg || cfg->after_extract == EX_AFTER_LINK) snprintf(rep, rl, "[[%s]]", name);
    else if (cfg->after_extract == EX_AFTER_EMBED) snprintf(rep, rl, "![[%s]]", name);
    else rep[0] = 0;
    *new_rel = made; *replacement = rep;
    return 0;
}

/* ---- merging ---- */
char *ex_compose_merge_text(const char *from, size_t fl, const char *from_title, const char *into, size_t il, const char *into_title, const ExComposeConfig *cfg, size_t *out_len) {
    size_t i0 = 0, i1 = 0, after = 0, bom = 0, body0, k;
    int has_front;
    char *body, *appended = NULL, *tpl = NULL, *res;
    size_t bl, al = 0, tl = 0;
    ExTplContext ctx;
    if (fl >= 3 && (unsigned char)from[0] == 0xEF && (unsigned char)from[1] == 0xBB && (unsigned char)from[2] == 0xBF) bom = 3;
    has_front = ex_tpl_front(from + bom, fl - bom, &i0, &i1, &after);
    body0 = bom + (has_front ? after : 0);
    bl = fl - body0;
    body = malloc(bl + 1);
    if (!body) return NULL;
    memcpy(body, from + body0, bl); body[bl] = 0;
    if (cfg && cfg->template_text && cfg->template_len) {
        memset(&ctx, 0, sizeof ctx);
        ctx.title = into_title; ctx.content = body; ctx.from_title = from_title; ctx.new_title = into_title;
        ctx.now_ms = cfg->now_ms; ctx.tz_min = cfg->tz_min; ctx.date_format = cfg->date_format; ctx.time_format = cfg->time_format;
        appended = ex_tpl_expand(cfg->template_text, cfg->template_len, &ctx, &al);
        free(body);
        body = NULL;
        if (!appended) return NULL;
    } else { appended = body; al = bl; body = NULL; }
    /* the synthetic template: from's properties, then a separator, then the text to add; used literally (no variables in the user's text are filled in) */
    {
        int nl_end = il > 0 && into[il - 1] == '\n';
        size_t cap = (has_front ? i1 - i0 + 16 : 0) + al + 8, o = 0;
        tpl = malloc(cap);
        if (!tpl) { free(appended); return NULL; }
        if (has_front && i1 > i0) {
            memcpy(tpl + o, "---\n", 4); o += 4;
            memcpy(tpl + o, from + bom + i0, i1 - i0); o += i1 - i0;
            if (tpl[o - 1] != '\n') tpl[o++] = '\n';
            memcpy(tpl + o, "---\n", 4); o += 4;
        }
        if (il > 0 && !nl_end) tpl[o++] = '\n';
        if (il > 0) tpl[o++] = '\n';
        memcpy(tpl + o, appended, al); o += al;
        if (al && appended[al - 1] != '\n') tpl[o++] = '\n';
        tl = o;
    }
    free(appended);
    memset(&ctx, 0, sizeof ctx);
    ctx.literal = 1;
    (void)k;
    res = ex_tpl_apply(into, il, tpl, tl, &ctx, il, out_len, NULL);
    free(tpl);
    return res;
}
