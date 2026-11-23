/* The Bases expression language: the lexer, the parser, the evaluator and the functions. See include/exhaust_base_expr.h and docs/formats.md. */
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pcre2.h"

#include "exhaust_base_expr.h"
#include "exhaust_datefmt.h"

#define MAX_DEPTH 120            /* nesting of parentheses, lists and calls in an expression; and of evaluation */
#define MAX_STEPS 3000000L       /* evaluation steps of one expression: a map inside a map over big lists cannot run for long */
#define MAX_STRING (1u << 20)    /* the longest string repeat() or replace() makes */
#define MAX_LIST 2000000u

/* ---- the syntax tree ---- */

typedef enum NType { N_NUM, N_STR, N_BOOL, N_NULL, N_REGEX, N_LIST, N_IDENT, N_MEMBER, N_INDEX, N_CALL, N_UNARY, N_BINARY, N_ERROR } NType;
enum { OP_ADD = 1, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE, OP_AND, OP_OR, OP_NOT, OP_NEG };

typedef struct Node {
    NType t;
    int op;
    double num;
    char *s, *flags;
    struct Node *a, *b;
    struct Node **args;
    size_t nargs;
} Node;

struct ExExpr { Node *root; Node **all; size_t n, cap; };

static Node *node_new(ExExpr *e, NType t) {
    Node *n = calloc(1, sizeof *n);
    if (!n) return NULL;
    if (e->n == e->cap) { size_t nc = e->cap ? e->cap * 2 : 32; Node **na = realloc(e->all, nc * sizeof *na); if (!na) { free(n); return NULL; } e->all = na; e->cap = nc; }
    e->all[e->n++] = n;
    n->t = t;
    return n;
}

void ex_expr_free(ExExpr *e) {
    size_t i;
    if (!e) return;
    for (i = 0; i < e->n; i++) { free(e->all[i]->s); free(e->all[i]->flags); free(e->all[i]->args); free(e->all[i]); }
    free(e->all);
    free(e);
}

/* ---- the lexer ---- */

typedef enum TokType { T_EOF, T_NUM, T_STR, T_IDENT, T_REGEX, T_OP, T_LP, T_RP, T_LB, T_RB, T_COMMA, T_DOT } TokType;
typedef struct Tok { TokType t; int op; double num; char *s; char *flags; int pos; } Tok;

typedef struct Parser {
    const char *src;
    size_t len, pos;
    Tok tok;
    int prev_operand;          /* the token before the current one ends an operand: a / is a division */
    int depth;
    char err[160];
    int err_pos;
    ExExpr *e;
} Parser;

static void perr(Parser *p, size_t pos, const char *fmt, ...) {
    va_list ap;
    if (p->err[0]) return;
    va_start(ap, fmt);
    vsnprintf(p->err, sizeof p->err, fmt, ap);
    va_end(ap);
    p->err_pos = (int)pos;
}

static int ident_start(unsigned char c) { return isalpha(c) || c == '_' || c == '$' || c >= 0x80; }
static int ident_char(unsigned char c) { return isalnum(c) || c == '_' || c == '$' || c >= 0x80; }

static void tok_free(Tok *t) { free(t->s); free(t->flags); t->s = t->flags = NULL; }

static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

static size_t utf8_put(char *out, unsigned cp) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) { out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
    out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static void lex(Parser *p) {
    const char *s = p->src;
    size_t i = p->pos;
    Tok *t = &p->tok;
    tok_free(t);
    memset(t, 0, sizeof *t);
    while (i < p->len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    t->pos = (int)i;
    if (i >= p->len) { t->t = T_EOF; p->pos = i; return; }
    {
        unsigned char c = (unsigned char)s[i];
        if (isdigit(c) || (c == '.' && i + 1 < p->len && isdigit((unsigned char)s[i + 1]))) {
            size_t j = i;
            char *end;
            char buf[80];
            while (j < p->len && (isdigit((unsigned char)s[j]) || s[j] == '.')) j++;
            if (j < p->len && (s[j] == 'e' || s[j] == 'E')) {
                size_t k = j + 1;
                if (k < p->len && (s[k] == '+' || s[k] == '-')) k++;
                if (k < p->len && isdigit((unsigned char)s[k])) { while (k < p->len && isdigit((unsigned char)s[k])) k++; j = k; }
            }
            if (j - i >= sizeof buf) { perr(p, i, "the number is too long"); t->t = T_EOF; p->pos = p->len; return; }
            memcpy(buf, s + i, j - i); buf[j - i] = 0;
            t->num = strtod(buf, &end);
            if (*end) { perr(p, i, "not a number: %s", buf); t->t = T_EOF; p->pos = p->len; return; }
            t->t = T_NUM; p->pos = j;
            return;
        }
        if (c == '"' || c == '\'') {
            size_t j = i + 1, o = 0;
            char *buf = malloc(p->len - i + 8);
            if (!buf) { perr(p, i, "out of memory"); t->t = T_EOF; p->pos = p->len; return; }
            while (j < p->len && s[j] != (char)c) {
                if (s[j] == '\\' && j + 1 < p->len) {
                    char e = s[j + 1];
                    j += 2;
                    switch (e) {
                    case 'n': buf[o++] = '\n'; break; case 't': buf[o++] = '\t'; break; case 'r': buf[o++] = '\r'; break;
                    case 'b': buf[o++] = '\b'; break; case 'f': buf[o++] = '\f'; break; case '0': buf[o++] = 0; break;
                    case 'u': {
                        unsigned cp = 0;
                        int k, ok = 1;
                        for (k = 0; k < 4; k++) { int h = j + (size_t)k < p->len ? hexv(s[j + (size_t)k]) : -1; if (h < 0) { ok = 0; break; } cp = cp * 16 + (unsigned)h; }
                        if (ok) {
                            j += 4;
                            if (cp >= 0xD800 && cp < 0xDC00 && j + 5 < p->len && s[j] == '\\' && s[j + 1] == 'u') {              /* a surrogate pair */
                                unsigned lo = 0;
                                int k2, ok2 = 1;
                                for (k2 = 0; k2 < 4; k2++) { int h = hexv(s[j + 2 + (size_t)k2]); if (h < 0) { ok2 = 0; break; } lo = lo * 16 + (unsigned)h; }
                                if (ok2 && lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); j += 6; }
                            }
                            if (cp >= 0xD800 && cp < 0xE000) cp = 0xFFFD;
                            o += utf8_put(buf + o, cp);
                        } else buf[o++] = 'u';
                        break;
                    }
                    default: buf[o++] = e;
                    }
                } else buf[o++] = s[j++];
            }
            if (j >= p->len) { free(buf); perr(p, i, "the text is not closed"); t->t = T_EOF; p->pos = p->len; return; }
            buf[o] = 0;
            t->t = T_STR; t->s = buf; p->pos = j + 1;
            return;
        }
        if (ident_start(c)) {
            size_t j = i + 1;
            for (;;) {
                while (j < p->len && ident_char((unsigned char)s[j])) j++;
                if (j + 1 < p->len && s[j] == '-' && ident_start((unsigned char)s[j + 1]) ) j++;    /* image-property is one name; a - b and a-5 are subtractions */
                else break;
            }
            t->s = malloc(j - i + 1);
            if (!t->s) { perr(p, i, "out of memory"); t->t = T_EOF; p->pos = p->len; return; }
            memcpy(t->s, s + i, j - i); t->s[j - i] = 0;
            t->t = T_IDENT; p->pos = j;
            return;
        }
        if (c == '/' && !p->prev_operand) {                                       /* a regular expression */
            size_t j = i + 1, o = 0;
            int in_class = 0;
            char *buf = malloc(p->len - i + 4);
            if (!buf) { perr(p, i, "out of memory"); t->t = T_EOF; p->pos = p->len; return; }
            while (j < p->len && (s[j] != '/' || in_class)) {
                if (s[j] == '\\' && j + 1 < p->len) { buf[o++] = s[j]; buf[o++] = s[j + 1]; j += 2; continue; }
                if (s[j] == '[') in_class = 1; else if (s[j] == ']') in_class = 0;
                if (s[j] == '\n') break;
                buf[o++] = s[j++];
            }
            if (j >= p->len || s[j] != '/') { free(buf); perr(p, i, "the regular expression is not closed"); t->t = T_EOF; p->pos = p->len; return; }
            buf[o] = 0;
            j++;
            {
                size_t f = j;
                while (f < p->len && isalpha((unsigned char)s[f])) f++;
                t->flags = malloc(f - j + 1);
                if (t->flags) { memcpy(t->flags, s + j, f - j); t->flags[f - j] = 0; }
                p->pos = f;
            }
            t->t = T_REGEX; t->s = buf;
            return;
        }
        p->pos = i + 1;
        switch (c) {
        case '(': t->t = T_LP; return; case ')': t->t = T_RP; return; case '[': t->t = T_LB; return; case ']': t->t = T_RB; return;
        case ',': t->t = T_COMMA; return; case '.': t->t = T_DOT; return;
        case '+': t->t = T_OP; t->op = OP_ADD; return; case '-': t->t = T_OP; t->op = OP_SUB; return;
        case '*': t->t = T_OP; t->op = OP_MUL; return; case '/': t->t = T_OP; t->op = OP_DIV; return; case '%': t->t = T_OP; t->op = OP_MOD; return;
        case '=': if (i + 1 < p->len && s[i + 1] == '=') { p->pos = i + 2; if (p->pos < p->len && s[p->pos] == '=') p->pos++; t->t = T_OP; t->op = OP_EQ; return; } break;
        case '!': if (i + 1 < p->len && s[i + 1] == '=') { p->pos = i + 2; if (p->pos < p->len && s[p->pos] == '=') p->pos++; t->t = T_OP; t->op = OP_NE; return; } t->t = T_OP; t->op = OP_NOT; return;
        case '<': if (i + 1 < p->len && s[i + 1] == '=') { p->pos = i + 2; t->t = T_OP; t->op = OP_LE; return; } t->t = T_OP; t->op = OP_LT; return;
        case '>': if (i + 1 < p->len && s[i + 1] == '=') { p->pos = i + 2; t->t = T_OP; t->op = OP_GE; return; } t->t = T_OP; t->op = OP_GT; return;
        case '&': if (i + 1 < p->len && s[i + 1] == '&') { p->pos = i + 2; t->t = T_OP; t->op = OP_AND; return; } break;
        case '|': if (i + 1 < p->len && s[i + 1] == '|') { p->pos = i + 2; t->t = T_OP; t->op = OP_OR; return; } break;
        default: break;
        }
        perr(p, i, "unexpected character '%c'", (c >= 32 && c < 127) ? c : '?');
        t->t = T_EOF; p->pos = p->len;
    }
}

static void advance(Parser *p) {
    TokType t = p->tok.t;
    p->prev_operand = t == T_NUM || t == T_STR || t == T_IDENT || t == T_REGEX || t == T_RP || t == T_RB;
    lex(p);
}

/* ---- the parser ---- */

static Node *parse_or(Parser *p);

static Node *mk(Parser *p, NType t) {
    Node *n = node_new(p->e, t);
    if (!n) perr(p, (size_t)p->tok.pos, "out of memory");
    return n;
}

static int add_arg(Node *call, Node *arg) {
    Node **na = realloc(call->args, (call->nargs + 1) * sizeof *na);
    if (!na) return -1;
    call->args = na;
    call->args[call->nargs++] = arg;
    return 0;
}

static Node *parse_primary(Parser *p) {
    Tok *t = &p->tok;
    Node *n;
    if (p->err[0]) return NULL;
    if (++p->depth > MAX_DEPTH) { perr(p, (size_t)t->pos, "the expression is nested too deeply"); return NULL; }
    switch (t->t) {
    case T_NUM: n = mk(p, N_NUM); if (n) n->num = t->num; advance(p); break;
    case T_STR: n = mk(p, N_STR); if (n) { n->s = t->s; t->s = NULL; } advance(p); break;
    case T_REGEX: n = mk(p, N_REGEX); if (n) { n->s = t->s; n->flags = t->flags; t->s = t->flags = NULL; } advance(p); break;
    case T_IDENT:
        if (!strcmp(t->s, "true") || !strcmp(t->s, "false")) { n = mk(p, N_BOOL); if (n) n->num = t->s[0] == 't'; }
        else if (!strcmp(t->s, "null")) n = mk(p, N_NULL);
        else { n = mk(p, N_IDENT); if (n) { n->s = t->s; t->s = NULL; } }
        advance(p);
        break;
    case T_LP:
        advance(p);
        n = parse_or(p);
        if (!n) break;
        if (p->tok.t != T_RP) { perr(p, (size_t)p->tok.pos, "a ) is missing"); n = NULL; break; }
        advance(p);
        break;
    case T_LB:
        n = mk(p, N_LIST);
        advance(p);
        if (!n) break;
        if (p->tok.t != T_RB) {
            for (;;) {
                Node *el = parse_or(p);
                if (!el) { n = NULL; break; }
                if (add_arg(n, el)) { perr(p, (size_t)p->tok.pos, "out of memory"); n = NULL; break; }
                if (p->tok.t == T_COMMA) { advance(p); continue; }
                break;
            }
            if (!n) break;
        }
        if (p->tok.t != T_RB) { perr(p, (size_t)p->tok.pos, "a ] is missing"); n = NULL; break; }
        advance(p);
        break;
    case T_EOF: perr(p, (size_t)t->pos, "the expression ends too soon"); n = NULL; break;
    default: perr(p, (size_t)t->pos, "this does not belong here"); n = NULL; break;
    }
    p->depth--;
    return n;
}

static Node *parse_postfix(Parser *p) {
    Node *n = parse_primary(p);
    while (n && !p->err[0]) {
        if (p->tok.t == T_DOT) {
            Node *m;
            advance(p);
            if (p->tok.t != T_IDENT) { perr(p, (size_t)p->tok.pos, "a name is missing after the ."); return NULL; }
            m = mk(p, N_MEMBER);
            if (!m) return NULL;
            m->a = n; m->s = p->tok.s; p->tok.s = NULL;
            advance(p);
            n = m;
        } else if (p->tok.t == T_LB) {
            Node *m;
            advance(p);
            m = mk(p, N_INDEX);
            if (!m) return NULL;
            m->a = n;
            if (++p->depth > MAX_DEPTH) { perr(p, (size_t)p->tok.pos, "the expression is nested too deeply"); return NULL; }
            m->b = parse_or(p);
            p->depth--;
            if (!m->b) return NULL;
            if (p->tok.t != T_RB) { perr(p, (size_t)p->tok.pos, "a ] is missing"); return NULL; }
            advance(p);
            n = m;
        } else if (p->tok.t == T_LP) {
            Node *m;
            if (n->t != N_IDENT && n->t != N_MEMBER) { perr(p, (size_t)p->tok.pos, "only a function can be called"); return NULL; }
            advance(p);
            m = mk(p, N_CALL);
            if (!m) return NULL;
            m->a = n;
            if (p->tok.t != T_RP) {
                for (;;) {
                    Node *arg;
                    if (++p->depth > MAX_DEPTH) { perr(p, (size_t)p->tok.pos, "the expression is nested too deeply"); return NULL; }
                    arg = parse_or(p);
                    p->depth--;
                    if (!arg) return NULL;
                    if (add_arg(m, arg)) { perr(p, (size_t)p->tok.pos, "out of memory"); return NULL; }
                    if (p->tok.t == T_COMMA) { advance(p); continue; }
                    break;
                }
            }
            if (p->tok.t != T_RP) { perr(p, (size_t)p->tok.pos, "a ) is missing"); return NULL; }
            advance(p);
            n = m;
        } else break;
    }
    return p->err[0] ? NULL : n;
}

static Node *parse_unary(Parser *p) {
    if (p->tok.t == T_OP && (p->tok.op == OP_NOT || p->tok.op == OP_SUB)) {
        Node *n = mk(p, N_UNARY);
        if (!n) return NULL;
        n->op = p->tok.op == OP_NOT ? OP_NOT : OP_NEG;
        advance(p);
        if (++p->depth > MAX_DEPTH) { perr(p, (size_t)p->tok.pos, "the expression is nested too deeply"); return NULL; }
        n->a = parse_unary(p);
        p->depth--;
        return n->a ? n : NULL;
    }
    return parse_postfix(p);
}

typedef struct Level { int ops[6]; Node *(*next)(Parser *); } Level;
static Node *parse_binary(Parser *p, const int *ops, Node *(*next)(Parser *)) {
    Node *left = next(p);
    while (left && p->tok.t == T_OP) {
        int k, found = 0, op = p->tok.op;
        Node *n, *right;
        for (k = 0; ops[k]; k++) if (ops[k] == op) { found = 1; break; }
        if (!found) break;
        advance(p);
        right = next(p);
        if (!right) return NULL;
        n = mk(p, N_BINARY);
        if (!n) return NULL;
        n->op = op; n->a = left; n->b = right;
        left = n;
    }
    return left;
}
static Node *parse_mul(Parser *p) { static const int ops[] = { OP_MUL, OP_DIV, OP_MOD, 0 }; return parse_binary(p, ops, parse_unary); }
static Node *parse_add(Parser *p) { static const int ops[] = { OP_ADD, OP_SUB, 0 }; return parse_binary(p, ops, parse_mul); }
static Node *parse_cmp(Parser *p) { static const int ops[] = { OP_LT, OP_GT, OP_LE, OP_GE, 0 }; return parse_binary(p, ops, parse_add); }
static Node *parse_eq(Parser *p) { static const int ops[] = { OP_EQ, OP_NE, 0 }; return parse_binary(p, ops, parse_cmp); }
static Node *parse_and(Parser *p) { static const int ops[] = { OP_AND, 0 }; return parse_binary(p, ops, parse_eq); }
static Node *parse_or(Parser *p) { static const int ops[] = { OP_OR, 0 }; return parse_binary(p, ops, parse_and); }

ExExpr *ex_expr_parse(const char *text, char *err, size_t errcap, int *err_pos) {
    Parser p;
    Node *root;
    ExExpr *e = calloc(1, sizeof *e);
    if (err && errcap) err[0] = 0;
    if (!e) { if (err && errcap) snprintf(err, errcap, "out of memory"); return NULL; }
    memset(&p, 0, sizeof p);
    p.src = text ? text : "";
    p.len = strlen(p.src);
    p.e = e;
    lex(&p);
    root = parse_or(&p);
    if (root && !p.err[0] && p.tok.t != T_EOF) perr(&p, (size_t)p.tok.pos, "this does not belong here");
    tok_free(&p.tok);
    if (!root || p.err[0]) {
        if (err && errcap) snprintf(err, errcap, "%s", p.err[0] ? p.err : "the expression is not valid");
        if (err_pos) *err_pos = p.err_pos;
        ex_expr_free(e);
        return NULL;
    }
    e->root = root;
    return e;
}

/* ---- evaluation ---- */

typedef struct Scope { struct Scope *parent; const char *name[3]; ExValue val[3]; int n; } Scope;
typedef struct Ev {
    ExArena *a;
    const ExExprHost *host;
    const ExValue *values;
    int depth;
    long steps;
    Scope *scope;
    const char *formulas[12];
    int nformulas;
} Ev;

static ExValue eval(Ev *ev, const Node *n);
static ExValue err(Ev *ev, const char *fmt, ...) {
    char buf[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return ex_v_error(ev->a, "%s", buf);
}
#define IS_ERR(v) ((v).t == EXV_ERROR)

static char *tostr(Ev *ev, ExValue v) { return ex_value_to_string(ev->a, ev->host, v); }
static ExValue str_v(Ev *ev, const char *s) { return ex_v_str(ev->a, s); }

/* a string the string functions can work on: a string, a URL's address, a link's target */
static int as_text(ExValue v, const char **s) {
    if (v.t == EXV_STR || v.t == EXV_HTML) { *s = v.u.str; return 1; }
    if (v.t == EXV_URL) { *s = v.u.url.url; return 1; }
    return 0;
}

/* ---- code points ---- */

static size_t cp_len(unsigned char c) { return c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1; }
static size_t cp_count(const char *s) { size_t n = 0; while (*s) { size_t l = cp_len((unsigned char)*s); size_t k; for (k = 0; k < l && s[k]; k++) {} s += k; n++; } return n; }
static size_t cp_offset(const char *s, size_t idx) {                 /* the byte of code point idx (or the end) */
    size_t o = 0;
    while (s[o] && idx > 0) { size_t l = cp_len((unsigned char)s[o]), k; for (k = 0; k < l && s[o + k]; k++) {} o += k; idx--; }
    return o;
}

/* ---- durations ---- */

typedef struct Unit { const char *name; double ms; int64_t months; int word; } Unit;
static const Unit UNITS[] = {
    { "y", 0, 12, 0 }, { "year", 0, 12, 1 }, { "years", 0, 12, 1 }, { "M", 0, 1, 0 }, { "month", 0, 1, 1 }, { "months", 0, 1, 1 },
    { "w", 604800000.0, 0, 0 }, { "week", 604800000.0, 0, 1 }, { "weeks", 604800000.0, 0, 1 }, { "d", 86400000.0, 0, 0 }, { "day", 86400000.0, 0, 1 }, { "days", 86400000.0, 0, 1 },
    { "h", 3600000.0, 0, 0 }, { "hour", 3600000.0, 0, 1 }, { "hours", 3600000.0, 0, 1 }, { "m", 60000.0, 0, 0 }, { "minute", 60000.0, 0, 1 }, { "minutes", 60000.0, 0, 1 },
    { "s", 1000.0, 0, 0 }, { "second", 1000.0, 0, 1 }, { "seconds", 1000.0, 0, 1 }, { "ms", 1.0, 0, 0 }, { "millisecond", 1.0, 0, 1 }, { "milliseconds", 1.0, 0, 1 },
};

static int same_nocase(const char *a, const char *b) { while (*a && *b) { if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0; a++; b++; } return !*a && !*b; }

/* "1d", "2 hours", "1y 2M", "-3 days": one or more amounts. Returns 0 or -1. */
static int parse_duration(const char *s, double *ms, int64_t *months) {
    double tot_ms = 0;
    int64_t tot_mo = 0;
    int any = 0;
    while (*s) {
        char *end;
        double amount;
        char unit[16];
        size_t k = 0, u;
        int found = 0;
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        amount = strtod(s, &end);
        if (end == s || !isfinite(amount)) return -1;
        s = end;
        while (*s == ' ') s++;
        while (isalpha((unsigned char)*s) && k < sizeof unit - 1) unit[k++] = *s++;
        unit[k] = 0;
        if (!k) return -1;
        for (u = 0; u < sizeof UNITS / sizeof *UNITS; u++) {
            if (UNITS[u].word ? same_nocase(unit, UNITS[u].name) : !strcmp(unit, UNITS[u].name)) {
                tot_ms += amount * UNITS[u].ms;
                tot_mo += (int64_t)llround(amount * (double)UNITS[u].months);
                found = 1;
                break;
            }
        }
        if (!found) return -1;
        any = 1;
        while (*s == ' ' || *s == ',') s++;
    }
    if (!any) return -1;
    *ms = tot_ms; *months = tot_mo;
    return 0;
}

static double add_duration(Ev *ev, double date_ms, double dms, int64_t dmonths, int sign) {
    double r = date_ms;
    if (dmonths) r = ex_date_add_months(r, ev->host ? ev->host->tz_min : 0, sign * dmonths);
    return r + sign * dms;
}

/* ---- regular expressions ---- */

typedef struct Re { pcre2_code *code; int global; } Re;
static int re_compile(ExValue v, Re *re, char *errbuf, size_t errcap) {
    uint32_t opts = PCRE2_UTF;
    int code;
    PCRE2_SIZE off;
    const char *f = v.u.re.flags ? v.u.re.flags : "";
    memset(re, 0, sizeof *re);
    for (; *f; f++) {
        if (*f == 'i') opts |= PCRE2_CASELESS;
        else if (*f == 'm') opts |= PCRE2_MULTILINE;
        else if (*f == 's') opts |= PCRE2_DOTALL;
        else if (*f == 'g') re->global = 1;
    }
    re->code = pcre2_compile((PCRE2_SPTR)v.u.re.pattern, PCRE2_ZERO_TERMINATED, opts, &code, &off, NULL);
    if (!re->code) { PCRE2_UCHAR b[120]; pcre2_get_error_message(code, b, sizeof b); snprintf(errbuf, errcap, "the regular expression is not valid: %s", (char *)b); return -1; }
    return 0;
}
static pcre2_match_context *re_limits(void) {
    pcre2_match_context *mc = pcre2_match_context_create(NULL);
    if (mc) { pcre2_set_match_limit(mc, 200000); pcre2_set_depth_limit(mc, 2000); pcre2_set_heap_limit(mc, 2048); }
    return mc;
}

/* ---- numbers ---- */

static int to_num(ExValue v, double *d) {
    if (v.t == EXV_NUM || v.t == EXV_BOOL) { *d = v.u.num; return 1; }
    return 0;
}

static ExValue need(Ev *ev, const char *fn, size_t nargs, size_t lo, size_t hi) {
    if (nargs < lo || nargs > hi) return err(ev, "%s takes %s%zu argument%s, not %zu", fn, lo == hi ? "" : "from ", lo, lo == 1 && hi == lo ? "" : "s", nargs);
    return ex_v_null();
}

/* ---- members and indexes ---- */

static int file_field(Ev *ev, const char *path, const char *field, ExValue *out) {
    if (ev->host && ev->host->file_field && path) return ev->host->file_field(ev->host->user, ev->a, path, field, out);
    return 0;
}

static ExValue eval_formula(Ev *ev, const char *name) {
    const ExExpr *f = ev->host && ev->host->formula ? ev->host->formula(ev->host->user, name) : NULL;
    ExValue r;
    Scope *saved;
    const ExValue *saved_values;
    int i;
    if (!f) return ex_v_null();
    for (i = 0; i < ev->nformulas; i++) if (!strcmp(ev->formulas[i], name)) {
        char chain[260] = "";
        int k;
        for (k = i; k < ev->nformulas; k++) { strncat(chain, ev->formulas[k], sizeof chain - strlen(chain) - 6); strcat(chain, " -> "); }
        strncat(chain, name, sizeof chain - strlen(chain) - 1);
        return err(ev, "circular reference: %s", chain);
    }
    if (ev->nformulas >= (int)(sizeof ev->formulas / sizeof *ev->formulas)) return err(ev, "formulas refer to each other too deeply");
    ev->formulas[ev->nformulas++] = name;
    saved = ev->scope; saved_values = ev->values;
    ev->scope = NULL; ev->values = NULL;
    r = eval(ev, f->root);
    ev->scope = saved; ev->values = saved_values;
    ev->nformulas--;
    return r;
}

static ExValue member(Ev *ev, ExValue o, const char *name) {
    size_t i;
    ExValue out;
    ExDateParts dp;
    switch (o.t) {
    case EXV_NS:
        if (o.u.ns == 1) return eval_formula(ev, name);
        if (ev->host && ev->host->note_prop && ev->host->note_prop(ev->host->user, ev->a, name, &out)) return out;
        return ex_v_null();
    case EXV_OBJECT:
        for (i = 0; i < o.u.obj.n; i++) if (!strcmp(o.u.obj.keys[i], name)) return o.u.obj.v[i];
        return ex_v_null();
    case EXV_LIST: if (!strcmp(name, "length")) return ex_v_num((double)o.u.list.n); return ex_v_null();
    case EXV_STR: case EXV_URL: case EXV_HTML: {
        const char *t;
        if (!strcmp(name, "length") && as_text(o, &t)) return ex_v_num((double)cp_count(t));
        return ex_v_null();
    }
    case EXV_DATE:
        ex_date_parts(o.u.date.ms, ev->host ? ev->host->tz_min : 0, &dp);
        if (!strcmp(name, "year")) return ex_v_num(dp.year);
        if (!strcmp(name, "month")) return ex_v_num(dp.month);
        if (!strcmp(name, "day")) return ex_v_num(dp.day);
        if (!strcmp(name, "hour")) return ex_v_num(dp.hour);
        if (!strcmp(name, "minute")) return ex_v_num(dp.minute);
        if (!strcmp(name, "second")) return ex_v_num(dp.second);
        if (!strcmp(name, "millisecond")) return ex_v_num(dp.ms);
        return ex_v_null();
    case EXV_DURATION: {
        double ms = o.u.dur.ms + (double)o.u.dur.months * 2629746000.0;                  /* a month is 30.436875 days when it has to be a length */
        if (!strcmp(name, "milliseconds")) return ex_v_num(floor(ms));
        if (!strcmp(name, "seconds")) return ex_v_num(floor(ms / 1000.0));
        if (!strcmp(name, "minutes")) return ex_v_num(floor(ms / 60000.0));
        if (!strcmp(name, "hours")) return ex_v_num(floor(ms / 3600000.0));
        if (!strcmp(name, "days")) return ex_v_num(floor(ms / 86400000.0));
        if (!strcmp(name, "weeks")) return ex_v_num(floor(ms / 604800000.0));
        if (!strcmp(name, "months")) return ex_v_num(floor(ms / 2629746000.0));
        if (!strcmp(name, "years")) return ex_v_num(floor(ms / 31556952000.0));
        return ex_v_null();
    }
    case EXV_FILE: {
        if (!strcmp(name, "file") || !strcmp(name, "note")) return o;
        if (file_field(ev, o.u.file, name, &out)) return out;
        if (file_field(ev, o.u.file, "properties", &out) && out.t == EXV_OBJECT) return member(ev, out, name);
        return ex_v_null();
    }
    case EXV_LINK:
        if (!strcmp(name, "path")) return str_v(ev, o.u.link.path);
        if (!strcmp(name, "display")) return o.u.link.display ? str_v(ev, o.u.link.display) : ex_v_null();
        return ex_v_null();
    default: return ex_v_null();
    }
}

static ExValue index_of(Ev *ev, ExValue o, ExValue idx) {
    double d;
    if (o.t == EXV_LIST && to_num(idx, &d)) { if (d >= 0 && d < (double)o.u.list.n && d == floor(d)) return o.u.list.v[(size_t)d]; return ex_v_null(); }
    if ((o.t == EXV_STR || o.t == EXV_URL) && to_num(idx, &d)) {
        const char *t;
        if (as_text(o, &t) && d >= 0 && d == floor(d) && d < (double)cp_count(t)) { size_t b = cp_offset(t, (size_t)d), l = cp_len((unsigned char)t[b]); return ex_v_strn(ev->a, t + b, l); }
        return ex_v_null();
    }
    if ((o.t == EXV_OBJECT || o.t == EXV_NS || o.t == EXV_FILE) && idx.t == EXV_STR) return member(ev, o, idx.u.str);
    if (o.t == EXV_NULL) return ex_v_null();
    return err(ev, "a %s cannot be indexed by a %s", ex_value_type_name(o), ex_value_type_name(idx));
}

/* ---- operators ---- */

static ExValue arith(Ev *ev, int op, ExValue a, ExValue b) {
    double x, y;
    if (IS_ERR(a)) return a;
    if (IS_ERR(b)) return b;
    if (op == OP_ADD && (a.t == EXV_STR || b.t == EXV_STR || a.t == EXV_HTML || b.t == EXV_HTML) && a.t != EXV_DATE && b.t != EXV_DATE) {
        char *s1 = tostr(ev, a), *s2 = tostr(ev, b);
        size_t l1 = strlen(s1), l2 = strlen(s2);
        char *r;
        if (l1 + l2 > MAX_STRING) return err(ev, "the text is too long");
        r = ex_arena_alloc(ev->a, l1 + l2 + 1);
        if (!r) return err(ev, "out of memory");
        memcpy(r, s1, l1); memcpy(r + l1, s2, l2);
        { ExValue v = ex_v_null(); v.t = EXV_STR; v.u.str = r; return v; }
    }
    if (a.t == EXV_NULL || b.t == EXV_NULL) return ex_v_null();
    /* dates and durations */
    if (a.t == EXV_DATE && (op == OP_ADD || op == OP_SUB)) {
        double dms = 0;
        int64_t dmo = 0;
        if (b.t == EXV_DURATION) { dms = b.u.dur.ms; dmo = b.u.dur.months; }
        else if (b.t == EXV_STR) { if (parse_duration(b.u.str, &dms, &dmo)) return err(ev, "\"%s\" is not a duration (like 1d, 2 hours or 1M)", b.u.str); }
        else if (b.t == EXV_DATE && op == OP_SUB) return ex_v_duration(a.u.date.ms - b.u.date.ms, 0);
        else return err(ev, "a duration cannot be %s a %s", op == OP_ADD ? "added to" : "taken from", ex_value_type_name(b));
        return ex_v_date(add_duration(ev, a.u.date.ms, dms, dmo, op == OP_ADD ? 1 : -1), a.u.date.has_time || fmod(dms, 86400000.0) != 0);       /* whole days keep a date a date */
    }
    if (a.t == EXV_DURATION && b.t == EXV_DATE && op == OP_ADD) return ex_v_date(add_duration(ev, b.u.date.ms, a.u.dur.ms, a.u.dur.months, 1), b.u.date.has_time || fmod(a.u.dur.ms, 86400000.0) != 0);
    if (a.t == EXV_DURATION) {
        if (b.t == EXV_DURATION && (op == OP_ADD || op == OP_SUB)) return ex_v_duration(a.u.dur.ms + (op == OP_ADD ? 1 : -1) * b.u.dur.ms, a.u.dur.months + (op == OP_ADD ? 1 : -1) * b.u.dur.months);
        if (b.t == EXV_STR && (op == OP_ADD || op == OP_SUB)) {
            double dms; int64_t dmo;
            if (parse_duration(b.u.str, &dms, &dmo)) return err(ev, "\"%s\" is not a duration", b.u.str);
            return ex_v_duration(a.u.dur.ms + (op == OP_ADD ? 1 : -1) * dms, a.u.dur.months + (op == OP_ADD ? 1 : -1) * dmo);
        }
        if (to_num(b, &y) && (op == OP_MUL || op == OP_DIV)) {
            if (op == OP_DIV && y == 0) return err(ev, "a duration cannot be divided by zero");
            return ex_v_duration(op == OP_MUL ? a.u.dur.ms * y : a.u.dur.ms / y, (int64_t)llround(op == OP_MUL ? (double)a.u.dur.months * y : (double)a.u.dur.months / y));
        }
        if (b.t == EXV_DURATION && op == OP_DIV) { double d = b.u.dur.ms + (double)b.u.dur.months * 2629746000.0; if (d == 0) return err(ev, "a duration cannot be divided by zero"); return ex_v_num((a.u.dur.ms + (double)a.u.dur.months * 2629746000.0) / d); }
        return err(ev, "a duration cannot be %s a %s", op == OP_ADD ? "added to" : op == OP_SUB ? "taken from" : "combined with", ex_value_type_name(b));
    }
    if (!to_num(a, &x) || !to_num(b, &y)) return err(ev, "cannot do arithmetic on a %s and a %s", ex_value_type_name(a), ex_value_type_name(b));
    switch (op) {
    case OP_ADD: return ex_v_num(x + y);
    case OP_SUB: return ex_v_num(x - y);
    case OP_MUL: return ex_v_num(x * y);
    case OP_DIV: return ex_v_num(x / y);
    case OP_MOD: return ex_v_num(fmod(x, y));
    default: return err(ev, "unknown operator");
    }
}

static ExValue compare_op(Ev *ev, int op, ExValue a, ExValue b) {
    int c;
    if (IS_ERR(a)) return a;
    if (IS_ERR(b)) return b;
    if (op == OP_EQ) return ex_v_bool(ex_value_equal(ev->host, a, b));
    if (op == OP_NE) return ex_v_bool(!ex_value_equal(ev->host, a, b));
    if (a.t == EXV_NULL || b.t == EXV_NULL) return ex_v_bool(0);                                   /* nothing is smaller or larger than a missing value */
    if (a.t == EXV_BOOL) a = ex_v_num(a.u.num);
    if (b.t == EXV_BOOL) b = ex_v_num(b.u.num);
    if (a.t != b.t || (a.t != EXV_NUM && a.t != EXV_STR && a.t != EXV_DATE && a.t != EXV_DURATION)) return err(ev, "cannot compare a %s with a %s", ex_value_type_name(a), ex_value_type_name(b));
    if (a.t == EXV_NUM && (isnan(a.u.num) || isnan(b.u.num))) return ex_v_bool(0);
    c = ex_value_compare(a, b);
    switch (op) { case OP_LT: return ex_v_bool(c < 0); case OP_GT: return ex_v_bool(c > 0); case OP_LE: return ex_v_bool(c <= 0); default: return ex_v_bool(c >= 0); }
}

/* ---- the functions ---- */

static const char *const FUNCTIONS[] = {
    "global:escapeHTML", "global:date", "global:duration", "global:file", "global:html", "global:if", "global:image", "global:icon", "global:link", "global:list",
    "global:max", "global:min", "global:now", "global:number", "global:today", "global:random",
    "any:isTruthy", "any:isType", "any:toString",
    "date:date", "date:format", "date:time", "date:relative", "date:isEmpty",
    "string:contains", "string:containsAll", "string:containsAny", "string:endsWith", "string:isEmpty", "string:lower", "string:replace", "string:repeat", "string:reverse",
    "string:slice", "string:split", "string:startsWith", "string:title", "string:trim",
    "number:abs", "number:ceil", "number:floor", "number:isEmpty", "number:round", "number:toFixed",
    "list:contains", "list:containsAll", "list:containsAny", "list:filter", "list:flat", "list:isEmpty", "list:join", "list:map", "list:reduce", "list:reverse", "list:slice",
    "list:sort", "list:unique", "list:sum", "list:mean", "list:median", "list:stddev",
    "link:asFile", "link:linksTo",
    "file:asLink", "file:hasLink", "file:hasProperty", "file:hasTag", "file:inFolder",
    "object:isEmpty", "object:keys", "object:values",
    "regexp:matches",
};
size_t ex_expr_function_count(void) { return sizeof FUNCTIONS / sizeof *FUNCTIONS; }
const char *ex_expr_function_name(size_t i) { return i < sizeof FUNCTIONS / sizeof *FUNCTIONS ? FUNCTIONS[i] : NULL; }

static ExValue make_list(Ev *ev, const ExValue *items, size_t n) {
    if (n > MAX_LIST) return err(ev, "the list is too long");
    return ex_v_list(ev->a, items, n);
}

static ExValue date_midnight(Ev *ev, double ms) {
    ExDateParts p;
    int tz = ev->host ? ev->host->tz_min : 0;
    ex_date_parts(ms, tz, &p);
    return ex_v_date(ex_date_make(p.year, p.month, p.day, 0, 0, 0, 0, tz), 0);
}

static ExValue relative_text(Ev *ev, double ms) {
    double now = ev->host ? ev->host->now_ms : 0, diff = ms - now, a = fabs(diff) / 1000.0;
    char b[80], *r;
    const char *what;
    long long n = 0;
    int fut = diff > 0;
    if (a < 45) what = "a few seconds";
    else if (a < 90) what = "a minute";
    else if (a / 60 < 45) { n = (long long)floor(a / 60 + 0.5); snprintf(b, sizeof b, "%lld minutes", n); what = b; }
    else if (a / 60 < 90) what = "an hour";
    else if (a / 3600 < 22) { n = (long long)floor(a / 3600 + 0.5); snprintf(b, sizeof b, "%lld hours", n); what = b; }
    else if (a / 3600 < 36) what = "a day";
    else if (a / 86400 < 26) { n = (long long)floor(a / 86400 + 0.5); snprintf(b, sizeof b, "%lld days", n); what = b; }
    else if (a / 86400 < 45) what = "a month";
    else if (a / 86400 < 320) { n = (long long)floor(a / 2629746.0 + 0.5); if (n < 2) n = 2; snprintf(b, sizeof b, "%lld months", n); what = b; }
    else if (a / 86400 < 548) what = "a year";
    else { n = (long long)floor(a / 31556952.0 + 0.5); snprintf(b, sizeof b, "%lld years", n); what = b; }
    r = ex_arena_alloc(ev->a, strlen(what) + 12);
    if (!r) return err(ev, "out of memory");
    sprintf(r, fut ? "in %s" : "%s ago", what);
    return str_v(ev, r);
}

static char *lower_copy(Ev *ev, const char *s) {
    size_t n = strlen(s), i;
    char *r = ex_arena_alloc(ev->a, n + 1);
    if (!r) return NULL;
    for (i = 0; i < n; i++) r[i] = (char)((unsigned char)s[i] < 0x80 ? tolower((unsigned char)s[i]) : s[i]);
    return r;
}

static int file_tags_has(Ev *ev, const char *path, const char *tag) {
    ExValue tags;
    size_t i, tl;
    while (*tag == '#') tag++;
    tl = strlen(tag);
    if (!file_field(ev, path, "tags", &tags) || tags.t != EXV_LIST) return 0;
    for (i = 0; i < tags.u.list.n; i++) {
        const char *t = tags.u.list.v[i].t == EXV_STR ? tags.u.list.v[i].u.str : "";
        size_t k;
        while (*t == '#') t++;
        for (k = 0; k < tl && t[k] && tolower((unsigned char)t[k]) == tolower((unsigned char)tag[k]); k++) {}
        if (k == tl && (t[tl] == 0 || t[tl] == '/')) return 1;                                    /* the tag, or a nested tag under it */
    }
    return 0;
}

static int resolve_path(Ev *ev, const char *from, const char *text, char *out, size_t cap) {
    if (ev->host && ev->host->resolve) return ev->host->resolve(ev->host->user, from ? from : "", text, out, cap);
    return 0;
}

static int path_of(Ev *ev, ExValue v, const char *from, char *out, size_t cap) {
    if (v.t == EXV_FILE) { snprintf(out, cap, "%s", v.u.file); return 1; }
    if (v.t == EXV_LINK) return resolve_path(ev, from, v.u.link.path, out, cap);
    if (v.t == EXV_STR) { if (resolve_path(ev, from, v.u.str, out, cap)) return 1; snprintf(out, cap, "%s", v.u.str); return 1; }
    return 0;
}

static int file_links_to(Ev *ev, const char *path, const char *target) {
    ExValue links;
    size_t i;
    if (!file_field(ev, path, "links", &links) || links.t != EXV_LIST) return 0;
    for (i = 0; i < links.u.list.n; i++) {
        char p[700];
        ExValue l = links.u.list.v[i];
        if (l.t == EXV_LINK) { if (resolve_path(ev, path, l.u.link.path, p, sizeof p) ? !strcmp(p, target) : !strcmp(l.u.link.path, target)) return 1; }
        else if (l.t == EXV_FILE && !strcmp(l.u.file, target)) return 1;
    }
    return 0;
}

static ExValue parse_link(Ev *ev, ExValue path, const ExValue *display) {
    const char *s, *disp = NULL;
    char *work, *bar, *hash;
    size_t n;
    ExValue r;
    if (path.t == EXV_FILE) { r = ex_v_link(ev->a, path.u.file, NULL, NULL); if (display && display->t != EXV_NULL) r.u.link.display = tostr(ev, *display); return r; }
    if (path.t == EXV_LINK) { r = path; if (display && display->t != EXV_NULL) r.u.link.display = tostr(ev, *display); return r; }
    if (!as_text(path, &s)) return err(ev, "link() needs a path or a file, not a %s", ex_value_type_name(path));
    if (!strncmp(s, "http://", 7) || !strncmp(s, "https://", 8) || !strncmp(s, "mailto:", 7)) return ex_v_url(ev->a, s, display && display->t != EXV_NULL ? tostr(ev, *display) : NULL);
    n = strlen(s);
    work = ex_arena_strndup(ev->a, s, n);
    if (!work) return err(ev, "out of memory");
    if (n >= 4 && work[0] == '[' && work[1] == '[' && work[n - 1] == ']' && work[n - 2] == ']') { work[n - 2] = 0; work += 2; }
    bar = strchr(work, '|');
    if (bar) { *bar = 0; disp = bar + 1; }
    hash = strchr(work, '#');
    if (hash) *hash++ = 0;
    if (display && display->t != EXV_NULL) disp = tostr(ev, *display);
    return ex_v_link(ev->a, work, hash, disp);
}

static ExValue to_number(Ev *ev, ExValue v) {
    double d;
    char *end, *t;
    switch (v.t) {
    case EXV_NUM: return v;
    case EXV_BOOL: return ex_v_num(v.u.num);
    case EXV_DATE: return ex_v_num(v.u.date.ms);
    case EXV_NULL: return ex_v_null();
    case EXV_STR:
        t = (char *)v.u.str;
        while (*t == ' ' || *t == '\t' || *t == '\n') t++;
        if (!*t) return err(ev, "\"%s\" is not a number", v.u.str);
        d = strtod(t, &end);
        while (*end == ' ' || *end == '\t' || *end == '\n') end++;
        if (*end || (t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) || !strcmp(t, "inf") || !strcmp(t, "nan") || !strncmp(t, "-inf", 4) || !strncmp(t, "-nan", 4) || !strncmp(t, "+inf", 4)) return err(ev, "\"%s\" is not a number", v.u.str);
        return ex_v_num(d);
    default: return err(ev, "a %s cannot be turned into a number", ex_value_type_name(v));
    }
}

static ExValue call_global(Ev *ev, const char *name, const ExValue *a, size_t n) {
    ExValue e;
    const ExExprHost *h = ev->host;
    if (!strcmp(name, "now")) { e = need(ev, name, n, 0, 0); if (IS_ERR(e)) return e; return ex_v_date(h ? h->now_ms : 0, 1); }
    if (!strcmp(name, "today")) { e = need(ev, name, n, 0, 0); if (IS_ERR(e)) return e; return date_midnight(ev, h ? h->now_ms : 0); }
    if (!strcmp(name, "random")) {
        e = need(ev, name, n, 0, 0); if (IS_ERR(e)) return e;
        return ex_v_num(h && h->random ? h->random(h->user) : (double)rand() / ((double)RAND_MAX + 1.0));
    }
    if (!strcmp(name, "escapeHTML")) {
        const char *s;
        char *r, *o;
        size_t i, len;
        e = need(ev, name, n, 1, 1); if (IS_ERR(e)) return e;
        if (a[0].t == EXV_NULL) return ex_v_null();
        s = tostr(ev, a[0]);
        len = strlen(s);
        r = ex_arena_alloc(ev->a, len * 6 + 1);
        if (!r) return err(ev, "out of memory");
        o = r;
        for (i = 0; i < len; i++) {
            switch (s[i]) {
            case '&': o += sprintf(o, "&amp;"); break; case '<': o += sprintf(o, "&lt;"); break; case '>': o += sprintf(o, "&gt;"); break;
            case '"': o += sprintf(o, "&quot;"); break; case '\'': o += sprintf(o, "&#39;"); break; default: *o++ = s[i];
            }
        }
        return str_v(ev, r);
    }
    if (!strcmp(name, "html")) { ExValue v; e = need(ev, name, n, 1, 1); if (IS_ERR(e)) return e; v = ex_v_null(); v.t = EXV_HTML; v.u.str = tostr(ev, a[0]); return v; }
    if (!strcmp(name, "icon")) { ExValue v; e = need(ev, name, n, 1, 1); if (IS_ERR(e)) return e; if (a[0].t != EXV_STR) return err(ev, "icon() needs a name"); v = ex_v_null(); v.t = EXV_ICON; v.u.str = a[0].u.str; return v; }
    if (!strcmp(name, "image")) {
        ExValue v = ex_v_null();
        e = need(ev, name, n, 1, 1); if (IS_ERR(e)) return e;
        v.t = EXV_IMAGE;
        if (a[0].t == EXV_STR) v.u.image = a[0].u.str;
        else if (a[0].t == EXV_FILE) v.u.image = a[0].u.file;
        else if (a[0].t == EXV_URL) v.u.image = a[0].u.url.url;
        else if (a[0].t == EXV_LINK) v.u.image = a[0].u.link.path;
        else if (a[0].t == EXV_NULL) return ex_v_null();
        else return err(ev, "image() needs a path, a file or an address, not a %s", ex_value_type_name(a[0]));
        return v;
    }
    if (!strcmp(name, "list")) {
        e = need(ev, name, n, 1, 1); if (IS_ERR(e)) return e;
        if (a[0].t == EXV_LIST) return a[0];
        if (a[0].t == EXV_NULL) return ex_v_list(ev->a, NULL, 0);
        return make_list(ev, a, 1);
    }
    if (!strcmp(name, "link")) { e = need(ev, name, n, 1, 2); if (IS_ERR(e)) return e; return parse_link(ev, a[0], n > 1 ? &a[1] : NULL); }
    if (!strcmp(name, "number")) { e = need(ev, name, n, 1, 1); if (IS_ERR(e)) return e; return to_number(ev, a[0]); }
    if (!strcmp(name, "date")) {
        double ms;
        int has;
        e = need(ev, name, n, 1, 1); if (IS_ERR(e)) return e;
        if (a[0].t == EXV_DATE) return a[0];
        if (a[0].t == EXV_NULL) return ex_v_null();
        if (a[0].t == EXV_NUM) return ex_v_date(a[0].u.num, 1);
        if (a[0].t != EXV_STR) return err(ev, "date() needs text like 2025-01-31 or 2025-01-31 14:30:00");
        if (ex_date_parse(a[0].u.str, strlen(a[0].u.str), h ? h->tz_min : 0, &ms, &has)) return err(ev, "\"%s\" is not a date (use YYYY-MM-DD or YYYY-MM-DD HH:mm:ss)", a[0].u.str);
        return ex_v_date(ms, has);
    }
    if (!strcmp(name, "duration")) {
        double ms;
        int64_t mo;
        e = need(ev, name, n, 1, 1); if (IS_ERR(e)) return e;
        if (a[0].t == EXV_DURATION) return a[0];
        if (a[0].t == EXV_NULL) return ex_v_null();
        if (a[0].t != EXV_STR || parse_duration(a[0].u.str, &ms, &mo)) return err(ev, "\"%s\" is not a duration (like 1d, 2 hours or 1y 2M)", a[0].t == EXV_STR ? a[0].u.str : "?");
        return ex_v_duration(ms, mo);
    }
    if (!strcmp(name, "file")) {
        char path[700];
        e = need(ev, name, n, 1, 1); if (IS_ERR(e)) return e;
        if (a[0].t == EXV_FILE) return a[0];
        if (a[0].t == EXV_NULL) return ex_v_null();
        if (a[0].t == EXV_LINK) return resolve_path(ev, h ? h->file_path : "", a[0].u.link.path, path, sizeof path) ? ex_v_file(ev->a, path) : ex_v_null();
        if (a[0].t == EXV_STR || a[0].t == EXV_URL) {
            const char *s;
            as_text(a[0], &s);
            return resolve_path(ev, h ? h->file_path : "", s, path, sizeof path) ? ex_v_file(ev->a, path) : ex_v_null();
        }
        return err(ev, "file() needs a path, a link or a file, not a %s", ex_value_type_name(a[0]));
    }
    if (!strcmp(name, "max") || !strcmp(name, "min")) {
        const ExValue *items = a;
        size_t cnt = n, i;
        double best = 0;
        int is_max = name[1] == 'a', any = 0;
        if (n == 1 && a[0].t == EXV_LIST) { items = a[0].u.list.v; cnt = a[0].u.list.n; }
        if (!n) return err(ev, "%s needs at least one number", name);
        for (i = 0; i < cnt; i++) {
            double d;
            if (items[i].t == EXV_NULL) continue;
            if (!to_num(items[i], &d)) return err(ev, "%s works on numbers, not a %s", name, ex_value_type_name(items[i]));
            if (isnan(d)) return ex_v_num(NAN);
            if (!any || (is_max ? d > best : d < best)) best = d;
            any = 1;
        }
        return any ? ex_v_num(best) : ex_v_null();
    }
    return err(ev, "there is no function called %s", name);
}

static const char *type_names_match(ExValue v) {
    switch (v.t) {
    case EXV_NULL: return "null"; case EXV_BOOL: return "boolean"; case EXV_NUM: return "number"; case EXV_STR: return "string"; case EXV_DATE: return "date";
    case EXV_DURATION: return "duration"; case EXV_LIST: return "list"; case EXV_OBJECT: return "object"; case EXV_LINK: return "link"; case EXV_URL: return "url";
    case EXV_FILE: return "file"; case EXV_REGEXP: return "regexp"; case EXV_HTML: return "html"; case EXV_IMAGE: return "image"; case EXV_ICON: return "icon"; default: return "";
    }
}

/* JavaScript's slice: negative positions count from the end. */
static void slice_range(const ExValue *a, size_t n, size_t len, size_t *from, size_t *to) {
    double s = 0, e = (double)len;
    if (n > 0 && a[0].t == EXV_NUM) s = trunc(a[0].u.num);
    if (n > 1 && a[1].t == EXV_NUM) e = trunc(a[1].u.num);
    if (s < 0) s += (double)len;
    if (s < 0) s = 0;
    if (s > (double)len) s = (double)len;
    if (e < 0) e += (double)len;
    if (e < 0) e = 0;
    if (e > (double)len) e = (double)len;
    *from = (size_t)s; *to = (size_t)(e > s ? e : s);
}

static ExValue string_replace(Ev *ev, const char *s, ExValue pat, const char *repl) {
    size_t slen = strlen(s), rlen = strlen(repl), cap = slen + 64, o = 0;
    char *out = ex_arena_alloc(ev->a, cap);
    if (!out) return err(ev, "out of memory");
#define GROW(extra) do { if (o + (extra) + 1 > cap) { size_t nc = cap * 2 + (extra); char *no; if (nc > MAX_STRING * 4) return err(ev, "the text is too long"); no = ex_arena_alloc(ev->a, nc); if (!no) return err(ev, "out of memory"); memcpy(no, out, o); out = no; cap = nc; } } while (0)
    if (pat.t == EXV_STR) {
        size_t plen = strlen(pat.u.str), i = 0;
        if (!plen) return str_v(ev, s);
        while (i < slen) {
            if (!strncmp(s + i, pat.u.str, plen)) { GROW(rlen); memcpy(out + o, repl, rlen); o += rlen; i += plen; }
            else { GROW(1); out[o++] = s[i++]; }
        }
        out[o] = 0;
        return str_v(ev, out);
    }
    {
        Re re;
        char eb[160];
        pcre2_match_data *md;
        pcre2_match_context *mc;
        PCRE2_SIZE start = 0;
        int first = 1;
        if (re_compile(pat, &re, eb, sizeof eb)) return err(ev, "%s", eb);
        md = pcre2_match_data_create_from_pattern(re.code, NULL);
        mc = re_limits();
        while (start <= slen) {
            int rc = pcre2_match(re.code, (PCRE2_SPTR)s, slen, start, 0, md, mc);
            PCRE2_SIZE *ov;
            size_t k;
            if (rc <= 0) break;
            ov = pcre2_get_ovector_pointer(md);
            GROW(ov[0] - start);
            memcpy(out + o, s + start, ov[0] - start); o += ov[0] - start;
            for (k = 0; repl[k]; k++) {                                                             /* $1 .. $99, $& and $$ */
                if (repl[k] == '$' && repl[k + 1] == '$') { GROW(1); out[o++] = '$'; k++; }
                else if (repl[k] == '$' && repl[k + 1] == '&') { GROW(ov[1] - ov[0]); memcpy(out + o, s + ov[0], ov[1] - ov[0]); o += ov[1] - ov[0]; k++; }
                else if (repl[k] == '$' && isdigit((unsigned char)repl[k + 1])) {
                    int g = repl[k + 1] - '0';
                    size_t adv = 1;
                    if (isdigit((unsigned char)repl[k + 2]) && g * 10 + (repl[k + 2] - '0') < rc) { g = g * 10 + (repl[k + 2] - '0'); adv = 2; }
                    if (g < rc && g > 0) { GROW(ov[2 * g + 1] - ov[2 * g]); if (ov[2 * g] != PCRE2_UNSET) { memcpy(out + o, s + ov[2 * g], ov[2 * g + 1] - ov[2 * g]); o += ov[2 * g + 1] - ov[2 * g]; } k += adv; }
                    else { GROW(1); out[o++] = '$'; }
                }
                else { GROW(1); out[o++] = repl[k]; }
            }
            first = 0;
            if (ov[1] == ov[0]) { if (ov[0] < slen) { GROW(1); out[o++] = s[ov[0]]; } start = ov[0] + 1; }          /* an empty match moves on one byte */
            else start = ov[1];
            if (!re.global) break;
        }
        (void)first;
        if (start <= slen) { GROW(slen - (start < slen ? start : slen)); if (start < slen) { memcpy(out + o, s + start, slen - start); o += slen - start; } }
        out[o] = 0;
        pcre2_match_data_free(md); pcre2_match_context_free(mc); pcre2_code_free(re.code);
        return str_v(ev, out);
    }
#undef GROW
}

static ExValue string_split(Ev *ev, const char *s, ExValue sep, ExValue limit) {
    ExValue *parts;
    size_t cnt = 0, cap = 16, slen = strlen(s), max = (size_t)-1;
    if (limit.t == EXV_NUM && limit.u.num >= 0) max = (size_t)limit.u.num;
    parts = ex_arena_alloc(ev->a, cap * sizeof *parts);
    if (!parts) return err(ev, "out of memory");
#define PUSH(str, len) do { if (cnt >= max) goto done; if (cnt == cap) { ExValue *np = ex_arena_alloc(ev->a, cap * 2 * sizeof *np); if (!np) return err(ev, "out of memory"); memcpy(np, parts, cnt * sizeof *np); parts = np; cap *= 2; } parts[cnt++] = ex_v_strn(ev->a, (str), (len)); } while (0)
    if (sep.t == EXV_STR) {
        size_t plen = strlen(sep.u.str), i = 0, start = 0;
        if (!plen) { while (i < slen) { size_t l = cp_len((unsigned char)s[i]); if (i + l > slen) l = slen - i; PUSH(s + i, l); i += l; } goto done; }
        while (i + plen <= slen) { if (!strncmp(s + i, sep.u.str, plen)) { PUSH(s + start, i - start); i += plen; start = i; } else i++; }
        PUSH(s + start, slen - start);
    } else {
        Re re;
        char eb[160];
        pcre2_match_data *md;
        pcre2_match_context *mc;
        PCRE2_SIZE start = 0, pos = 0;
        if (re_compile(sep, &re, eb, sizeof eb)) return err(ev, "%s", eb);
        md = pcre2_match_data_create_from_pattern(re.code, NULL);
        mc = re_limits();
        while (pos <= slen) {
            int rc = pcre2_match(re.code, (PCRE2_SPTR)s, slen, pos, 0, md, mc);
            PCRE2_SIZE *ov;
            if (rc <= 0) break;
            ov = pcre2_get_ovector_pointer(md);
            if (ov[1] == ov[0]) { if (ov[0] >= slen) break; if (ov[0] == start) { pos = ov[0] + 1; continue; } }
            if (cnt >= max) break;
            if (cnt == cap) { ExValue *np = ex_arena_alloc(ev->a, cap * 2 * sizeof *np); if (!np) return err(ev, "out of memory"); memcpy(np, parts, cnt * sizeof *np); parts = np; cap *= 2; }
            parts[cnt++] = ex_v_strn(ev->a, s + start, ov[0] - start);
            start = ov[1]; pos = ov[1] > ov[0] ? ov[1] : ov[0] + 1;
        }
        if (cnt < max) PUSH(s + start, slen - start);
        pcre2_match_data_free(md); pcre2_match_context_free(mc); pcre2_code_free(re.code);
    }
done:
#undef PUSH
    return make_list(ev, parts, cnt);
}

typedef struct SortItem { ExValue v; size_t i; } SortItem;
static int sort_cmp(const void *x, const void *y) {
    const SortItem *a = x, *b = y;
    int c = ex_value_compare(a->v, b->v);
    return c ? c : (a->i < b->i ? -1 : a->i > b->i);
}

static int numbers_of(const ExValue *items, size_t n, double *out, size_t *cnt) {
    size_t i, k = 0;
    for (i = 0; i < n; i++) { double d; if (to_num(items[i], &d) && items[i].t == EXV_NUM) out[k++] = d; }
    *cnt = k;
    return 0;
}

static int dcmp(const void *x, const void *y) { double a = *(const double *)x, b = *(const double *)y; return a < b ? -1 : a > b; }

static ExValue list_method(Ev *ev, ExValue r, const char *name, const ExValue *a, size_t n) {
    const ExValue *items = r.u.list.v;
    size_t len = r.u.list.n, i, j;
    ExValue e, *out;
    if (!strcmp(name, "contains")) { e = need(ev, name, n, 1, 1); if (IS_ERR(e)) return e; for (i = 0; i < len; i++) if (ex_value_equal(ev->host, items[i], a[0])) return ex_v_bool(1); ev->steps += (long)len; return ex_v_bool(0); }
    if (!strcmp(name, "containsAll") || !strcmp(name, "containsAny")) {
        int all = name[8] == 'A' && name[9] == 'l';
        for (j = 0; j < n; j++) {
            int found = 0;
            for (i = 0; i < len && !found; i++) found = ex_value_equal(ev->host, items[i], a[j]);
            if (all && !found) return ex_v_bool(0);
            if (!all && found) return ex_v_bool(1);
        }
        return ex_v_bool(all);
    }
    if (!strcmp(name, "isEmpty")) return ex_v_bool(len == 0);
    if (!strcmp(name, "flat")) {
        size_t cnt = 0;
        for (i = 0; i < len; i++) cnt += items[i].t == EXV_LIST ? items[i].u.list.n : 1;
        if (cnt > MAX_LIST) return err(ev, "the list is too long");
        out = cnt ? ex_arena_alloc(ev->a, cnt * sizeof *out) : NULL;
        if (cnt && !out) return err(ev, "out of memory");
        cnt = 0;
        for (i = 0; i < len; i++) { if (items[i].t == EXV_LIST) for (j = 0; j < items[i].u.list.n; j++) out[cnt++] = items[i].u.list.v[j]; else out[cnt++] = items[i]; }
        return make_list(ev, out, cnt);
    }
    if (!strcmp(name, "join")) {
        const char *sep = n > 0 ? tostr(ev, a[0]) : ",";
        size_t seplen = strlen(sep), total = 0, o = 0;
        char **txt = len ? ex_arena_alloc(ev->a, len * sizeof *txt) : NULL;
        char *res;
        if (len && !txt) return err(ev, "out of memory");
        for (i = 0; i < len; i++) { txt[i] = tostr(ev, items[i]); total += strlen(txt[i]) + seplen; if (total > MAX_STRING) return err(ev, "the text is too long"); }
        res = ex_arena_alloc(ev->a, total + 1);
        if (!res) return err(ev, "out of memory");
        for (i = 0; i < len; i++) { size_t l = strlen(txt[i]); if (i) { memcpy(res + o, sep, seplen); o += seplen; } memcpy(res + o, txt[i], l); o += l; }
        return str_v(ev, res);
    }
    if (!strcmp(name, "reverse")) {
        out = len ? ex_arena_alloc(ev->a, len * sizeof *out) : NULL;
        if (len && !out) return err(ev, "out of memory");
        for (i = 0; i < len; i++) out[i] = items[len - 1 - i];
        return make_list(ev, out, len);
    }
    if (!strcmp(name, "slice")) { size_t from, to; slice_range(a, n, len, &from, &to); return make_list(ev, items + from, to - from); }
    if (!strcmp(name, "sort")) {
        SortItem *si = len ? ex_arena_alloc(ev->a, len * sizeof *si) : NULL;
        if (len && !si) return err(ev, "out of memory");
        out = len ? ex_arena_alloc(ev->a, len * sizeof *out) : NULL;
        if (len && !out) return err(ev, "out of memory");
        for (i = 0; i < len; i++) { si[i].v = items[i]; si[i].i = i; }
        if (len) qsort(si, len, sizeof *si, sort_cmp);
        for (i = 0; i < len; i++) out[i] = si[i].v;
        return make_list(ev, out, len);
    }
    if (!strcmp(name, "unique")) {
        size_t cnt = 0;
        out = len ? ex_arena_alloc(ev->a, len * sizeof *out) : NULL;
        if (len && !out) return err(ev, "out of memory");
        for (i = 0; i < len; i++) {
            int dup = 0;
            for (j = 0; j < cnt && !dup; j++) dup = ex_value_equal(ev->host, out[j], items[i]);
            ev->steps += (long)cnt;
            if (!dup) out[cnt++] = items[i];
        }
        return make_list(ev, out, cnt);
    }
    if (!strcmp(name, "sum") || !strcmp(name, "mean") || !strcmp(name, "median") || !strcmp(name, "stddev")) {
        double *nums = len ? ex_arena_alloc(ev->a, len * sizeof *nums) : NULL, total = 0;
        size_t cnt = 0;
        if (len && !nums) return err(ev, "out of memory");
        numbers_of(items, len, nums, &cnt);
        for (i = 0; i < cnt; i++) total += nums[i];
        if (name[0] == 's' && name[1] == 'u') return ex_v_num(total);
        if (!cnt) return ex_v_null();
        if (name[0] == 'm' && name[2] == 'a') return ex_v_num(total / (double)cnt);
        if (name[0] == 'm') {
            qsort(nums, cnt, sizeof *nums, dcmp);
            return ex_v_num(cnt % 2 ? nums[cnt / 2] : (nums[cnt / 2 - 1] + nums[cnt / 2]) / 2.0);
        }
        { double mean = total / (double)cnt, ss = 0; for (i = 0; i < cnt; i++) ss += (nums[i] - mean) * (nums[i] - mean); return ex_v_num(sqrt(ss / (double)cnt)); }
    }
    return err(ev, "a list has no function called %s", name);
}

static ExValue string_method(Ev *ev, ExValue r, const char *s, const char *name, const ExValue *a, size_t n) {
    ExValue e;
    size_t slen = strlen(s), i;
    if (!strcmp(name, "contains") || !strcmp(name, "startsWith") || !strcmp(name, "endsWith")) {
        const char *q;
        size_t ql;
        e = need(ev, name, n, 1, 1); if (IS_ERR(e)) return e;
        q = tostr(ev, a[0]); ql = strlen(q);
        if (name[0] == 'c') return ex_v_bool(strstr(s, q) != NULL);
        if (name[0] == 's') return ex_v_bool(ql <= slen && !strncmp(s, q, ql));
        return ex_v_bool(ql <= slen && !strcmp(s + slen - ql, q));
    }
    if (!strcmp(name, "containsAll") || !strcmp(name, "containsAny")) {
        int all = name[8] == 'A' && name[9] == 'l';
        for (i = 0; i < n; i++) {
            int found = strstr(s, tostr(ev, a[i])) != NULL;
            if (all && !found) return ex_v_bool(0);
            if (!all && found) return ex_v_bool(1);
        }
        return ex_v_bool(all);
    }
    if (!strcmp(name, "isEmpty")) return ex_v_bool(slen == 0);
    if (!strcmp(name, "lower")) { char *l = lower_copy(ev, s); return l ? str_v(ev, l) : err(ev, "out of memory"); }
    if (!strcmp(name, "title")) {
        char *l = lower_copy(ev, s);
        int start = 1;
        if (!l) return err(ev, "out of memory");
        for (i = 0; l[i]; i++) {
            if (start && isalpha((unsigned char)l[i])) l[i] = (char)toupper((unsigned char)l[i]);
            start = l[i] == ' ' || l[i] == '\t' || l[i] == '\n' || l[i] == '-' || l[i] == '(' || l[i] == '"';
        }
        return str_v(ev, l);
    }
    if (!strcmp(name, "trim")) {
        size_t b = 0, f = slen;
        while (b < f && isspace((unsigned char)s[b])) b++;
        while (f > b && isspace((unsigned char)s[f - 1])) f--;
        return ex_v_strn(ev->a, s + b, f - b);
    }
    if (!strcmp(name, "repeat")) {
        double c;
        char *res;
        size_t k, cnt;
        e = need(ev, name, n, 1, 1); if (IS_ERR(e)) return e;
        if (!to_num(a[0], &c) || c < 0 || c != floor(c)) return err(ev, "repeat() needs a count of zero or more");
        cnt = (size_t)c;
        if (slen && cnt > MAX_STRING / slen) return err(ev, "the text is too long");
        res = ex_arena_alloc(ev->a, slen * cnt + 1);
        if (!res) return err(ev, "out of memory");
        for (k = 0; k < cnt; k++) memcpy(res + k * slen, s, slen);
        return str_v(ev, res);
    }
    if (!strcmp(name, "reverse")) {
        char *res = ex_arena_alloc(ev->a, slen + 1);
        size_t o = slen;
        if (!res) return err(ev, "out of memory");
        for (i = 0; i < slen;) { size_t l = cp_len((unsigned char)s[i]); if (i + l > slen) l = slen - i; o -= l; memcpy(res + o, s + i, l); i += l; }
        return str_v(ev, res);
    }
    if (!strcmp(name, "slice")) {
        size_t from, to, b, f;
        e = need(ev, name, n, 1, 2); if (IS_ERR(e)) return e;
        slice_range(a, n, cp_count(s), &from, &to);
        b = cp_offset(s, from); f = cp_offset(s, to);
        return ex_v_strn(ev->a, s + b, f - b);
    }
    if (!strcmp(name, "replace")) {
        e = need(ev, name, n, 2, 2); if (IS_ERR(e)) return e;
        if (a[0].t != EXV_STR && a[0].t != EXV_REGEXP) return err(ev, "replace() needs text or a regular expression to look for");
        return string_replace(ev, s, a[0], tostr(ev, a[1]));
    }
    if (!strcmp(name, "split")) {
        e = need(ev, name, n, 1, 2); if (IS_ERR(e)) return e;
        if (a[0].t != EXV_STR && a[0].t != EXV_REGEXP) return err(ev, "split() needs text or a regular expression to split at");
        return string_split(ev, s, a[0], n > 1 ? a[1] : ex_v_null());
    }
    (void)r;
    return err(ev, "a string has no function called %s", name);
}

static ExValue number_method(Ev *ev, double x, const char *name, const ExValue *a, size_t n) {
    ExValue e;
    if (!strcmp(name, "abs")) return ex_v_num(fabs(x));
    if (!strcmp(name, "ceil")) return ex_v_num(ceil(x));
    if (!strcmp(name, "floor")) return ex_v_num(floor(x));
    if (!strcmp(name, "isEmpty")) return ex_v_bool(isnan(x));
    if (!strcmp(name, "round")) {
        double d = 0, f;
        e = need(ev, name, n, 0, 1); if (IS_ERR(e)) return e;
        if (n && !to_num(a[0], &d)) return err(ev, "round() needs a number of digits");
        if (d < 0 || d > 15) d = d < 0 ? 0 : 15;
        f = pow(10.0, floor(d));
        return ex_v_num(floor(x * f + 0.5) / f);                                                    /* halves go up, as Math.round does */
    }
    if (!strcmp(name, "toFixed")) {
        double p;
        char b[400];
        e = need(ev, name, n, 1, 1); if (IS_ERR(e)) return e;
        if (!to_num(a[0], &p) || p < 0 || p > 100) return err(ev, "toFixed() needs 0 to 100 digits");
        if (isnan(x) || isinf(x)) { ex_number_to_string(x, b, sizeof b); return str_v(ev, b); }
        if (fabs(x) >= 1e21) { ex_number_to_string(x, b, sizeof b); return str_v(ev, b); }
        snprintf(b, sizeof b, "%.*f", (int)p, x);
        return str_v(ev, b);
    }
    return err(ev, "a number has no function called %s", name);
}

static ExValue date_method(Ev *ev, ExValue r, const char *name, const ExValue *a, size_t n) {
    ExValue e;
    int tz = ev->host ? ev->host->tz_min : 0;
    char b[200];
    if (!strcmp(name, "date")) return date_midnight(ev, r.u.date.ms);
    if (!strcmp(name, "isEmpty")) return ex_v_bool(0);
    if (!strcmp(name, "format")) {
        size_t need_len;
        char *res;
        e = need(ev, name, n, 1, 1); if (IS_ERR(e)) return e;
        if (a[0].t != EXV_STR) return err(ev, "format() needs a format text like YYYY-MM-DD");
        need_len = ex_date_format(NULL, 0, r.u.date.ms, tz, a[0].u.str);
        if (need_len > MAX_STRING) return err(ev, "the text is too long");
        res = ex_arena_alloc(ev->a, need_len + 1);
        if (!res) return err(ev, "out of memory");
        ex_date_format(res, need_len + 1, r.u.date.ms, tz, a[0].u.str);
        return str_v(ev, res);
    }
    if (!strcmp(name, "time")) { ex_date_format(b, sizeof b, r.u.date.ms, tz, "HH:mm:ss"); return str_v(ev, b); }
    if (!strcmp(name, "relative")) return relative_text(ev, r.u.date.ms);
    return err(ev, "a date has no function called %s", name);
}

static ExValue file_method(Ev *ev, ExValue r, const char *name, const ExValue *a, size_t n) {
    const char *path = r.u.file;
    ExValue e;
    size_t i;
    if (!strcmp(name, "asLink")) {
        ExValue l;
        e = need(ev, name, n, 0, 1); if (IS_ERR(e)) return e;
        l = ex_v_link(ev->a, path, NULL, NULL);
        if (n && a[0].t != EXV_NULL) l.u.link.display = tostr(ev, a[0]);
        return l;
    }
    if (!strcmp(name, "hasLink")) {
        char target[700];
        e = need(ev, name, n, 1, 1); if (IS_ERR(e)) return e;
        if (!path_of(ev, a[0], path, target, sizeof target)) return ex_v_bool(0);
        return ex_v_bool(file_links_to(ev, path, target));
    }
    if (!strcmp(name, "hasProperty")) {
        ExValue props;
        e = need(ev, name, n, 1, 1); if (IS_ERR(e)) return e;
        if (a[0].t != EXV_STR || !file_field(ev, path, "properties", &props) || props.t != EXV_OBJECT) return ex_v_bool(0);
        for (i = 0; i < props.u.obj.n; i++) if (!strcmp(props.u.obj.keys[i], a[0].u.str)) return ex_v_bool(1);
        return ex_v_bool(0);
    }
    if (!strcmp(name, "hasTag")) {
        for (i = 0; i < n; i++) {
            if (a[i].t == EXV_LIST) { size_t k; for (k = 0; k < a[i].u.list.n; k++) if (a[i].u.list.v[k].t == EXV_STR && file_tags_has(ev, path, a[i].u.list.v[k].u.str)) return ex_v_bool(1); }
            else if (a[i].t == EXV_STR && file_tags_has(ev, path, a[i].u.str)) return ex_v_bool(1);
        }
        return ex_v_bool(0);
    }
    if (!strcmp(name, "inFolder")) {
        size_t fl;
        char *f;
        e = need(ev, name, n, 1, 1); if (IS_ERR(e)) return e;
        if (a[0].t != EXV_STR) return err(ev, "inFolder() needs a folder name");
        f = ex_arena_strdup(ev->a, a[0].u.str);
        if (!f) return err(ev, "out of memory");
        fl = strlen(f);
        while (fl > 0 && f[fl - 1] == '/') f[--fl] = 0;
        if (!fl) return ex_v_bool(1);
        return ex_v_bool(!strncmp(path, f, fl) && path[fl] == '/');
    }
    return err(ev, "a file has no function called %s", name);
}

static ExValue call_method(Ev *ev, ExValue r, const char *name, const ExValue *a, size_t n) {
    ExValue e;
    const char *text;
    /* what every value can do */
    if (!strcmp(name, "isTruthy")) { e = need(ev, name, n, 0, 0); if (IS_ERR(e)) return e; return ex_v_bool(ex_value_truthy(r)); }
    if (!strcmp(name, "toString")) { e = need(ev, name, n, 0, 0); if (IS_ERR(e)) return e; return str_v(ev, tostr(ev, r)); }
    if (!strcmp(name, "isType")) {
        e = need(ev, name, n, 1, 1); if (IS_ERR(e)) return e;
        if (a[0].t != EXV_STR) return err(ev, "isType() needs the name of a type");
        return ex_v_bool(!strcmp(type_names_match(r), a[0].u.str));
    }
    if (!strcmp(name, "isEmpty") && (r.t == EXV_NULL || r.t == EXV_BOOL || r.t == EXV_LINK || r.t == EXV_URL || r.t == EXV_FILE || r.t == EXV_DURATION || r.t == EXV_REGEXP)) {
        e = need(ev, name, n, 0, 0); if (IS_ERR(e)) return e;
        return ex_v_bool(r.t == EXV_NULL);
    }
    switch (r.t) {
    case EXV_NULL: return ex_v_null();                                                            /* a missing value has no functions: the result is missing too */
    case EXV_STR: case EXV_HTML: case EXV_URL: as_text(r, &text); return string_method(ev, r, text, name, a, n);
    case EXV_NUM: return number_method(ev, r.u.num, name, a, n);
    case EXV_DATE: return date_method(ev, r, name, a, n);
    case EXV_LIST: return list_method(ev, r, name, a, n);
    case EXV_FILE: return file_method(ev, r, name, a, n);
    case EXV_LINK: {
        char path[700];
        if (!strcmp(name, "asFile")) {
            e = need(ev, name, n, 0, 0); if (IS_ERR(e)) return e;
            return resolve_path(ev, ev->host ? ev->host->file_path : "", r.u.link.path, path, sizeof path) ? ex_v_file(ev->a, path) : ex_v_null();
        }
        if (!strcmp(name, "linksTo")) {
            char target[700];
            e = need(ev, name, n, 1, 1); if (IS_ERR(e)) return e;
            if (!resolve_path(ev, ev->host ? ev->host->file_path : "", r.u.link.path, path, sizeof path)) return ex_v_bool(0);
            if (!path_of(ev, a[0], path, target, sizeof target)) return ex_v_bool(0);
            return ex_v_bool(file_links_to(ev, path, target));
        }
        return err(ev, "a link has no function called %s", name);
    }
    case EXV_OBJECT:
        if (!strcmp(name, "isEmpty")) return ex_v_bool(r.u.obj.n == 0);
        if (!strcmp(name, "keys")) { size_t i; ExValue *k = r.u.obj.n ? ex_arena_alloc(ev->a, r.u.obj.n * sizeof *k) : NULL; if (r.u.obj.n && !k) return err(ev, "out of memory"); for (i = 0; i < r.u.obj.n; i++) k[i] = str_v(ev, r.u.obj.keys[i]); return make_list(ev, k, r.u.obj.n); }
        if (!strcmp(name, "values")) return make_list(ev, r.u.obj.v, r.u.obj.n);
        return err(ev, "an object has no function called %s", name);
    case EXV_REGEXP:
        if (!strcmp(name, "matches")) {
            Re re;
            char eb[160];
            pcre2_match_data *md;
            pcre2_match_context *mc;
            int rc;
            const char *s;
            e = need(ev, name, n, 1, 1); if (IS_ERR(e)) return e;
            s = tostr(ev, a[0]);
            if (re_compile(r, &re, eb, sizeof eb)) return err(ev, "%s", eb);
            md = pcre2_match_data_create_from_pattern(re.code, NULL);
            mc = re_limits();
            rc = pcre2_match(re.code, (PCRE2_SPTR)s, strlen(s), 0, 0, md, mc);
            pcre2_match_data_free(md); pcre2_match_context_free(mc); pcre2_code_free(re.code);
            if (rc < 0 && rc != PCRE2_ERROR_NOMATCH) return err(ev, "the regular expression could not be run on this text");
            return ex_v_bool(rc >= 0);
        }
        return err(ev, "a regular expression has no function called %s", name);
    default: return err(ev, "a %s has no function called %s", ex_value_type_name(r), name);
    }
}

/* ---- the evaluator ---- */

static ExValue eval_ident(Ev *ev, const char *name) {
    Scope *sc;
    ExValue out;
    const ExExprHost *h = ev->host;
    int i;
    for (sc = ev->scope; sc; sc = sc->parent) for (i = 0; i < sc->n; i++) if (!strcmp(sc->name[i], name)) return sc->val[i];
    if (ev->values && !strcmp(name, "values")) return *ev->values;
    if (!strcmp(name, "file")) return h && h->file_path ? ex_v_file(ev->a, h->file_path) : ex_v_null();
    if (!strcmp(name, "this")) return h && h->this_path ? ex_v_file(ev->a, h->this_path) : ex_v_null();
    if (!strcmp(name, "note")) { ExValue v = ex_v_null(); v.t = EXV_NS; v.u.ns = 0; return v; }
    if (!strcmp(name, "formula")) { ExValue v = ex_v_null(); v.t = EXV_NS; v.u.ns = 1; return v; }
    if (h && h->note_prop && h->note_prop(h->user, ev->a, name, &out)) return out;
    return ex_v_null();
}

static ExValue eval_call(Ev *ev, const Node *n) {
    const Node *callee = n->a;
    ExValue recv = ex_v_null(), *args, r;
    int has_recv = callee->t == N_MEMBER;
    const char *name = callee->s;
    size_t i;
    if (has_recv) { recv = eval(ev, callee->a); if (IS_ERR(recv)) return recv; }
    /* the forms whose arguments are not evaluated first */
    if (!has_recv && !strcmp(name, "if")) {
        ExValue c;
        if (n->nargs < 2 || n->nargs > 3) return err(ev, "if() takes a condition, a result and an optional other result");
        c = eval(ev, n->args[0]);
        if (IS_ERR(c)) return c;
        if (ex_value_truthy(c)) return eval(ev, n->args[1]);
        return n->nargs == 3 ? eval(ev, n->args[2]) : ex_v_null();
    }
    if (has_recv && recv.t == EXV_LIST && (!strcmp(name, "filter") || !strcmp(name, "map") || !strcmp(name, "reduce"))) {
        int reduce = name[0] == 'r', is_filter = name[0] == 'f';
        ExValue acc = ex_v_null(), *out;
        size_t cnt = 0;
        Scope sc;
        if ((reduce && n->nargs != 2) || (!reduce && n->nargs != 1)) return err(ev, "%s() takes %s", name, reduce ? "an expression and a start value" : "an expression");
        if (reduce) { acc = eval(ev, n->args[1]); if (IS_ERR(acc)) return acc; }
        out = recv.u.list.n && !reduce ? ex_arena_alloc(ev->a, recv.u.list.n * sizeof *out) : NULL;
        if (recv.u.list.n && !reduce && !out) return err(ev, "out of memory");
        sc.parent = ev->scope; sc.n = reduce ? 3 : 2;
        sc.name[0] = "value"; sc.name[1] = "index"; sc.name[2] = "acc";
        ev->scope = &sc;
        for (i = 0; i < recv.u.list.n; i++) {
            ExValue v;
            sc.val[0] = recv.u.list.v[i]; sc.val[1] = ex_v_num((double)i); sc.val[2] = acc;
            v = eval(ev, n->args[0]);
            if (IS_ERR(v)) { ev->scope = sc.parent; return v; }
            if (reduce) acc = v;
            else if (is_filter) { if (ex_value_truthy(v)) out[cnt++] = recv.u.list.v[i]; }
            else out[cnt++] = v;
        }
        ev->scope = sc.parent;
        return reduce ? acc : make_list(ev, out, cnt);
    }
    args = n->nargs ? ex_arena_alloc(ev->a, n->nargs * sizeof *args) : NULL;
    if (n->nargs && !args) return err(ev, "out of memory");
    for (i = 0; i < n->nargs; i++) { args[i] = eval(ev, n->args[i]); if (IS_ERR(args[i])) return args[i]; }
    if (!has_recv) return call_global(ev, name, args, n->nargs);
    r = call_method(ev, recv, name, args, n->nargs);
    return r;
}

static ExValue eval(Ev *ev, const Node *n) {
    ExValue a, b;
    if (++ev->steps > MAX_STEPS) return err(ev, "the expression takes too long");
    if (ev->depth > MAX_DEPTH) return err(ev, "the expression is nested too deeply");
    ev->depth++;
    switch (n->t) {
    case N_NUM: a = ex_v_num(n->num); break;
    case N_STR: a = str_v(ev, n->s); break;
    case N_BOOL: a = ex_v_bool(n->num != 0); break;
    case N_NULL: a = ex_v_null(); break;
    case N_ERROR: a = err(ev, "%s", n->s); break;
    case N_REGEX: a = ex_v_null(); a.t = EXV_REGEXP; a.u.re.pattern = ex_arena_strdup(ev->a, n->s); a.u.re.flags = ex_arena_strdup(ev->a, n->flags ? n->flags : ""); break;
    case N_LIST: {
        size_t i;
        ExValue *items = n->nargs ? ex_arena_alloc(ev->a, n->nargs * sizeof *items) : NULL;
        if (n->nargs && !items) { a = err(ev, "out of memory"); break; }
        a = ex_v_null();
        for (i = 0; i < n->nargs; i++) { items[i] = eval(ev, n->args[i]); if (IS_ERR(items[i])) { a = items[i]; break; } }
        if (!IS_ERR(a)) a = make_list(ev, items, n->nargs);
        break;
    }
    case N_IDENT: a = eval_ident(ev, n->s); break;
    case N_MEMBER: b = eval(ev, n->a); a = IS_ERR(b) ? b : member(ev, b, n->s); break;
    case N_INDEX: { ExValue idx; b = eval(ev, n->a); if (IS_ERR(b)) { a = b; break; } idx = eval(ev, n->b); a = IS_ERR(idx) ? idx : index_of(ev, b, idx); break; }
    case N_CALL: a = eval_call(ev, n); break;
    case N_UNARY:
        b = eval(ev, n->a);
        if (IS_ERR(b)) { a = b; break; }
        if (n->op == OP_NOT) a = ex_v_bool(!ex_value_truthy(b));
        else if (b.t == EXV_NUM) a = ex_v_num(-b.u.num);
        else if (b.t == EXV_DURATION) a = ex_v_duration(-b.u.dur.ms, -b.u.dur.months);
        else if (b.t == EXV_NULL) a = b;
        else a = err(ev, "a %s cannot be negated", ex_value_type_name(b));
        break;
    case N_BINARY:
        if (n->op == OP_AND || n->op == OP_OR) {
            b = eval(ev, n->a);
            if (IS_ERR(b)) { a = b; break; }
            if (n->op == OP_AND ? !ex_value_truthy(b) : ex_value_truthy(b)) { a = b; break; }
            a = eval(ev, n->b);
            break;
        }
        b = eval(ev, n->a);
        if (IS_ERR(b)) { a = b; break; }
        {
            ExValue c = eval(ev, n->b);
            if (n->op == OP_ADD || n->op == OP_SUB || n->op == OP_MUL || n->op == OP_DIV || n->op == OP_MOD) a = arith(ev, n->op, b, c);
            else a = compare_op(ev, n->op, b, c);
        }
        break;
    default: a = err(ev, "unknown expression"); break;
    }
    ev->depth--;
    return a;
}

ExExpr *ex_expr_failed(const char *message) {
    ExExpr *e = calloc(1, sizeof *e);
    Node *n;
    if (!e) return NULL;
    n = node_new(e, N_ERROR);
    if (!n) { free(e); return NULL; }
    n->s = malloc(strlen(message) + 1);
    if (!n->s) { ex_expr_free(e); return NULL; }
    strcpy(n->s, message);
    e->root = n;
    return e;
}

ExValue ex_expr_eval(const ExExpr *e, ExArena *a, const ExExprHost *host, const ExValue *values) {
    Ev ev;
    ExValue r;
    memset(&ev, 0, sizeof ev);
    ev.a = a; ev.host = host; ev.values = values;
    r = eval(&ev, e->root);
    if (r.t == EXV_NS) return ex_v_error(a, "`%s` needs a property name after it (like %s.name)", r.u.ns ? "formula" : "note", r.u.ns ? "formula" : "note");
    return r;
}

ExValue ex_expr_run(const char *text, ExArena *a, const ExExprHost *host, const ExValue *values) {
    char errbuf[200];
    int pos = 0;
    ExExpr *e = ex_expr_parse(text, errbuf, sizeof errbuf, &pos);
    ExValue r;
    if (!e) return ex_v_error(a, "%s (at character %d)", errbuf, pos + 1);
    r = ex_expr_eval(e, a, host, values);
    ex_expr_free(e);
    return r;
}
