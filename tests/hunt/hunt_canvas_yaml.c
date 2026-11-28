/* hunt_canvas_yaml: JSON Canvas files (include/exhaust_canvas.h) and the YAML tree behind .base files (include/exhaust_yaml.h).
 *
 * Both are files the program rewrites after a user edit, and both promise "nothing the file says is lost": unknown keys, the order of keys, the
 * style of values and, for a canvas that is already in the usual layout, every byte. This test
 *   A. writes a canvas, reads it, writes it again and expects the same bytes; plants unknown keys at every level and expects them back; reads
 *      hostile JSON (BOM, trailing text, duplicate keys, ids repeated or missing, numbers like 1e999 and -0, lone surrogates, NUL in a string, nesting
 *      a hundred thousand deep) and requires that whatever was accepted is written as JSON the reader accepts again, to the same text;
 *   B. runs random editing sequences (add, remove, raise, edges) and checks the ids stay unique, no edge points at a removed card, and
 *      find_node agrees with the order;
 *   C. compares hit testing, group membership, side points and edge ends with brute-force versions of the documented rules, and ids and colors
 *      with their documented forms;
 *   D. for YAML, round-trips random strings through the emitter and the parser (the text must come back exactly, and text that YAML would read as a
 *      number, a boolean or null must be quoted), checks the writer is a fixed point on its own output, keeps flow/block styles, anchors and aliases
 *      (without expanding them: a "billion laughs" file must stay small), and survives nesting that is deeper than the stack.
 * Link: exhaust_canvas exhaust_base. */
#include "hunt.h"

#include <math.h>

#include "exhaust_canvas.h"
#include "exhaust_yaml.h"

static char *write_canvas(const ExCanvas *c, size_t *n) { size_t l = 0; char *t = ex_canvas_write(c, &l); if (n) *n = l; return t; }

/* ---- A. files ---- */

static const char *const SAMPLE =
    "{\n\t\"nodes\":[\n"
    "\t\t{\"id\":\"8c2f1a9b0d3e4f51\",\"type\":\"group\",\"x\":-480,\"y\":-300,\"width\":820,\"height\":520,\"label\":\"Reading list\",\"color\":\"4\"},\n"
    "\t\t{\"id\":\"a1b2c3d4e5f60718\",\"type\":\"text\",\"x\":-440,\"y\":-240,\"width\":300,\"height\":120,\"text\":\"# Ideas\\n\\n- a **bold** one\\n\\ttabbed \\\"q\\\" \xC3\xA9\"},\n"
    "\t\t{\"id\":\"0f1e2d3c4b5a6978\",\"type\":\"file\",\"x\":-60,\"y\":-240,\"width\":340,\"height\":200,\"file\":\"Notes/Home.md\",\"subpath\":\"#Welcome\",\"color\":\"#ff8800\"},\n"
    "\t\t{\"id\":\"5e4d3c2b1a090807\",\"type\":\"link\",\"x\":-440,\"y\":-60,\"width\":300,\"height\":120,\"url\":\"https://jsoncanvas.org/spec/1.0/\"},\n"
    "\t\t{\"id\":\"c0ffee00c0ffee01\",\"type\":\"text\",\"x\":420,\"y\":-120,\"width\":260,\"height\":90,\"text\":\"unknown keys\",\"zIndex\":7,\"meta\":{\"tags\":[\"a\",\"b\"],\"ratio\":1.50,\"big\":12345678901234567890,\"none\":null}},\n"
    "\t\t{\"id\":\"deadbeefdeadbeef\",\"type\":\"sticker\",\"x\":420,\"y\":40,\"width\":100,\"height\":100,\"emoji\":\"\xF0\x9F\x99\x82\",\"size\":2.5e1}\n"
    "\t],\n\t\"edges\":[\n"
    "\t\t{\"id\":\"e000000000000001\",\"fromNode\":\"a1b2c3d4e5f60718\",\"fromSide\":\"right\",\"toNode\":\"0f1e2d3c4b5a6978\",\"toSide\":\"left\",\"label\":\"see also\"},\n"
    "\t\t{\"id\":\"e000000000000002\",\"fromNode\":\"5e4d3c2b1a090807\",\"fromSide\":\"right\",\"fromEnd\":\"arrow\",\"toNode\":\"0f1e2d3c4b5a6978\",\"toSide\":\"left\",\"toEnd\":\"none\",\"color\":\"5\"},\n"
    "\t\t{\"id\":\"e000000000000003\",\"fromNode\":\"0f1e2d3c4b5a6978\",\"toNode\":\"c0ffee00c0ffee01\",\"color\":\"#3366cc\",\"style\":\"dashed\"}\n"
    "\t],\n\t\"metadata\":{\"version\":\"1.0-1.0\",\"frontmatter\":{\"a\":1}}\n}";

static void check_fixed_point(const char *text, size_t len, const char *what) {
    ExCanvas *c = ex_canvas_parse(text, len, NULL, 0), *d;
    char *w1, *w2;
    size_t n1 = 0, n2 = 0;
    if (!c) return;
    w1 = write_canvas(c, &n1);
    HUNT_CHECKF(w1 != NULL, "%s: the canvas was read but cannot be written", what);
    if (!w1) { ex_canvas_free(c); return; }
    d = ex_canvas_parse(w1, n1, NULL, 0);
    HUNT_CHECKF(d != NULL, "%s: what the writer wrote does not read back", what);
    if (d) {
        w2 = write_canvas(d, &n2);
        HUNT_CHECKF(w2 && n1 == n2 && !memcmp(w1, w2, n1), "%s: writing, reading and writing again changed the text", what);
        if (c->n == d->n && c->ne == d->ne) {
            size_t i;
            for (i = 0; i < c->n; i++) HUNT_CHECKF(c->nodes[i].id && d->nodes[i].id ? !strcmp(c->nodes[i].id, d->nodes[i].id) : c->nodes[i].id == d->nodes[i].id, "%s: node %lu changed its id", what, (unsigned long)i);
        } else HUNT_CHECKF(0, "%s: %lu nodes and %lu edges became %lu and %lu", what, (unsigned long)c->n, (unsigned long)c->ne, (unsigned long)d->n, (unsigned long)d->ne);
        free(w2);
        ex_canvas_free(d);
    }
    free(w1);
    ex_canvas_free(c);
}

static void test_files(void) {
    ExCanvas *c;
    char *w;
    size_t n = 0, len = strlen(SAMPLE);
    char err[200];

    hunt_step("canvas: a file in the usual layout is written back byte for byte");
    c = ex_canvas_parse(SAMPLE, len, err, sizeof err);
    HUNT_CHECK(c != NULL);
    if (c) {
        w = write_canvas(c, &n);
        HUNT_SPECF(w && n == len && !memcmp(w, SAMPLE, len), "include/exhaust_canvas.h: a file in that layout is written back byte for byte", "the written text has %lu bytes, the file %lu", (unsigned long)n, (unsigned long)len);
        if (w && (n != len || memcmp(w, SAMPLE, len))) { size_t k = 0; while (k < n && k < len && w[k] == SAMPLE[k]) k++; hunt_note_("first difference at byte %lu: wrote \"%.40s\" but the file has \"%.40s\"", (unsigned long)k, w + k, SAMPLE + k); }
        free(w);
        HUNT_CHECK(c->n == 6 && c->ne == 3);
        HUNT_CHECK(!c->repaired);
        /* unknown keys are kept at every level, and the order of keys inside an object */
        HUNT_CHECK(c->nodes[4].nextra >= 2);
        HUNT_CHECK(c->nodes[5].kind == EX_CN_OTHER && c->nodes[5].type && !strcmp(c->nodes[5].type, "sticker"));
        HUNT_CHECK(c->nextra >= 1);
        ex_canvas_free(c);
    }

    hunt_step("canvas: not a canvas");
    {
        const char *bad[] = { "", "   ", "[]", "[{}]", "\"nodes\"", "42", "null", "true", "{", "{\"nodes\":", "{\"nodes\":[}", "nodes: []" };
        size_t i;
        for (i = 0; i < sizeof bad / sizeof *bad; i++) {
            err[0] = 0;
            c = ex_canvas_parse(bad[i], strlen(bad[i]), err, sizeof err);
            HUNT_CHECKF(c == NULL, "\"%s\" is not a JSON object but was accepted as a canvas", bad[i]);
            if (c) ex_canvas_free(c);
            else HUNT_CHECKF(err[0] != 0, "no reason was given for refusing \"%s\"", bad[i]);
        }
        c = ex_canvas_parse(NULL, 0, NULL, 0);
        HUNT_CHECK(c == NULL);
    }
    hunt_step("canvas: an empty object and a new canvas");
    c = ex_canvas_parse("{}", 2, NULL, 0);
    HUNT_CHECK(c && c->n == 0 && c->ne == 0);
    if (c) { w = write_canvas(c, &n); HUNT_CHECK(w != NULL); check_fixed_point(w, n, "empty object"); free(w); ex_canvas_free(c); }
    c = ex_canvas_new();
    w = write_canvas(c, &n);
    HUNT_CHECK(w != NULL);
    if (w) { check_fixed_point(w, n, "new canvas"); free(w); }
    ex_canvas_free(c);

    hunt_step("canvas: hostile but valid JSON");
    {
        struct { const char *name, *text; } h[] = {
            { "BOM", "\xEF\xBB\xBF{\"nodes\":[],\"edges\":[]}" },
            { "trailing text", "{\"nodes\":[],\"edges\":[]} trailing" },
            { "two objects", "{\"nodes\":[]}{\"nodes\":[]}" },
            { "duplicate top-level keys", "{\"nodes\":[],\"nodes\":[{\"id\":\"a\",\"type\":\"text\",\"x\":0,\"y\":0,\"width\":1,\"height\":1,\"text\":\"t\"}]}" },
            { "duplicate ids", "{\"nodes\":[{\"id\":\"a\",\"type\":\"text\",\"x\":0,\"y\":0,\"width\":1,\"height\":1,\"text\":\"1\"},{\"id\":\"a\",\"type\":\"text\",\"x\":5,\"y\":5,\"width\":1,\"height\":1,\"text\":\"2\"}],\"edges\":[{\"id\":\"e\",\"fromNode\":\"a\",\"toNode\":\"a\"},{\"id\":\"e\",\"fromNode\":\"a\",\"toNode\":\"a\"}]}" },
            { "missing ids", "{\"nodes\":[{\"type\":\"text\",\"x\":0,\"y\":0,\"width\":1,\"height\":1,\"text\":\"1\"},{\"type\":\"text\",\"text\":\"2\"}],\"edges\":[{\"fromNode\":\"x\",\"toNode\":\"y\"}]}" },
            { "id that is not a string", "{\"nodes\":[{\"id\":5,\"type\":\"text\"},{\"id\":null,\"type\":\"file\"},{\"id\":[1],\"type\":\"link\"}]}" },
            { "huge numbers", "{\"nodes\":[{\"id\":\"a\",\"type\":\"text\",\"x\":1e999,\"y\":-1e999,\"width\":1e308,\"height\":-0,\"text\":\"t\"}]}" },
            { "number spellings", "{\"nodes\":[{\"id\":\"a\",\"type\":\"text\",\"x\":1e2,\"y\":250.0,\"width\":+5,\"height\":0.5E+1,\"text\":\"t\"}]}" },
            { "negative zero and exponents", "{\"nodes\":[{\"id\":\"a\",\"type\":\"text\",\"x\":-0,\"y\":-0.0,\"width\":1E5,\"height\":1e-5,\"text\":\"t\"}]}" },
            { "numbers as strings", "{\"nodes\":[{\"id\":\"a\",\"type\":\"text\",\"x\":\"10\",\"y\":\"20\",\"width\":\"30\",\"height\":\"40\",\"text\":\"t\"}]}" },
            { "wrong types", "{\"nodes\":{\"a\":1},\"edges\":\"none\"}" },
            { "nodes is null", "{\"nodes\":null,\"edges\":null}" },
            { "node is not an object", "{\"nodes\":[1,\"two\",null,[3],true,{\"id\":\"x\",\"type\":\"text\",\"text\":\"ok\"}]}" },
            { "lone surrogate", "{\"nodes\":[{\"id\":\"a\",\"type\":\"text\",\"text\":\"\\ud800\"}]}" },
            { "surrogate pair", "{\"nodes\":[{\"id\":\"a\",\"type\":\"text\",\"text\":\"\\ud83d\\ude00 and \\u00e9\"}]}" },
            { "NUL escape", "{\"nodes\":[{\"id\":\"a\",\"type\":\"text\",\"text\":\"a\\u0000b\"}]}" },
            { "control characters", "{\"nodes\":[{\"id\":\"a\",\"type\":\"text\",\"text\":\"\\u0001\\u001f\\b\\f\\n\\r\\t\\/\"}]}" },
            { "edge to nothing", "{\"nodes\":[],\"edges\":[{\"id\":\"e\",\"fromNode\":\"gone\",\"toNode\":\"gone\",\"fromSide\":\"diagonal\",\"toEnd\":\"sideways\"}]}" },
            { "unknown colors", "{\"nodes\":[{\"id\":\"a\",\"type\":\"text\",\"color\":\"purple\",\"text\":\"t\"},{\"id\":\"b\",\"type\":\"text\",\"color\":7,\"text\":\"t\"}]}" },
            { "unknown type", "{\"nodes\":[{\"id\":\"a\",\"type\":\"hologram\",\"x\":1,\"y\":2,\"width\":3,\"height\":4,\"payload\":{\"deep\":[[[]]]}},{\"id\":\"b\",\"x\":1}]}" },
            { "key order and case", "{\"edges\":[],\"Nodes\":[1],\"nodes\":[],\"NODES\":[2]}" },
            { "empty strings", "{\"nodes\":[{\"id\":\"\",\"type\":\"\",\"text\":\"\",\"file\":\"\",\"url\":\"\",\"label\":\"\"}]}" },
            { "very long string", "" },
        };
        size_t i;
        char *longs = (char *)malloc(4 * 1024 * 1024);
        for (i = 0; i < sizeof h / sizeof *h; i++) {
            const char *text = h[i].text;
            size_t tl = strlen(text);
            if (!*text) {
                size_t k, p = 0;
                p += (size_t)sprintf(longs, "{\"nodes\":[{\"id\":\"a\",\"type\":\"text\",\"text\":\"");
                for (k = 0; k < 3 * 1024 * 1024; k++) longs[p++] = (char)('a' + k % 26);
                p += (size_t)sprintf(longs + p, "\"}]}");
                text = longs; tl = p;
            }
            hunt_stepf("canvas hostile: %s", h[i].name);
            hunt_set_input(text, tl > 600 ? 600 : tl, h[i].name);
            check_fixed_point(text, tl, h[i].name);
            hunt_clear_input();
        }
        free(longs);
        /* the repaired flag says the text the file has is not what would be written */
        c = ex_canvas_parse(h[4].text, strlen(h[4].text), NULL, 0);
        if (c) {
            size_t a, b;
            int dup = 0;
            HUNT_CHECK(c->repaired);
            for (a = 0; a < c->n; a++) for (b = a + 1; b < c->n; b++) if (c->nodes[a].id && c->nodes[b].id && !strcmp(c->nodes[a].id, c->nodes[b].id)) dup = 1;      /* an entry that is not an object has no id */
            for (a = 0; a < c->ne; a++) for (b = a + 1; b < c->ne; b++) if (c->edges[a].id && c->edges[b].id && !strcmp(c->edges[a].id, c->edges[b].id)) dup = 1;
            HUNT_CHECKF(!dup, "after repair two cards or two edges still share an id");
            ex_canvas_free(c);
        }
        c = ex_canvas_parse(h[5].text, strlen(h[5].text), NULL, 0);
        if (c) {
            size_t a, b;
            int dup = 0, empty = 0;
            for (a = 0; a < c->n; a++) { if (c->nodes[a].raw) continue; if (!c->nodes[a].id || !*c->nodes[a].id) empty = 1; for (b = a + 1; b < c->n; b++) if (!c->nodes[b].raw && c->nodes[a].id && c->nodes[b].id && !strcmp(c->nodes[a].id, c->nodes[b].id)) dup = 1; }
            HUNT_CHECK(!dup && !empty);
            ex_canvas_free(c);
        }
    }

    hunt_step("canvas: nesting a hundred thousand deep inside an unknown key");
    {
        size_t depth = 100000, i, p = 0;
        char *t = (char *)malloc(depth * 2 + 200);
        p += (size_t)sprintf(t, "{\"nodes\":[],\"deep\":");
        for (i = 0; i < depth; i++) t[p++] = '[';
        for (i = 0; i < depth; i++) t[p++] = ']';
        t[p++] = '}'; t[p] = 0;
        hunt_set_input(t, 64, "deep arrays");
        check_fixed_point(t, p, "deep nesting");
        hunt_clear_input();
        free(t);
    }

    hunt_step("canvas: random mutations of the sample");
    {
        long rounds = hunt_iters(4000), r;
        for (r = 0; r < rounds; r++) {
            unsigned char *buf = (unsigned char *)malloc(len * 2 + 64);
            size_t l = len;
            memcpy(buf, SAMPLE, len);
            l = hunt_mutate(buf, l, len * 2 + 32);
            hunt_stepf("canvas mutation %ld", r);
            hunt_set_input(buf, l, "mutated canvas");
            check_fixed_point((const char *)buf, l, "mutated sample");
            hunt_clear_input();
            free(buf);
        }
    }
}

/* ---- B. editing ---- */

static void check_canvas_consistent(const ExCanvas *c, const char *what) {
    size_t i, j;
    for (i = 0; i < c->n; i++) {
        HUNT_CHECKF(c->nodes[i].id && *c->nodes[i].id, "%s: card %lu has no id", what, (unsigned long)i);
        if (!c->nodes[i].id) continue;
        for (j = i + 1; j < c->n; j++) if (c->nodes[j].id && !strcmp(c->nodes[i].id, c->nodes[j].id)) HUNT_CHECKF(0, "%s: cards %lu and %lu share the id %s", what, (unsigned long)i, (unsigned long)j, c->nodes[i].id);
        HUNT_CHECKF(ex_canvas_find_node(c, c->nodes[i].id) == (int)i, "%s: find_node(%s) is not %lu", what, c->nodes[i].id, (unsigned long)i);
    }
    for (i = 0; i < c->ne; i++) {
        HUNT_CHECKF(c->edges[i].from && ex_canvas_find_node(c, c->edges[i].from) >= 0, "%s: edge %lu starts at a card that is not there", what, (unsigned long)i);
        HUNT_CHECKF(c->edges[i].to && ex_canvas_find_node(c, c->edges[i].to) >= 0, "%s: edge %lu ends at a card that is not there", what, (unsigned long)i);
        for (j = i + 1; j < c->ne; j++) if (c->edges[j].id && c->edges[i].id && !strcmp(c->edges[i].id, c->edges[j].id)) HUNT_CHECKF(0, "%s: edges %lu and %lu share an id", what, (unsigned long)i, (unsigned long)j);
    }
    HUNT_CHECK(ex_canvas_find_node(c, "no such card") == -1);
    HUNT_CHECK(ex_canvas_find_node(c, "") == -1);
}

static void test_editing(void) {
    long rounds = hunt_iters(300), r;
    for (r = 0; r < rounds; r++) {
        ExCanvas *c = ex_canvas_new();
        int ops = 30 + (int)hunt_rnd(80), k;
        hunt_stepf("canvas editing %ld", r);
        ex_canvas_set_seed(hunt_rand64());
        for (k = 0; k < ops; k++) {
            int kind = (int)hunt_rnd(100);
            unsigned long rev = c->rev;
            if (kind < 35 || c->n == 0) {
                ExCanvasKind ck = (ExCanvasKind)hunt_rnd(4);
                int i = ex_canvas_add_node(c, ck, (double)hunt_rnd(2000) - 1000, (double)hunt_rnd(2000) - 1000, 20.0 + hunt_rnd(400), 20.0 + hunt_rnd(300));
                HUNT_CHECK(i >= 0 && (size_t)i < c->n);
                if (i >= 0) { HUNT_CHECKF((size_t)i == c->n - 1, "a new card goes on top"); HUNT_CHECK(c->rev != rev); HUNT_CHECK(c->nodes[i].kind == ck); if (ck == EX_CN_TEXT) HUNT_CHECK(c->nodes[i].text != NULL); if (ck == EX_CN_FILE) HUNT_CHECK(c->nodes[i].file != NULL); if (ck == EX_CN_LINK) HUNT_CHECK(c->nodes[i].url != NULL); }
            } else if (kind < 50 && c->n >= 2) {
                size_t a = hunt_rnd_size(c->n), b = hunt_rnd_size(c->n);
                int e = ex_canvas_add_edge(c, a, (int)hunt_rnd(5) - 1, b, (int)hunt_rnd(5) - 1);
                HUNT_CHECK(e >= 0);
            } else if (kind < 62) {
                size_t i = hunt_rnd_size(c->n), ne0 = c->ne, joined = 0, j;
                char id[40];
                snprintf(id, sizeof id, "%s", c->nodes[i].id);
                for (j = 0; j < c->ne; j++) if (!strcmp(c->edges[j].from, id) || !strcmp(c->edges[j].to, id)) joined++;
                HUNT_EQ_INT(ex_canvas_remove_node(c, i), 0);
                HUNT_CHECKF(c->ne == ne0 - joined, "removing a card removed %lu edges, %lu joined it", (unsigned long)(ne0 - c->ne), (unsigned long)joined);
                HUNT_CHECK(ex_canvas_find_node(c, id) == -1);
            } else if (kind < 70 && c->ne > 0) {
                size_t i = hunt_rnd_size(c->ne);
                HUNT_EQ_INT(ex_canvas_remove_edge(c, i), 0);
            } else if (kind < 80) {
                size_t i = hunt_rnd_size(c->n);
                char id[40];
                int ni;
                snprintf(id, sizeof id, "%s", c->nodes[i].id);
                ni = ex_canvas_raise_node(c, i);
                HUNT_CHECK(ni == (int)c->n - 1);
                HUNT_CHECK(!strcmp(c->nodes[c->n - 1].id, id));
            } else if (kind < 88) {
                size_t i = hunt_rnd_size(c->n);
                ExCanvasNode *nd = &c->nodes[i];
                const char *vals[] = { "plain", "with \"quotes\" and \\ backslash", "line\nbreak\ttab", "\xC3\xA9\xF0\x9F\x98\x80", "", "</script>", "\\u0041" };
                char **field = nd->kind == EX_CN_TEXT ? &nd->text : nd->kind == EX_CN_FILE ? &nd->file : nd->kind == EX_CN_LINK ? &nd->url : &nd->label;
                HUNT_EQ_INT(ex_canvas_set_string(c, field, vals[hunt_rnd(sizeof vals / sizeof *vals)]), 0);
                nd->x += 0.5; ex_canvas_touch(c);
            } else {
                /* bad indexes */
                HUNT_CHECK(ex_canvas_remove_node(c, c->n + 3) != 0);
                HUNT_CHECK(ex_canvas_remove_node(c, (size_t)-1) != 0);
                HUNT_CHECK(ex_canvas_remove_edge(c, c->ne + 3) != 0);
                HUNT_CHECK(ex_canvas_add_edge(c, c->n + 1, 0, 0, 0) < 0);
                HUNT_CHECK(ex_canvas_add_edge(c, 0, 0, c->n + 7, 0) < 0);
                HUNT_SUSPECT(ex_canvas_raise_node(c, c->n + 2) < 0, "raising a card that does not exist should return -1");
            }
            check_canvas_consistent(c, "after an edit");
        }
        {
            size_t n = 0;
            char *w = write_canvas(c, &n);
            if (w) { check_fixed_point(w, n, "edited canvas"); free(w); }
        }
        ex_canvas_free(c);
    }

    hunt_step("canvas: new ids are 16 hexadecimal digits, unique, and repeatable from a seed");
    {
        ExCanvas *c = ex_canvas_new();
        char id[17], first[40][17];
        int i;
        size_t n = 0;
        ex_canvas_set_seed(12345);
        for (i = 0; i < 40; i++) { ex_canvas_new_id(c, first[i]); }
        ex_canvas_set_seed(12345);
        for (i = 0; i < 40; i++) { ex_canvas_new_id(c, id); HUNT_CHECKF(!strcmp(id, first[i]), "the same seed gave another id the second time"); }
        for (i = 0; i < 40; i++) {
            size_t k;
            HUNT_CHECK(strlen(first[i]) == 16);
            for (k = 0; k < 16; k++) HUNT_CHECKF((first[i][k] >= '0' && first[i][k] <= '9') || (first[i][k] >= 'a' && first[i][k] <= 'f'), "id %s has a character that is not lowercase hexadecimal", first[i]);
        }
        for (i = 0; i < 6000; i++) { int ni = ex_canvas_add_node(c, EX_CN_TEXT, 0, 0, 10, 10); HUNT_CHECK(ni >= 0); }
        check_canvas_consistent(c, "6000 cards");
        n = c->n;
        HUNT_EQ_INT(n, 6000);
        ex_canvas_free(c);
    }
}

/* ---- C. geometry ---- */

static int inside(const ExCanvasNode *n, double x, double y) { return x >= n->x && x <= n->x + n->w && y >= n->y && y <= n->y + n->h; }

static void test_geometry(void) {
    long rounds = hunt_iters(600), r;
    hunt_step("geometry: hit testing, bounds and group members against brute force");
    for (r = 0; r < rounds; r++) {
        ExCanvas *c = ex_canvas_new();
        int count = 2 + (int)hunt_rnd(14), i, k;
        double bx0 = 1e18, by0 = 1e18, bx1 = -1e18, by1 = -1e18;
        hunt_stepf("canvas geometry %ld", r);
        for (i = 0; i < count; i++) {
            ExCanvasKind ck = hunt_chance(30) ? EX_CN_GROUP : (ExCanvasKind)hunt_rnd(3);
            int ni = ex_canvas_add_node(c, ck, (double)hunt_rnd(800) - 400 + 0.25, (double)hunt_rnd(800) - 400 + 0.25, 40.0 + hunt_rnd(500) + 0.5, 40.0 + hunt_rnd(400) + 0.5);
            (void)ni;
        }
        for (i = 0; i < (int)c->n; i++) { const ExCanvasNode *n = &c->nodes[i]; if (n->x < bx0) bx0 = n->x; if (n->y < by0) by0 = n->y; if (n->x + n->w > bx1) bx1 = n->x + n->w; if (n->y + n->h > by1) by1 = n->y + n->h; }
        {
            double x0, y0, x1, y1;
            HUNT_CHECK(ex_canvas_bounds(c, &x0, &y0, &x1, &y1) != 0);
            HUNT_CHECKF(x0 == bx0 && y0 == by0 && x1 == bx1 && y1 == by1, "bounds %g,%g %g,%g, want %g,%g %g,%g", x0, y0, x1, y1, bx0, by0, bx1, by1);
        }
        for (k = 0; k < 60; k++) {
            double x = (double)hunt_rnd(1400) - 700 + 0.37, y = (double)hunt_rnd(1400) - 700 + 0.61;
            int want = -1, got = ex_canvas_node_at(c, x, y);
            for (i = (int)c->n - 1; i >= 0 && want < 0; i--) if (c->nodes[i].kind != EX_CN_GROUP && c->nodes[i].kind != EX_CN_OTHER && inside(&c->nodes[i], x, y)) want = i;
            for (i = (int)c->n - 1; i >= 0 && want < 0; i--) if (c->nodes[i].kind == EX_CN_GROUP && inside(&c->nodes[i], x, y)) want = i;
            HUNT_CHECKF(got == want, "node_at(%g, %g) = %d, brute force says %d", x, y, got, want);
        }
        for (i = 0; i < (int)c->n; i++) {
            if (c->nodes[i].kind != EX_CN_GROUP) continue;
            {
                size_t out[64], cnt = ex_canvas_group_members(c, (size_t)i, out, 64), j, expect = 0, found = 0;
                const ExCanvasNode *g = &c->nodes[i];
                for (j = 0; j < c->n; j++) {
                    const ExCanvasNode *n = &c->nodes[j];
                    int wholly;
                    if ((int)j == i) continue;
                    wholly = n->x >= g->x && n->y >= g->y && n->x + n->w <= g->x + g->w && n->y + n->h <= g->y + g->h;
                    if (wholly && (n->kind != EX_CN_GROUP || (n->w * n->h < g->w * g->h))) expect++;
                }
                HUNT_CHECKF(cnt == expect, "group %d has %lu members, brute force counts %lu", i, (unsigned long)cnt, (unsigned long)expect);
                for (j = 0; j < cnt && j < 64; j++) { if (out[j] < c->n && (int)out[j] != i) found++; }
                HUNT_CHECK(found == (cnt < 64 ? cnt : 64));
                HUNT_CHECK(ex_canvas_group_members(c, (size_t)i, out, 0) <= c->n);        /* max 0 must not write */
            }
        }
        /* side points and nearest side from far away on each axis */
        for (i = 0; i < (int)c->n && i < 5; i++) {
            const ExCanvasNode *n = &c->nodes[i];
            double px, py, cx = n->x + n->w / 2, cy = n->y + n->h / 2;
            ex_canvas_side_point(n, EX_CS_TOP, &px, &py);    HUNT_CHECK(px == cx && py == n->y);
            ex_canvas_side_point(n, EX_CS_BOTTOM, &px, &py); HUNT_CHECK(px == cx && py == n->y + n->h);
            ex_canvas_side_point(n, EX_CS_LEFT, &px, &py);   HUNT_CHECK(px == n->x && py == cy);
            ex_canvas_side_point(n, EX_CS_RIGHT, &px, &py);  HUNT_CHECK(px == n->x + n->w && py == cy);
            HUNT_CHECK(ex_canvas_nearest_side(n, cx, n->y - 900) == EX_CS_TOP);
            HUNT_CHECK(ex_canvas_nearest_side(n, cx, n->y + n->h + 900) == EX_CS_BOTTOM);
            HUNT_CHECK(ex_canvas_nearest_side(n, n->x - 900, cy) == EX_CS_LEFT);
            HUNT_CHECK(ex_canvas_nearest_side(n, n->x + n->w + 900, cy) == EX_CS_RIGHT);
        }
        ex_canvas_free(c);
    }

    hunt_step("geometry: edge ends face each other when the file does not say");
    {
        ExCanvas *c = ex_canvas_new();
        double x1, y1, x2, y2;
        int s1, s2, a = ex_canvas_add_node(c, EX_CN_TEXT, 0, 0, 100, 50), b = ex_canvas_add_node(c, EX_CN_TEXT, 500, 0, 100, 50), d = ex_canvas_add_node(c, EX_CN_TEXT, 0, 500, 100, 50), e;
        e = ex_canvas_add_edge(c, (size_t)a, EX_CS_NONE, (size_t)b, EX_CS_NONE);
        HUNT_CHECK(e >= 0 && ex_canvas_edge_ends(c, &c->edges[e], &x1, &y1, &s1, &x2, &y2, &s2) != 0);
        HUNT_CHECKF(s1 == EX_CS_RIGHT && s2 == EX_CS_LEFT && x1 == 100 && y1 == 25 && x2 == 500 && y2 == 25, "a card to the right: sides %d and %d, points %g,%g and %g,%g", s1, s2, x1, y1, x2, y2);
        e = ex_canvas_add_edge(c, (size_t)a, EX_CS_NONE, (size_t)d, EX_CS_NONE);
        HUNT_CHECK(e >= 0 && ex_canvas_edge_ends(c, &c->edges[e], &x1, &y1, &s1, &x2, &y2, &s2) != 0);
        HUNT_CHECKF(s1 == EX_CS_BOTTOM && s2 == EX_CS_TOP, "a card below: sides %d and %d", s1, s2);
        e = ex_canvas_add_edge(c, (size_t)b, EX_CS_TOP, (size_t)a, EX_CS_BOTTOM);
        HUNT_CHECK(e >= 0 && ex_canvas_edge_ends(c, &c->edges[e], &x1, &y1, &s1, &x2, &y2, &s2) != 0);
        HUNT_CHECKF(s1 == EX_CS_TOP && s2 == EX_CS_BOTTOM && x1 == 550 && y1 == 0 && x2 == 50 && y2 == 50, "sides the file names are kept");
        c->edges[e].from = hunt_strdup("not a card");
        HUNT_CHECK(ex_canvas_edge_ends(c, &c->edges[e], &x1, &y1, &s1, &x2, &y2, &s2) == 0);      /* an end that is missing */
        ex_canvas_free(c);
    }

    hunt_step("colors");
    {
        uint8_t rgb[3] = { 9, 9, 9 };
        const char *bad[] = { "7", "0", "", "#abcdef0", "#12345", "#ggggg0", "red", "#", " #ffffff", "#ffffff ", "10", "-1", "#00000g", "##ffffff", "1 ", " 1" };
        size_t i;
        for (i = 1; i <= 6; i++) { char s[2] = { (char)('0' + i), 0 }; HUNT_CHECKF(ex_canvas_color(s, rgb) == (int)i, "preset %s", s); }
        HUNT_CHECK(ex_canvas_color("#ff8800", rgb) == 0 && rgb[0] == 0xff && rgb[1] == 0x88 && rgb[2] == 0x00);
        HUNT_CHECK(ex_canvas_color("#FF8800", rgb) == 0 && rgb[0] == 0xff && rgb[1] == 0x88 && rgb[2] == 0x00);
        HUNT_CHECK(ex_canvas_color("#000000", rgb) == 0 && rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0);
        HUNT_CHECK(ex_canvas_color(NULL, rgb) == -1);
        for (i = 0; i < sizeof bad / sizeof *bad; i++) HUNT_CHECKF(ex_canvas_color(bad[i], rgb) == -1, "color \"%s\" should not be understood", bad[i]);
    }
}

/* ---- D. YAML ---- */

static int tree_equal(const ExYNode *a, const ExYNode *b) {
    size_t i;
    if (!a || !b) return a == b;
    if (a->kind != b->kind || a->n != b->n) return 0;
    if ((a->value == NULL) != (b->value == NULL) || (a->value && strcmp(a->value, b->value))) return 0;
    if ((a->anchor == NULL) != (b->anchor == NULL) || (a->anchor && strcmp(a->anchor, b->anchor))) return 0;
    if ((a->tag == NULL) != (b->tag == NULL) || (a->tag && strcmp(a->tag, b->tag))) return 0;
    for (i = 0; i < a->n; i++) if (!tree_equal(a->kids[i], b->kids[i])) return 0;
    return 1;
}

static void yaml_fixed_point(const char *text, size_t len, const char *what) {
    char err[200];
    ExYNode *t = ex_y_parse(text, len, err, sizeof err), *u;
    size_t n1 = 0, n2 = 0;
    char *w1, *w2;
    if (!t) { HUNT_CHECKF(err[0] != 0, "%s: refused without a reason", what); return; }
    w1 = ex_y_write(t, &n1);
    HUNT_CHECKF(w1 != NULL, "%s: read but cannot be written", what);
    if (!w1) { ex_y_free(t); return; }
    u = ex_y_parse(w1, n1, err, sizeof err);
    HUNT_CHECKF(u != NULL, "%s: what was written does not read back (%s):\n%s", what, err, w1);
    if (u) {
        HUNT_CHECKF(tree_equal(t, u), "%s: the tree changed through write and read:\n--- written\n%s", what, w1);
        w2 = ex_y_write(u, &n2);
        HUNT_CHECKF(w2 && n1 == n2 && !memcmp(w1, w2, n1), "%s: writing is not a fixed point:\n--- first\n%s--- second\n%s", what, w1, w2 ? w2 : "(null)");
        free(w2);
        ex_y_free(u);
    }
    {
        ExYNode *cp = ex_y_copy(t);
        HUNT_CHECKF(cp && tree_equal(cp, t), "%s: a copy differs", what);
        ex_y_free(cp);
    }
    free(w1);
    ex_y_free(t);
}

static void test_yaml(void) {
    hunt_step("yaml: scalars written and read back exactly");
    {
        static const char *const texts[] = {
            "plain", "", " ", " lead", "trail ", "two  spaces", "true", "True", "TRUE", "false", "yes", "no", "on", "off", "y", "n", "Y", "N", "null", "Null", "NULL", "~", "123", "-1", "+1", "1.5", "1e3", "1E3", ".5", "0x1F", "0o17", "0b101", "1_000",
            ".inf", "-.inf", ".nan", ".NaN", "1:30", "2024-01-01", "2024-01-01T10:00:00Z", "- a", "-", "? x", "a: b", "a:b", "a #b", "#c", "a#b", "@x", "`x", "&anchor", "*alias", "!tag", "%dir", "|", ">", "'", "\"", "'quoted'", "\"dq\"",
            "{x}", "[x]", "x, y", "multi\nline", "line\n", "\nlead", "tab\there", "\t", "back\\slash", "\\n", "\xC3\xA9", "e\xCC\x81", "\xF0\x9F\x98\x80", "\xE2\x80\xA8", "\xC2\x85", "\xEF\xBB\xBF" "bom", "---", "...", "--- x", "key: ", "[[Link]]", "[[Link|alias]]",
            "http://x.y/z?a=b&c=d#e", "C:\\dir", "a'b", "a\"b", "it's", "<<", "=", "!!str", "!!int 5", "0", "00", "0.0", "-0", "1e", "e1", "1.", "09", "0123", "9223372036854775808", "1,5", "no\nyes", "a\r\nb", "a\rb",
        };
        size_t i;
        for (i = 0; i < sizeof texts / sizeof *texts; i++) {
            ExYNode *m = ex_y_map(), *back;
            char *w, err[200];
            size_t wl = 0;
            const char *got;
            ex_y_set(m, "key", ex_y_scalar(texts[i]));
            w = ex_y_write(m, &wl);
            hunt_stepf("yaml scalar %lu", (unsigned long)i);
            HUNT_CHECK(w != NULL);
            if (!w) { ex_y_free(m); continue; }
            back = ex_y_parse(w, wl, err, sizeof err);
            HUNT_CHECKF(back != NULL, "the text \"%s\" was written as\n%s\nand does not read back: %s", texts[i], w, err);
            if (back) {
                ExYNode *v = ex_y_get(back, "key");
                got = v ? ex_y_str(v) : NULL;
                HUNT_CHECKF(got && !strcmp(got, texts[i]), "the text \"%s\" came back as \"%s\" from\n%s", texts[i], got ? got : "(null)", w);
                if (v && v->kind == EXY_SCALAR) {
                    static const char *const must_quote[] = { "true", "false", "null", "~", "123", "-1", "1.5", "1e3", "True", "TRUE", "False", "Null", "NULL", "+1", ".5", "0x1F", "", ".inf", ".nan" };
                    size_t q;
                    for (q = 0; q < sizeof must_quote / sizeof *must_quote; q++) if (!strcmp(texts[i], must_quote[q]) && texts[i][0]) {
                        HUNT_SPECF(v->style != EXY_PLAIN, "include/exhaust_yaml.h: text that YAML would read as a number, a boolean or null is quoted", "\"%s\" was written plain as\n%s", texts[i], w);
                    }
                    if (!strcmp(texts[i], "yes") || !strcmp(texts[i], "no") || !strcmp(texts[i], "on") || !strcmp(texts[i], "off") || !strcmp(texts[i], "y") || !strcmp(texts[i], "n") || !strcmp(texts[i], "2024-01-01") || !strcmp(texts[i], "1:30") || !strcmp(texts[i], "1_000") || !strcmp(texts[i], "0o17"))
                        HUNT_SUSPECT(v->style != EXY_PLAIN, "YAML 1.1 readers (many tools, PyYAML) read this plain text as a boolean, a date or a number: another program will read the base file differently");
                }
                ex_y_free(back);
            }
            free(w);
            ex_y_free(m);
        }
    }

    hunt_step("yaml: random strings through the emitter");
    {
        long rounds = hunt_iters(8000), r;
        for (r = 0; r < rounds; r++) {
            char text[120];
            size_t tl = hunt_random_valid_text(text, sizeof text, 6);
            ExYNode *m = ex_y_map(), *seq = ex_y_seq(), *back;
            char *w, err[200];
            size_t wl = 0;
            const char *got;
            int style = 1 + (int)hunt_rnd(5);
            (void)tl;
            ex_y_set(m, text, ex_y_scalar_style(text, hunt_chance(50) ? style : EXY_PLAIN));       /* the key and the value */
            ex_y_push(seq, ex_y_scalar(text));
            ex_y_set(m, "list", seq);
            hunt_stepf("yaml emitter %ld", r);
            hunt_set_input(text, strlen(text), "scalar text");
            w = ex_y_write(m, &wl);
            if (!w) { HUNT_CHECK(0); ex_y_free(m); continue; }
            back = ex_y_parse(w, wl, err, sizeof err);
            if (!back) { HUNT_CHECKF(0, "the text was written as\n%s\nand does not read back: %s", w, err); }
            else {
                ExYNode *l = ex_y_get(back, "list"), *v = ex_y_get(back, text);
                got = l && ex_y_item(l, 0) ? ex_y_str(ex_y_item(l, 0)) : NULL;
                HUNT_CHECKF(got && !strcmp(got, text), "a list item came back as \"%s\" from\n%s", got ? got : "(null)", w);
                if (!hunt_chance(1)) { /* a key that is empty or a long key may be written in a form that parses to a different key; check only that it is there */ HUNT_CHECKF(v != NULL || text[0] == 0 || strlen(text) > 100, "the key \"%s\" is not there after reading\n%s", text, w); }
                ex_y_free(back);
            }
            hunt_clear_input();
            free(w);
            ex_y_free(m);
        }
    }

    hunt_step("yaml: documents with every feature");
    {
        const char *docs[] = {
            "a: 1\nb: [1, 2, 3]\nc: {x: 1, y: two}\n",
            "key: value\nlist:\n  - one\n  - two\n  - three: 3\n    four: 4\nnested:\n  deeper:\n    deepest: yes\n",
            "text: |\n  line one\n  line two\nfolded: >\n  folded\n  text\nkeep: |+\n  kept\n\nstrip: |-\n  stripped\n",
            "anchor: &a\n  x: 1\nuse: *a\nlist: &l [1, 2]\nagain: *l\n",
            "tagged: !custom value\nint: !!int \"5\"\nset: !!set {a, b}\n",
            "\"quoted key\": 'single quoted'\n? complex key\n: complex value\n",
            "empty:\nnull_value: null\ntilde: ~\nlist_of_empty:\n  -\n  -\n",
            "- a\n- b\n- - nested\n  - list\n- key: in list\n",
            "unicode: \xC3\xA9\xF0\x9F\x98\x80\nescaped: \"\\u00e9 \\t \\n \\\" \\\\\"\n",
            "formulas:\n  ppu: (price / age).toFixed(2)\nviews:\n  - type: table\n    name: Table\n    order:\n      - file.name\n      - note.price\n    unknown_key:\n      keep: me\n",
            "# a comment\nkey: value # trailing\n",
            "---\na: 1\n...\n",
            "a: 1\n---\nb: 2\n",
            "", "\n\n", "---\n", "{}", "[]", "~", "just a string", "42", "- ", "a:\n- b\n- c\n",
            "a: b: c\n", "a: [1, 2\n", "a: {x\n", "\t a: 1\n", "a: 1\n  b: 2\n", "a: *undefined\n", "a: &x 1\nb: &x 2\n", "[a, b]: c\n",
        };
        size_t i;
        for (i = 0; i < sizeof docs / sizeof *docs; i++) { hunt_stepf("yaml doc %lu", (unsigned long)i); hunt_set_input(docs[i], strlen(docs[i]), "yaml"); yaml_fixed_point(docs[i], strlen(docs[i]), docs[i][0] ? docs[i] : "(empty)"); hunt_clear_input(); }
    }

    hunt_step("yaml: mapping and sequence operations");
    {
        ExYNode *m = ex_y_map(), *s = ex_y_seq();
        size_t i;
        HUNT_CHECK(ex_y_get(m, "a") == NULL && ex_y_count(m) == 0 && ex_y_is_null(NULL) && ex_y_str(NULL) == NULL);
        HUNT_EQ_INT(ex_y_set(m, "a", ex_y_scalar("1")), 0);
        HUNT_EQ_INT(ex_y_set(m, "b", ex_y_scalar("2")), 0);
        HUNT_EQ_INT(ex_y_set(m, "a", ex_y_scalar("3")), 0);             /* replaces, keeps its place */
        HUNT_EQ_INT(ex_y_count(m), 2);
        HUNT_CHECK(!strcmp(ex_y_key(m, 0), "a") && !strcmp(ex_y_str(ex_y_val(m, 0)), "3") && !strcmp(ex_y_key(m, 1), "b"));
        { const char *k5 = ex_y_key(m, 5); HUNT_CHECK(k5 == NULL || !*k5); }
        HUNT_CHECK(ex_y_val(m, 5) == NULL);
        HUNT_EQ_INT(ex_y_remove(m, "a"), 0);
        HUNT_CHECK(ex_y_remove(m, "a") != 0);
        HUNT_CHECK(ex_y_count(m) == 1 && ex_y_get(m, "a") == NULL);
        for (i = 0; i < 1000; i++) { char k[16]; snprintf(k, sizeof k, "k%lu", (unsigned long)i); ex_y_set(m, k, ex_y_scalar(k)); ex_y_push(s, ex_y_scalar(k)); }
        HUNT_CHECK(ex_y_count(m) == 1001 && ex_y_count(s) == 1000 && !strcmp(ex_y_str(ex_y_item(s, 999)), "k999") && ex_y_item(s, 1000) == NULL);
        {
            ExYNode *n1 = ex_y_scalar_style("", EXY_PLAIN), *n2 = ex_y_scalar_style("~", EXY_PLAIN), *n3 = ex_y_scalar_style("Null", EXY_PLAIN), *n4 = ex_y_scalar_style("x", EXY_PLAIN), *n5 = ex_y_scalar_style("null", EXY_DOUBLE);
            HUNT_CHECK(ex_y_is_null(n1) && ex_y_is_null(n2) && ex_y_is_null(n3) && !ex_y_is_null(n4));
            HUNT_SUSPECT(!ex_y_is_null(n5), "a quoted \"null\" is the text null, not a null value");
            ex_y_free(n1); ex_y_free(n2); ex_y_free(n3); ex_y_free(n4); ex_y_free(n5);
        }
        ex_y_free(m); ex_y_free(s);
    }

    hunt_step("yaml: nesting deeper than the stack");
    {
        size_t depths[] = { 1000, 20000, 200000 }, d;
        for (d = 0; d < sizeof depths / sizeof *depths; d++) {
            size_t depth = depths[d], i, p = 0;
            char *t = (char *)malloc(depth * 2 + 64);
            ExYNode *y;
            char err[100];
            for (i = 0; i < depth; i++) t[p++] = '[';
            t[p++] = '1';
            for (i = 0; i < depth; i++) t[p++] = ']';
            t[p] = 0;
            hunt_stepf("yaml flow nesting %lu", (unsigned long)depth);
            y = ex_y_parse(t, p, err, sizeof err);
            if (y) {
                size_t wl = 0;
                char *w = ex_y_write(y, &wl);
                ExYNode *cp = ex_y_copy(y);
                free(w);
                ex_y_free(cp);
                ex_y_free(y);
            }
            free(t);
        }
        {
            size_t depth = 3000, i, p = 0, k;
            char *t = (char *)malloc(depth * depth / 2 * 2 + depth * 8 + 64), err[100];
            ExYNode *y;
            for (i = 0; i < depth; i++) { for (k = 0; k < i; k++) t[p++] = ' '; p += (size_t)sprintf(t + p, "k%lu:\n", (unsigned long)i); }
            t[p] = 0;
            hunt_step("yaml block nesting 3000");
            y = ex_y_parse(t, p, err, sizeof err);
            if (y) { size_t wl = 0; char *w = ex_y_write(y, &wl); free(w); ex_y_free(y); }
            free(t);
        }
    }

    hunt_step("yaml: aliases are kept as aliases (a billion laughs file stays small)");
    {
        char *t = (char *)malloc(64000), err[100];
        size_t p = 0, lvl;
        ExYNode *y;
        clock_t t0;
        p += (size_t)sprintf(t, "a0: &a0 [x, x, x, x, x, x, x, x, x]\n");
        for (lvl = 1; lvl < 12; lvl++) p += (size_t)sprintf(t + p, "a%lu: &a%lu [*a%lu, *a%lu, *a%lu, *a%lu, *a%lu, *a%lu, *a%lu, *a%lu, *a%lu]\n", (unsigned long)lvl, (unsigned long)lvl, (unsigned long)lvl - 1, (unsigned long)lvl - 1, (unsigned long)lvl - 1,
            (unsigned long)lvl - 1, (unsigned long)lvl - 1, (unsigned long)lvl - 1, (unsigned long)lvl - 1, (unsigned long)lvl - 1, (unsigned long)lvl - 1);
        t0 = clock();
        y = ex_y_parse(t, p, err, sizeof err);
        if (y) {
            size_t wl = 0;
            char *w = ex_y_write(y, &wl);
            ExYNode *cp = ex_y_copy(y);
            HUNT_SPECF(wl < 100000, "include/exhaust_yaml.h: anchors and aliases are kept", "a file of %lu bytes with aliases was written as %lu bytes: the aliases were expanded", (unsigned long)p, (unsigned long)wl);
            free(w); ex_y_free(cp); ex_y_free(y);
        }
        HUNT_SUSPECT(hunt_cpu_since(t0) < hunt_time_limit(3.0), "a file with nine-fold aliases on twelve levels took more than 3 seconds");
        free(t);
    }

    hunt_step("yaml: random mutations of documents");
    {
        const char *seed = "formulas:\n  ppu: (price / age).toFixed(2)\nfilters:\n  and:\n    - file.hasTag(\"x\")\n    - 'price > 5'\nviews:\n  - type: table\n    name: \"Table\"\n    order: [file.name, note.price]\n    sort:\n      - property: price\n        direction: DESC\n    extra: &anc\n      keep: [1, 2, {a: b}]\n    other: *anc\n    text: |\n      multi\n      line\n";
        size_t sl = strlen(seed);
        long rounds = hunt_iters(5000), r;
        for (r = 0; r < rounds; r++) {
            unsigned char *buf = (unsigned char *)malloc(sl * 2 + 64);
            size_t l;
            memcpy(buf, seed, sl);
            l = hunt_mutate(buf, sl, sl * 2 + 32);
            hunt_stepf("yaml mutation %ld", r);
            hunt_set_input(buf, l, "mutated yaml");
            yaml_fixed_point((const char *)buf, l, "mutated base");
            hunt_clear_input();
            free(buf);
        }
    }
}

int main(int argc, char **argv) {
    hunt_begin("hunt_canvas_yaml", argc, argv);
    test_files();
    test_editing();
    test_geometry();
    test_yaml();
    return hunt_end();
}
