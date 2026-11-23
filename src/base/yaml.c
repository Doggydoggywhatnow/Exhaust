/* A YAML tree that keeps everything: see include/exhaust_yaml.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yaml.h>

#include "exhaust_yaml.h"

#define MAX_DEPTH 200

static char *dups(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }
static char *dupn(const char *s, size_t n) { char *p = malloc(n + 1); if (p) { memcpy(p, s, n); p[n] = 0; } return p; }

static ExYNode *node_new(ExYKind k) { ExYNode *n = calloc(1, sizeof *n); if (n) n->kind = k; return n; }

void ex_y_free(ExYNode *n) {
    size_t i;
    if (!n) return;
    for (i = 0; i < n->n; i++) ex_y_free(n->kids[i]);
    free(n->kids); free(n->value); free(n->anchor); free(n->tag);
    free(n);
}

static int add_kid(ExYNode *p, ExYNode *k) {
    if (p->n == p->cap) {
        size_t nc = p->cap ? p->cap * 2 : 4;
        ExYNode **nk = realloc(p->kids, nc * sizeof *nk);
        if (!nk) return -1;
        p->kids = nk; p->cap = nc;
    }
    p->kids[p->n++] = k;
    return 0;
}

ExYNode *ex_y_copy(const ExYNode *n) {
    ExYNode *c;
    size_t i;
    if (!n) return NULL;
    c = node_new(n->kind);
    if (!c) return NULL;
    c->style = n->style;
    if (n->value) c->value = dups(n->value);
    if (n->anchor) c->anchor = dups(n->anchor);
    if (n->tag) c->tag = dups(n->tag);
    for (i = 0; i < n->n; i++) { ExYNode *k = ex_y_copy(n->kids[i]); if (!k || add_kid(c, k)) { ex_y_free(k); ex_y_free(c); return NULL; } }
    return c;
}

/* ---- reading ---- */

ExYNode *ex_y_parse(const char *text, size_t len, char *err, size_t errcap) {
    yaml_parser_t parser;
    yaml_event_t ev;
    ExYNode *root = NULL, *stack[MAX_DEPTH + 2];
    int sp = 0, done = 0, in_doc = 0, fail = 0;
    char msg[200] = "";
    if (err && errcap) err[0] = 0;
    if (!yaml_parser_initialize(&parser)) { if (err && errcap) snprintf(err, errcap, "out of memory"); return NULL; }
    yaml_parser_set_input_string(&parser, (const unsigned char *)text, len);
    while (!done && !fail) {
        ExYNode *n = NULL;
        if (!yaml_parser_parse(&parser, &ev)) {
            snprintf(msg, sizeof msg, "line %d: %s", (int)parser.problem_mark.line + 1, parser.problem ? parser.problem : "not valid YAML");
            fail = 1;
            break;
        }
        switch (ev.type) {
        case YAML_DOCUMENT_START_EVENT: in_doc = 1; break;
        case YAML_DOCUMENT_END_EVENT: if (root) done = 1; break;                    /* only the first document */
        case YAML_STREAM_END_EVENT: done = 1; break;
        case YAML_SCALAR_EVENT:
            if (!in_doc) break;
            n = node_new(EXY_SCALAR);
            if (!n) { fail = 1; snprintf(msg, sizeof msg, "out of memory"); break; }
            n->value = dupn((const char *)ev.data.scalar.value, ev.data.scalar.length);
            n->style = (int)ev.data.scalar.style;
            if (ev.data.scalar.anchor) n->anchor = dups((const char *)ev.data.scalar.anchor);
            if (ev.data.scalar.tag) n->tag = dups((const char *)ev.data.scalar.tag);
            break;
        case YAML_ALIAS_EVENT:
            n = node_new(EXY_ALIAS);
            if (!n) { fail = 1; snprintf(msg, sizeof msg, "out of memory"); break; }
            n->value = dups((const char *)ev.data.alias.anchor);
            break;
        case YAML_SEQUENCE_START_EVENT: case YAML_MAPPING_START_EVENT:
            if (sp >= MAX_DEPTH) { fail = 1; snprintf(msg, sizeof msg, "the document is nested too deeply"); break; }
            n = node_new(ev.type == YAML_SEQUENCE_START_EVENT ? EXY_SEQ : EXY_MAP);
            if (!n) { fail = 1; snprintf(msg, sizeof msg, "out of memory"); break; }
            if (ev.type == YAML_SEQUENCE_START_EVENT) {
                n->style = ev.data.sequence_start.style == YAML_FLOW_SEQUENCE_STYLE;
                if (ev.data.sequence_start.anchor) n->anchor = dups((const char *)ev.data.sequence_start.anchor);
                if (ev.data.sequence_start.tag) n->tag = dups((const char *)ev.data.sequence_start.tag);
            } else {
                n->style = ev.data.mapping_start.style == YAML_FLOW_MAPPING_STYLE;
                if (ev.data.mapping_start.anchor) n->anchor = dups((const char *)ev.data.mapping_start.anchor);
                if (ev.data.mapping_start.tag) n->tag = dups((const char *)ev.data.mapping_start.tag);
            }
            break;
        case YAML_SEQUENCE_END_EVENT: case YAML_MAPPING_END_EVENT:
            if (sp > 0) sp--;
            break;
        default: break;
        }
        if (n && !fail) {
            int container = n->kind == EXY_SEQ || n->kind == EXY_MAP;
            if (sp == 0) {
                if (root) { ex_y_free(n); n = NULL; }                                  /* not the first node of the document: cannot happen, but never leak */
                else { root = n; if (container) stack[sp++] = n; }
            } else {
                if (add_kid(stack[sp - 1], n)) { ex_y_free(n); fail = 1; snprintf(msg, sizeof msg, "out of memory"); }
                else if (container) stack[sp++] = n;
            }
        } else if (n) ex_y_free(n);
        yaml_event_delete(&ev);
    }
    yaml_parser_delete(&parser);
    if (fail) {
        ex_y_free(root);                                                               /* everything built hangs from the root */
        if (err && errcap) snprintf(err, errcap, "%s", msg);
        return NULL;
    }
    if (!root) root = node_new(EXY_MAP);                                              /* an empty file is an empty mapping */
    return root;
}

/* ---- writing ---- */

typedef struct Out { char *p; size_t n, cap; int oom; } Out;
static int write_handler(void *data, unsigned char *buf, size_t size) {
    Out *o = data;
    if (o->oom) return 0;
    if (o->n + size + 1 > o->cap) {
        size_t nc = o->cap ? o->cap * 2 : 512;
        char *np;
        while (nc < o->n + size + 1) nc *= 2;
        np = realloc(o->p, nc);
        if (!np) { o->oom = 1; return 0; }
        o->p = np; o->cap = nc;
    }
    memcpy(o->p + o->n, buf, size);
    o->n += size;
    o->p[o->n] = 0;
    return 1;
}

static int emit(yaml_emitter_t *e, yaml_event_t *ev) { return yaml_emitter_emit(e, ev); }

static int emit_node(yaml_emitter_t *e, const ExYNode *n, int depth) {
    yaml_event_t ev;
    size_t i;
    if (depth > MAX_DEPTH + 5) return 0;
    switch (n->kind) {
    case EXY_ALIAS:
        if (!yaml_alias_event_initialize(&ev, (const yaml_char_t *)n->value)) return 0;
        return emit(e, &ev);
    case EXY_SCALAR: {
        int implicit = n->tag == NULL;                                          /* untagged: the reader finds the type from the text and the quoting */
        if (!yaml_scalar_event_initialize(&ev, (const yaml_char_t *)n->anchor, (const yaml_char_t *)n->tag, (const yaml_char_t *)(n->value ? n->value : ""),
                                          (int)strlen(n->value ? n->value : ""), implicit, implicit, n->style ? (yaml_scalar_style_t)n->style : YAML_PLAIN_SCALAR_STYLE)) return 0;
        return emit(e, &ev);
    }
    case EXY_SEQ:
        if (!yaml_sequence_start_event_initialize(&ev, (const yaml_char_t *)n->anchor, (const yaml_char_t *)n->tag, n->tag == NULL, n->style ? YAML_FLOW_SEQUENCE_STYLE : YAML_BLOCK_SEQUENCE_STYLE)) return 0;
        if (!emit(e, &ev)) return 0;
        for (i = 0; i < n->n; i++) if (!emit_node(e, n->kids[i], depth + 1)) return 0;
        if (!yaml_sequence_end_event_initialize(&ev)) return 0;
        return emit(e, &ev);
    case EXY_MAP:
        if (!yaml_mapping_start_event_initialize(&ev, (const yaml_char_t *)n->anchor, (const yaml_char_t *)n->tag, n->tag == NULL, n->style ? YAML_FLOW_MAPPING_STYLE : YAML_BLOCK_MAPPING_STYLE)) return 0;
        if (!emit(e, &ev)) return 0;
        for (i = 0; i < n->n; i++) if (!emit_node(e, n->kids[i], depth + 1)) return 0;
        if (!yaml_mapping_end_event_initialize(&ev)) return 0;
        return emit(e, &ev);
    }
    return 0;
}

char *ex_y_write(const ExYNode *root, size_t *len) {
    yaml_emitter_t e;
    yaml_event_t ev;
    Out o;
    int ok;
    memset(&o, 0, sizeof o);
    if (!yaml_emitter_initialize(&e)) return NULL;
    yaml_emitter_set_output(&e, write_handler, &o);
    yaml_emitter_set_indent(&e, 2);
    yaml_emitter_set_width(&e, -1);
    yaml_emitter_set_unicode(&e, 1);
    ok = yaml_stream_start_event_initialize(&ev, YAML_UTF8_ENCODING) && emit(&e, &ev);
    ok = ok && yaml_document_start_event_initialize(&ev, NULL, NULL, NULL, 1) && emit(&e, &ev);
    ok = ok && emit_node(&e, root, 0);
    ok = ok && yaml_document_end_event_initialize(&ev, 1) && emit(&e, &ev);
    ok = ok && yaml_stream_end_event_initialize(&ev) && emit(&e, &ev);
    yaml_emitter_delete(&e);
    if (!ok || o.oom) { free(o.p); return NULL; }
    if (!o.p) o.p = dups("");
    if (len) *len = o.n;
    return o.p;
}

/* ---- building and reading ---- */

static int looks_special(const char *s) {
    char *end;
    if (!*s) return 1;
    if (!strcmp(s, "true") || !strcmp(s, "false") || !strcmp(s, "null") || !strcmp(s, "~") || !strcmp(s, "yes") || !strcmp(s, "no") || !strcmp(s, "on") || !strcmp(s, "off")
        || !strcmp(s, "True") || !strcmp(s, "False") || !strcmp(s, "Null") || !strcmp(s, "TRUE") || !strcmp(s, "FALSE") || !strcmp(s, "NULL") || !strcmp(s, "Yes") || !strcmp(s, "No") || !strcmp(s, "YES") || !strcmp(s, "NO")) return 1;
    strtod(s, &end);
    return end != s && !*end;
}

ExYNode *ex_y_scalar_style(const char *text, int style) {
    ExYNode *n = node_new(EXY_SCALAR);
    if (!n) return NULL;
    n->value = dups(text ? text : "");
    n->style = style;
    if (!n->value) { ex_y_free(n); return NULL; }
    return n;
}
ExYNode *ex_y_scalar(const char *text) { return ex_y_scalar_style(text, looks_special(text ? text : "") ? EXY_DOUBLE : EXY_PLAIN); }
ExYNode *ex_y_map(void) { return node_new(EXY_MAP); }
ExYNode *ex_y_seq(void) { return node_new(EXY_SEQ); }
int ex_y_push(ExYNode *seq, ExYNode *item) { if (!seq || seq->kind != EXY_SEQ || !item) return -1; return add_kid(seq, item); }

const char *ex_y_str(const ExYNode *n) { return n && n->kind == EXY_SCALAR ? (n->value ? n->value : "") : NULL; }
int ex_y_is_null(const ExYNode *n) {
    const char *s;
    if (!n) return 1;
    if (n->kind != EXY_SCALAR) return 0;
    s = n->value ? n->value : "";
    if (n->style != EXY_PLAIN && n->style != 0) return 0;
    return !*s || !strcmp(s, "null") || !strcmp(s, "Null") || !strcmp(s, "NULL") || !strcmp(s, "~");
}
size_t ex_y_count(const ExYNode *n) { return !n ? 0 : n->kind == EXY_MAP ? n->n / 2 : n->kind == EXY_SEQ ? n->n : 0; }
const char *ex_y_key(const ExYNode *map, size_t i) { const char *s; return map && map->kind == EXY_MAP && i * 2 < map->n && (s = ex_y_str(map->kids[i * 2])) ? s : ""; }
ExYNode *ex_y_val(const ExYNode *map, size_t i) { return map && map->kind == EXY_MAP && i * 2 + 1 < map->n ? map->kids[i * 2 + 1] : NULL; }
ExYNode *ex_y_item(const ExYNode *seq, size_t i) { return seq && seq->kind == EXY_SEQ && i < seq->n ? seq->kids[i] : NULL; }

ExYNode *ex_y_get(const ExYNode *map, const char *key) {
    size_t i;
    if (!map || map->kind != EXY_MAP) return NULL;
    for (i = 0; i + 1 < map->n; i += 2) { const char *k = ex_y_str(map->kids[i]); if (k && !strcmp(k, key)) return map->kids[i + 1]; }
    return NULL;
}

int ex_y_set(ExYNode *map, const char *key, ExYNode *value) {
    size_t i;
    ExYNode *k;
    if (!map || map->kind != EXY_MAP || !value) return -1;
    for (i = 0; i + 1 < map->n; i += 2) {
        const char *kk = ex_y_str(map->kids[i]);
        if (kk && !strcmp(kk, key)) { ex_y_free(map->kids[i + 1]); map->kids[i + 1] = value; return 0; }
    }
    k = ex_y_scalar(key);
    if (!k) { ex_y_free(value); return -1; }
    if (add_kid(map, k)) { ex_y_free(k); ex_y_free(value); return -1; }
    if (add_kid(map, value)) { map->n--; ex_y_free(k); ex_y_free(value); return -1; }
    return 0;
}

int ex_y_remove(ExYNode *map, const char *key) {
    size_t i;
    if (!map || map->kind != EXY_MAP) return -1;
    for (i = 0; i + 1 < map->n; i += 2) {
        const char *kk = ex_y_str(map->kids[i]);
        if (kk && !strcmp(kk, key)) {
            ex_y_free(map->kids[i]); ex_y_free(map->kids[i + 1]);
            memmove(&map->kids[i], &map->kids[i + 2], (map->n - i - 2) * sizeof *map->kids);
            map->n -= 2;
            return 0;
        }
    }
    return -1;
}
