/* canvas (6a): the JSON Canvas model. A sample canvas round-trips unchanged (byte for byte in the layout the JSON Canvas samples use, and with the
 * same content in any other layout), keys this version does not know are kept with their order, edits write what they should, damaged input never
 * crashes and never produces a canvas, and the geometry the view relies on is right.
 * Usage: test_canvas <case> <sample.canvas>   (cases: roundtrip, layouts, unknown, edit, bad_input, geometry, ids) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_canvas.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static char *read_all(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    char *b;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) { b[n] = 0; *len = (size_t)n; }
    return b;
}

static ExCanvas *parse(const char *s) { char err[200]; ExCanvas *c = ex_canvas_parse(s, strlen(s), err, sizeof err); if (!c) fprintf(stderr, "parse failed: %s\n", err); return c; }
static int has(const char *hay, const char *needle) { return strstr(hay, needle) != NULL; }

static void roundtrip(const char *path) {
    size_t len = 0, olen = 0;
    char *text = read_all(path, &len), *out, *again;
    ExCanvas *c;
    char err[200];
    CHECK(text != NULL);
    if (!text) return;
    c = ex_canvas_parse(text, len, err, sizeof err);
    CHECK(c != NULL);
    if (!c) { free(text); return; }
    CHECK(c->n == 7 && c->ne == 4);
    CHECK(!c->repaired);
    out = ex_canvas_write(c, &olen);
    CHECK(out != NULL && olen == len && memcmp(out, text, len) == 0);              /* byte for byte */
    if (out && (olen != len || memcmp(out, text, len))) fprintf(stderr, "--- written ---\n%s\n", out);
    /* the kinds and members were read */
    CHECK(c->nodes[0].kind == EX_CN_GROUP && !strcmp(c->nodes[0].label, "Reading list") && !strcmp(c->nodes[0].color, "4"));
    CHECK(c->nodes[1].kind == EX_CN_TEXT && has(c->nodes[1].text, "\"quoted\"") && has(c->nodes[1].text, "\n\ttabbed"));
    CHECK(c->nodes[2].kind == EX_CN_FILE && !strcmp(c->nodes[2].file, "Notes/Home.md") && !strcmp(c->nodes[2].subpath, "#Welcome") && !strcmp(c->nodes[2].color, "#ff8800"));
    CHECK(c->nodes[3].kind == EX_CN_LINK && !strcmp(c->nodes[3].url, "https://jsoncanvas.org/spec/1.0/"));
    CHECK(c->nodes[6].kind == EX_CN_OTHER && !strcmp(c->nodes[6].type, "sticker"));
    CHECK(c->nodes[1].x == -440 && c->nodes[1].y == -240 && c->nodes[1].w == 300 && c->nodes[1].h == 120);
    CHECK(c->edges[0].from_side == EX_CS_RIGHT && c->edges[0].to_side == EX_CS_LEFT && !strcmp(c->edges[0].label, "see also"));
    CHECK(c->edges[1].from_end == EX_CE_ARROW && c->edges[1].to_end == EX_CE_NONE);
    CHECK(c->edges[2].from_side == EX_CS_NONE && c->edges[2].to_end == EX_CE_DEFAULT);
    /* writing twice gives the same text (nothing in the model changes by being written) */
    again = ex_canvas_write(c, NULL);
    CHECK(again && out && !strcmp(again, out));
    free(again); free(out);
    ex_canvas_free(c);
    free(text);
}

/* The same canvas in other layouts: the content is the same and the layout becomes the standard one, stably. */
static void layouts(const char *path) {
    size_t len = 0, wlen = 0, i;
    char *text = read_all(path, &len), *std, *compact, *spaced, *reparsed_text;
    ExCanvas *a, *b, *c2, *d;
    char *p;
    CHECK(text != NULL);
    if (!text) return;
    a = ex_canvas_parse(text, len, NULL, 0);
    std = ex_canvas_write(a, &wlen);
    /* compact: every tab, newline and indentation outside strings removed */
    compact = malloc(len + 1);
    {
        size_t o = 0; int in_str = 0;
        for (i = 0; i < len; i++) {
            char ch = text[i];
            if (in_str) { compact[o++] = ch; if (ch == '\\') compact[o++] = text[++i]; else if (ch == '"') in_str = 0; continue; }
            if (ch == '"') { in_str = 1; compact[o++] = ch; continue; }
            if (ch == '\n' || ch == '\t') continue;
            compact[o++] = ch;
        }
        compact[o] = 0;
    }
    CHECK(strlen(compact) < len);
    b = parse(compact);
    CHECK(b != NULL);
    if (b) {
        char *w = ex_canvas_write(b, NULL);
        CHECK(w && !strcmp(w, std));                                              /* same content, same layout out */
        free(w);
        CHECK(!b->final_newline);
    }
    /* spaces and a trailing newline: also the same content; the final newline stays */
    spaced = malloc(len * 3 + 8);
    {
        size_t o = 0; int in_str = 0;
        for (i = 0; i < len; i++) {
            char ch = text[i];
            if (in_str) { spaced[o++] = ch; if (ch == '\\') spaced[o++] = text[++i]; else if (ch == '"') in_str = 0; continue; }
            if (ch == '"') in_str = 1;
            if (ch == '\t') { spaced[o++] = ' '; spaced[o++] = ' '; continue; }
            spaced[o++] = ch;
            if (ch == ':' || ch == ',') spaced[o++] = ' ';
        }
        spaced[o++] = '\n'; spaced[o] = 0;
    }
    c2 = parse(spaced);
    CHECK(c2 && c2->final_newline);
    if (c2) {
        char *w = ex_canvas_write(c2, &wlen);
        CHECK(w && strncmp(w, std, strlen(std)) == 0 && w[strlen(std)] == '\n' && w[strlen(std) + 1] == 0);
        free(w);
    }
    /* once written, a canvas does not change again */
    reparsed_text = ex_canvas_write(a, NULL);
    d = parse(reparsed_text);
    p = d ? ex_canvas_write(d, NULL) : NULL;
    CHECK(p && !strcmp(p, reparsed_text));
    free(p); ex_canvas_free(d); free(reparsed_text);
    ex_canvas_free(a); ex_canvas_free(b); ex_canvas_free(c2);
    free(std); free(compact); free(spaced); free(text);
}

static void unknown(void) {
    /* keys in an order of their own, keys of every unknown kind, numbers written in other ways, a type this version does not draw */
    const char *src =
        "{\n\t\"version\":2,\n\t\"edges\":[\n\t\t{\"toNode\":\"b\",\"x-note\":[1,{\"a\":null}],\"id\":\"e1\",\"fromNode\":\"a\"}\n\t],\n\t\"nodes\":[\n"
        "\t\t{\"height\":60,\"width\":250,\"type\":\"text\",\"text\":\"one\",\"y\":10,\"x\":1e2,\"id\":\"a\",\"pin\":true,\"weird\":{\"k\":[1.0,2.50,-0.0]}},\n"
        "\t\t{\"id\":\"b\",\"type\":\"text\",\"text\":\"two\",\"x\":400,\"y\":10,\"width\":250,\"height\":60},\n"
        "\t\t42,\n\t\t\"a string\",\n\t\tnull\n\t],\n\t\"zz\":{\"deep\":[[[[[1]]]]]}\n}";
    ExCanvas *c = parse(src);
    char *out;
    CHECK(c != NULL);
    if (!c) return;
    CHECK(c->n == 5 && c->ne == 1);
    CHECK(c->nodes[0].x == 100);                                                  /* 1e2 is a number */
    CHECK(c->nodes[2].raw && !strcmp(c->nodes[2].raw, "42"));                      /* an element that is not an object is kept */
    out = ex_canvas_write(c, NULL);
    CHECK(out && !strcmp(out, src));                                              /* order of keys, unknown keys, raw numbers, odd elements: all as they were */
    if (out && strcmp(out, src)) fprintf(stderr, "--- written ---\n%s\n--- source ---\n%s\n", out, src);
    free(out);
    /* an edit of a known member keeps every unknown key and the order */
    ex_canvas_set_string(c, &c->nodes[0].text, "changed \"text\"\nline");
    out = ex_canvas_write(c, NULL);
    CHECK(out && has(out, "\"text\":\"changed \\\"text\\\"\\nline\"") && has(out, "\"pin\":true") && has(out, "\"weird\":{\"k\":[1.0,2.50,-0.0]}") && has(out, "\"zz\":{\"deep\":[[[[[1]]]]]}") && has(out, "42,\n\t\t\"a string\",\n\t\tnull"));
    CHECK(out && strstr(out, "\"version\"") < strstr(out, "\"edges\"") && strstr(out, "\"edges\"") < strstr(out, "\"nodes\""));
    free(out);
    ex_canvas_free(c);
    /* a file with neither array stays without them until something is added */
    c = parse("{\"meta\":1}");
    CHECK(c && !c->has_nodes && !c->has_edges);
    out = c ? ex_canvas_write(c, NULL) : NULL;
    CHECK(out && !strcmp(out, "{\n\t\"meta\":1\n}"));
    free(out);
    if (c) { ex_canvas_add_node(c, EX_CN_TEXT, 0, 0, 100, 50); out = ex_canvas_write(c, NULL); CHECK(out && has(out, "\"nodes\":[") && has(out, "\"meta\":1")); free(out); }
    ex_canvas_free(c);
    /* an empty object */
    c = parse("{}");
    out = c ? ex_canvas_write(c, NULL) : NULL;
    CHECK(c && out && !strcmp(out, "{}"));
    free(out); ex_canvas_free(c);
    c = ex_canvas_new();
    out = ex_canvas_write(c, NULL);
    CHECK(out && !strcmp(out, "{\n\t\"nodes\":[],\n\t\"edges\":[]\n}"));
    free(out); ex_canvas_free(c);
}

static void edit(void) {
    ExCanvas *c = ex_canvas_new(), *d;
    int a, b, g, e;
    unsigned long rev;
    char *out;
    ex_canvas_set_seed(12345);
    a = ex_canvas_add_node(c, EX_CN_TEXT, 0, 0, 200, 100);
    b = ex_canvas_add_node(c, EX_CN_FILE, 300, 0, 200, 100);
    g = ex_canvas_add_node(c, EX_CN_GROUP, -50, -50, 600, 300);
    CHECK(a == 0 && b == 1 && g == 2 && c->n == 3);
    CHECK(strlen(c->nodes[0].id) == 16 && strcmp(c->nodes[0].id, c->nodes[1].id) && strcmp(c->nodes[1].id, c->nodes[2].id));
    ex_canvas_set_string(c, &c->nodes[0].text, "Hello \\ world");
    ex_canvas_set_string(c, &c->nodes[1].file, "Notes/A.md");
    ex_canvas_set_string(c, &c->nodes[2].label, "Group");
    ex_canvas_set_string(c, &c->nodes[0].color, "3");
    e = ex_canvas_add_edge(c, 0, EX_CS_RIGHT, 1, EX_CS_LEFT);
    CHECK(e == 0 && c->ne == 1 && !strcmp(c->edges[0].from, c->nodes[0].id));
    ex_canvas_set_string(c, &c->edges[0].label, "next");
    rev = c->rev;
    out = ex_canvas_write(c, NULL);
    CHECK(c->rev == rev);                                                            /* writing is not a change */
    d = parse(out);
    CHECK(d && d->n == 3 && d->ne == 1);
    if (d) {
        CHECK(!strcmp(d->nodes[0].text, "Hello \\ world") && !strcmp(d->nodes[0].color, "3") && !strcmp(d->nodes[1].file, "Notes/A.md") && !strcmp(d->nodes[2].label, "Group"));
        CHECK(!strcmp(d->edges[0].label, "next") && d->edges[0].from_side == EX_CS_RIGHT && d->edges[0].to_side == EX_CS_LEFT);
        CHECK(d->nodes[0].w == 200 && d->nodes[2].x == -50);
    }
    free(out); ex_canvas_free(d);
    /* raising a card puts it last; removing a card removes its edges */
    CHECK(ex_canvas_raise_node(c, 0) == 2);
    CHECK(!strcmp(c->nodes[2].text, "Hello \\ world") && c->nodes[0].kind == EX_CN_FILE);
    CHECK(ex_canvas_raise_node(c, 2) == 2);
    CHECK(c->ne == 1);
    CHECK(ex_canvas_remove_node(c, 2) == 0);
    CHECK(c->n == 2 && c->ne == 0);                                                  /* the edge went with the card */
    CHECK(ex_canvas_remove_node(c, 9) == -1 && ex_canvas_remove_edge(c, 0) == -1 && ex_canvas_add_edge(c, 0, 0, 9, 0) == -1);
    CHECK(ex_canvas_add_node(c, EX_CN_OTHER, 0, 0, 1, 1) == -1);
    CHECK(ex_canvas_find_node(c, c->nodes[1].id) == 1 && ex_canvas_find_node(c, "nope") == -1 && ex_canvas_find_node(c, NULL) == -1);
    ex_canvas_free(c);
}

static void bad_input(void) {
    static const char *const bad[] = {
        "", " ", "{", "}", "[]", "null", "42", "\"x\"", "{\"nodes\":", "{\"nodes\":[{\"id\":\"a\",", "{\"nodes\":[1,2,3]} trailing", "nan", "{'a':1}",
        "{\"nodes\":[{\"id\":\"a\",\"x\":\"a string\"}]}x", "\xff\xfe{}", "{\"a\":\"\xc3\x28\"}", "{\"nodes\":[{\"id\":\"\\ud800\"}]}",
    };
    static const char *const odd[] = {
        "{\"nodes\":\"no\",\"edges\":5}", "{\"nodes\":[{}]}", "{\"nodes\":[{\"id\":7,\"type\":9,\"x\":\"1\",\"width\":null,\"text\":[],\"color\":false}]}",
        "{\"nodes\":[{\"id\":\"a\",\"type\":\"text\"},{\"id\":\"a\",\"type\":\"text\"},{\"type\":\"text\"}]}", "{\"edges\":[{},{\"fromNode\":1,\"fromSide\":\"diagonal\",\"toEnd\":\"loop\"}]}",
        "{\"nodes\":[{\"id\":\"a\",\"x\":1e400,\"y\":-1e999,\"width\":1e308,\"height\":0.0000001}]}", "{\"nodes\":[],\"nodes\":[],\"edges\":[],\"edges\":[],\"x\":1,\"x\":2}",
        "{\"nodes\":[{\"id\":\"a\",\"id\":\"b\",\"text\":\"\\u0000nul\",\"text\":\"again\"}]}",
    };
    size_t i;
    char err[200];
    for (i = 0; i < sizeof bad / sizeof *bad; i++) {
        size_t len = strlen(bad[i]);
        ExCanvas *c = ex_canvas_parse(bad[i], len, err, sizeof err);
        if (i == 14) { c = ex_canvas_parse(bad[i], 5, err, sizeof err); }                  /* a byte-order mark is the caller's job: here it is not JSON */
        if (c) { fprintf(stderr, "bad input %zu was accepted: %s\n", i, bad[i]); failures++; ex_canvas_free(c); }
        else CHECK(err[0] != 0);
    }
    CHECK(ex_canvas_parse(NULL, 0, err, sizeof err) == NULL);
    CHECK(ex_canvas_parse("", 0, NULL, 0) == NULL);
    for (i = 0; i < sizeof odd / sizeof *odd; i++) {
        ExCanvas *c = parse(odd[i]);
        char *out;
        ExCanvas *d;
        CHECK(c != NULL);
        if (!c) continue;
        out = ex_canvas_write(c, NULL);
        d = out ? ex_canvas_parse(out, strlen(out), NULL, 0) : NULL;
        CHECK(d != NULL);                                                         /* what is written can be read */
        if (d) {
            char *o2 = ex_canvas_write(d, NULL);
            CHECK(o2 && !strcmp(o2, out));
            free(o2);
        }
        /* the geometry never trips over strange cards */
        { double x0, y0, x1, y1; (void)ex_canvas_bounds(c, &x0, &y0, &x1, &y1); (void)ex_canvas_node_at(c, 0, 0); }
        free(out); ex_canvas_free(d); ex_canvas_free(c);
    }
    {   /* repeated and missing ids are replaced so that every card has its own */
        ExCanvas *c = parse(odd[3]);
        CHECK(c && c->n == 3 && c->repaired);
        if (c) CHECK(strcmp(c->nodes[0].id, c->nodes[1].id) && strcmp(c->nodes[1].id, c->nodes[2].id) && c->nodes[2].id[0]);
        ex_canvas_free(c);
    }
    {   /* a deeply nested value is kept and written back (the reader has no recursion to run out of) */
        size_t n = 20000, k;
        char *deep = malloc(2 * n + 64), *out;
        ExCanvas *c;
        strcpy(deep, "{\"deep\":");
        for (k = 0; k < n; k++) deep[8 + k] = '[';
        for (k = 0; k < n; k++) deep[8 + n + k] = ']';
        strcpy(deep + 8 + 2 * n, "}");
        c = ex_canvas_parse(deep, strlen(deep), err, sizeof err);
        CHECK(c != NULL);
        if (c) { out = ex_canvas_write(c, NULL); CHECK(out && has(out, "[[[[[[]]]]]]")); free(out); }
        ex_canvas_free(c); free(deep);
    }
}

static void geometry(void) {
    ExCanvas *c = ex_canvas_new();
    double x0, y0, x1, y1, ex1, ey1, ex2, ey2;
    int g, a, b, c2, s1, s2;
    size_t members[8];
    ExCanvasEdge *e;
    uint8_t rgb[3];
    CHECK(!ex_canvas_bounds(c, &x0, &y0, &x1, &y1) && ex_canvas_node_at(c, 0, 0) == -1);
    g = ex_canvas_add_node(c, EX_CN_GROUP, 0, 0, 500, 400);
    a = ex_canvas_add_node(c, EX_CN_TEXT, 20, 40, 100, 50);
    b = ex_canvas_add_node(c, EX_CN_TEXT, 300, 200, 100, 50);
    c2 = ex_canvas_add_node(c, EX_CN_TEXT, 600, 100, 100, 50);
    CHECK(ex_canvas_bounds(c, &x0, &y0, &x1, &y1) && x0 == 0 && y0 == 0 && x1 == 700 && y1 == 400);
    CHECK(ex_canvas_node_at(c, 30, 50) == a && ex_canvas_node_at(c, 310, 210) == b && ex_canvas_node_at(c, 250, 350) == g);   /* a card wins over the group it lies in */
    CHECK(ex_canvas_node_at(c, 550, 350) == -1 && ex_canvas_node_at(c, 650, 120) == c2 && ex_canvas_node_at(c, -1, 0) == -1);
    CHECK(ex_canvas_group_members(c, (size_t)g, members, 8) == 2 && members[0] == (size_t)a && members[1] == (size_t)b);
    CHECK(ex_canvas_group_members(c, (size_t)a, members, 8) == 0);
    /* a card laid over another: the one above is hit */
    {
        int top = ex_canvas_add_node(c, EX_CN_TEXT, 10, 30, 200, 100);
        CHECK(ex_canvas_node_at(c, 30, 50) == top);
        CHECK(ex_canvas_raise_node(c, (size_t)a) >= 0 && ex_canvas_node_at(c, 30, 50) == (int)c->n - 1);
    }
    /* edges: the file's sides, and sides that face each other when the file does not say */
    a = ex_canvas_find_node(c, c->nodes[1].id);
    c->nodes[0].x = 0; /* the group */
    ex_canvas_add_edge(c, 1, EX_CS_NONE, 2, EX_CS_NONE);
    e = &c->edges[0];
    CHECK(ex_canvas_edge_ends(c, e, &ex1, &ey1, &s1, &ex2, &ey2, &s2));
    CHECK(s1 >= 0 && s2 >= 0 && s1 != s2);
    e->from_side = EX_CS_TOP; e->to_side = EX_CS_LEFT;
    CHECK(ex_canvas_edge_ends(c, e, &ex1, &ey1, &s1, &ex2, &ey2, &s2) && s1 == EX_CS_TOP && s2 == EX_CS_LEFT);
    CHECK(ex1 == c->nodes[1].x + c->nodes[1].w / 2 && ey1 == c->nodes[1].y && ex2 == c->nodes[2].x && ey2 == c->nodes[2].y + c->nodes[2].h / 2);
    free(e->to); e->to = malloc(8); memcpy(e->to, "missing", 8);
    CHECK(!ex_canvas_edge_ends(c, e, &ex1, &ey1, &s1, &ex2, &ey2, &s2));
    CHECK(ex_canvas_nearest_side(&c->nodes[1], c->nodes[1].x + 500, c->nodes[1].y + 20) == EX_CS_RIGHT);
    CHECK(ex_canvas_nearest_side(&c->nodes[1], c->nodes[1].x - 500, c->nodes[1].y + 20) == EX_CS_LEFT);
    CHECK(ex_canvas_nearest_side(&c->nodes[1], c->nodes[1].x + 50, c->nodes[1].y + 900) == EX_CS_BOTTOM);
    CHECK(ex_canvas_nearest_side(&c->nodes[1], c->nodes[1].x + 50, c->nodes[1].y - 900) == EX_CS_TOP);
    /* colors */
    CHECK(ex_canvas_color("1", rgb) == 1 && ex_canvas_color("6", rgb) == 6 && ex_canvas_color("7", rgb) == -1 && ex_canvas_color("", rgb) == -1 && ex_canvas_color(NULL, rgb) == -1);
    CHECK(ex_canvas_color("#ff8800", rgb) == 0 && rgb[0] == 255 && rgb[1] == 136 && rgb[2] == 0);
    CHECK(ex_canvas_color("#f80", rgb) == 0 && rgb[0] == 255 && rgb[1] == 136 && rgb[2] == 0);
    CHECK(ex_canvas_color("#ff88", rgb) == -1 && ex_canvas_color("ff8800", rgb) == -1 && ex_canvas_color("#gg0000", rgb) == -1);
    ex_canvas_free(c);
}

static void ids(void) {
    ExCanvas *c = ex_canvas_new();
    size_t i, j;
    char id[17];
    ex_canvas_set_seed(1);
    for (i = 0; i < 2000; i++) ex_canvas_add_node(c, EX_CN_TEXT, (double)i, 0, 10, 10);
    for (i = 0; i < c->n; i++) {
        size_t k;
        CHECK(strlen(c->nodes[i].id) == 16);
        for (k = 0; k < 16; k++) CHECK((c->nodes[i].id[k] >= '0' && c->nodes[i].id[k] <= '9') || (c->nodes[i].id[k] >= 'a' && c->nodes[i].id[k] <= 'f'));
    }
    for (i = 0; i < c->n; i += 97) for (j = i + 1; j < c->n; j++) CHECK(strcmp(c->nodes[i].id, c->nodes[j].id) != 0);
    /* the same seed gives the same ids (a test can rely on it); an id already in the canvas is never given again */
    ex_canvas_set_seed(1);
    ex_canvas_new_id(NULL, id);
    CHECK(!strcmp(id, c->nodes[0].id));
    ex_canvas_set_seed(1);
    ex_canvas_new_id(c, id);
    CHECK(strcmp(id, c->nodes[0].id) != 0);
    ex_canvas_free(c);
}

int main(int argc, char **argv) {
    const char *which = argc > 1 ? argv[1] : "";
    if (!strcmp(which, "roundtrip") && argc > 2) roundtrip(argv[2]);
    else if (!strcmp(which, "layouts") && argc > 2) layouts(argv[2]);
    else if (!strcmp(which, "unknown")) unknown();
    else if (!strcmp(which, "edit")) edit();
    else if (!strcmp(which, "bad_input")) bad_input();
    else if (!strcmp(which, "geometry")) geometry();
    else if (!strcmp(which, "ids")) ids();
    else { fprintf(stderr, "usage: test_canvas roundtrip|layouts <sample.canvas> | unknown | edit | bad_input | geometry | ids\n"); return 2; }
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    return 0;
}
