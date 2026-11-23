/* mermaid_basic (6d-ii) and mermaid_more (6d-iii): Mermaid diagrams are parsed and laid out into shapes, lines and texts. mermaid_basic: flowcharts and
 * sequence diagrams; mermaid_more: class, state, entity-relationship, pie and gantt diagrams. Common to both: The sample diagrams in
 * tests/data/mermaid match their golden listings (`<name>.golden`, the "render output": one item a line, numbers to two decimals, compared with a
 * tolerance of 0.03 so a platform's floating point rounding cannot fail it); the layout follows the rules by position (no overlapping nodes, edges that
 * start and end on nodes, ranks in the direction asked for, subgraph boxes around their members, sequence messages in order between their lifelines,
 * frames around their messages, activation bars); bad input is reported and draws nothing; the reading view draws a diagram and shows a bad one as source.
 * Usage: test_mermaid DATA_DIR [--more] [--update]   (--more: the types of 6d-iii; --update writes the golden files)  |  test_mermaid --dump FILE | --ppm FILE OUT.ppm */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_diagram.h"
#include "exhaust_diagramdraw.h"
#include "exhaust_platform.h"
#include "exhaust_theme.h"
#include "reading_view.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static ExFont *g_font;
static const char *g_dir = ".";

static ExDiagram *parse(const char *src) { return ex_diagram_parse(g_font, src, strlen(src)); }
static ExDiagram *parse_file(const char *name) {
    char path[600];
    size_t len = 0;
    char *src;
    ExDiagram *d;
    snprintf(path, sizeof path, "%s/%s.mmd", g_dir, name);
    src = ex_platform_read_file(path, &len);
    if (!src) { fprintf(stderr, "cannot read %s\n", path); failures++; return NULL; }
    d = ex_diagram_parse(g_font, src, len);
    free(src);
    return d;
}

/* ---- goldens ---- */
static int same_token(const char *a, size_t la, const char *b, size_t lb) {
    char x[64], y[64], *ex, *ey;
    double u, v;
    if (la == lb && !memcmp(a, b, la)) return 1;
    if (la >= sizeof x || lb >= sizeof y) return 0;
    memcpy(x, a, la); x[la] = 0; memcpy(y, b, lb); y[lb] = 0;
    u = strtod(x, &ex); v = strtod(y, &ey);
    return *ex == 0 && *ey == 0 && la && lb && fabs(u - v) <= 0.031;
}
static int sep(char c) { return c == ' ' || c == ',' || c == ':' || c == '\n' || c == '=' || c == 0; }
/* returns 0 when the listings agree token by token (numbers within 0.03), else the 1-based line of the first difference */
static int compare_listings(const char *a, const char *b) {
    int line = 1;
    for (;;) {
        size_t la = 0, lb = 0;
        while (*a == ' ' || *a == ',' || *a == ':' || *a == '=') a++;
        while (*b == ' ' || *b == ',' || *b == ':' || *b == '=') b++;
        if (*a == '\n' || !*a || *b == '\n' || !*b) {
            if ((*a == '\n' || !*a) != (*b == '\n' || !*b)) return line;
            if (!*a && !*b) return 0;
            a += *a == '\n'; b += *b == '\n'; line++;
            if (!*a && !*b) return 0;
            continue;
        }
        if (*a == '"') { /* a quoted text is compared whole */
            const char *ea = strchr(a + 1, '"'), *eb = strchr(b + 1, '"');
            if (!ea || !eb || *b != '"' || ea - a != eb - b || memcmp(a, b, (size_t)(ea - a))) return line;
            a = ea + 1; b = eb + 1;
            continue;
        }
        while (!sep(a[la])) la++;
        while (!sep(b[lb])) lb++;
        if (!same_token(a, la, b, lb)) return line;
        a += la; b += lb;
    }
}

static int update_goldens;
static void test_golden(const char *name) {
    char path[600], *buf = malloc(1 << 20), *want;
    ExDiagram *d = parse_file(name);
    size_t len = 0;
    int diff;
    if (!d || !buf) { free(buf); ex_diagram_free(d); return; }
    CHECK(!ex_diagram_error(d));
    ex_diagram_dump(d, buf, 1 << 20);
    snprintf(path, sizeof path, "%s/%s.golden", g_dir, name);
    if (update_goldens) { FILE *f = fopen(path, "wb"); if (f) { fputs(buf, f); fclose(f); } }
    want = ex_platform_read_file(path, &len);
    CHECK(want != NULL);
    if (want) {
        diff = compare_listings(buf, want);
        if (diff) fprintf(stderr, "%s: the listing differs from %s at line %d\n", name, path, diff);
        CHECK(diff == 0);
        free(want);
    }
    free(buf);
    ex_diagram_free(d);
}

/* ---- helpers over the items ---- */
typedef struct View { const ExDiagram *d; const ExDItem *it; size_t n; const float *pts; } View;
static View view(const ExDiagram *d) { View v; v.d = d; v.it = ex_diagram_items(d, &v.n); v.pts = ex_diagram_points(d, NULL); return v; }
static int is_shape(const ExDItem *it) { return it->kind <= EXD_DOUBLE_CIRCLE; }
static const ExDItem *find_text(const View *v, const char *text) {
    size_t i;
    for (i = 0; i < v->n; i++) if (v->it[i].kind == EXD_TEXT && !strcmp(ex_diagram_text(v->d, &v->it[i]), text)) return &v->it[i];
    return NULL;
}
static int count_texts(const View *v, const char *text) {
    size_t i; int c = 0;
    for (i = 0; i < v->n; i++) if (v->it[i].kind == EXD_TEXT && !strcmp(ex_diagram_text(v->d, &v->it[i]), text)) c++;
    return c;
}
static int count_kind(const View *v, int kind) { size_t i; int c = 0; for (i = 0; i < v->n; i++) if (v->it[i].kind == kind) c++; return c; }
/* the box (a node, not a panel or a label background) whose text is `text` */
static const ExDItem *node_box(const View *v, const char *text) {
    const ExDItem *t = find_text(v, text);
    size_t i;
    if (!t) return NULL;
    for (i = 0; i < v->n; i++) {
        const ExDItem *b = &v->it[i];
        if (is_shape(b) && b->fill == EXD_C_NODE_FILL && t->x >= b->x - 0.01f && t->x <= b->x + b->w + 0.01f && t->y >= b->y - 0.01f && t->y <= b->y + b->h + 0.01f) return b;
    }
    return NULL;
}
static float cx(const ExDItem *b) { return b->x + b->w / 2.0f; }
static float cy(const ExDItem *b) { return b->y + b->h / 2.0f; }
static int inside(const ExDItem *b, float x, float y, float tol) { return x >= b->x - tol && x <= b->x + b->w + tol && y >= b->y - tol && y <= b->y + b->h + tol; }

static void check_nodes(const char *name, int any_shape);
static void test_no_overlap_and_endpoints(const char *name) { check_nodes(name, 0); }
/* any_shape: a link may also end on a panel (a composite state), a start dot or a note */
static void check_nodes(const char *name, int any_shape) {
    ExDiagram *d = parse_file(name);
    View v;
    size_t i, j;
    if (!d) return;
    v = view(d);
    for (i = 0; i < v.n; i++) {
        if (!is_shape(&v.it[i]) || v.it[i].fill != EXD_C_NODE_FILL) continue;
        for (j = i + 1; j < v.n; j++) {
            const ExDItem *a = &v.it[i], *b = &v.it[j];
            if (!is_shape(b) || b->fill != EXD_C_NODE_FILL) continue;
            if (!(a->x + a->w <= b->x + 0.01f || b->x + b->w <= a->x + 0.01f || a->y + a->h <= b->y + 0.01f || b->y + b->h <= a->y + 0.01f)) {
                fprintf(stderr, "%s: two nodes overlap\n", name);
                failures++;
            }
        }
    }
    for (i = 0; i < v.n; i++) {   /* every link starts and ends on a node */
        const ExDItem *l = &v.it[i];
        size_t k;
        int ok_start = 0, ok_end = 0;
        if (l->kind != EXD_LINE) continue;
        for (k = 0; k < v.n; k++) {
            if (!is_shape(&v.it[k]) || (any_shape ? v.it[k].fill == EXD_C_LABEL_BG || v.it[k].fill == EXD_C_NONE : v.it[k].fill != EXD_C_NODE_FILL)) continue;
            if (inside(&v.it[k], v.pts[l->pt0], v.pts[l->pt0 + 1], 0.06f)) ok_start = 1;
            if (inside(&v.it[k], v.pts[l->pt0 + 2 * (l->npt - 1)], v.pts[l->pt0 + 2 * (l->npt - 1) + 1], 0.06f)) ok_end = 1;
        }
        if (!ok_start || !ok_end) fprintf(stderr, "%s: a link has an end that is on no node\n", name);
        CHECK(ok_start && ok_end);
    }
    ex_diagram_free(d);
}

static void test_flowcharts(void) {
    ExDiagram *d;
    View v;
    const ExDItem *a, *b, *c, *e;
    size_t i;
    int heads = 0;
    test_no_overlap_and_endpoints("flow1"); test_no_overlap_and_endpoints("flow2"); test_no_overlap_and_endpoints("flow3"); test_no_overlap_and_endpoints("flow4");

    d = parse_file("flow1");                              /* top to down */
    v = view(d);
    CHECK(ex_diagram_kind(d) == EXD_FLOWCHART);
    a = node_box(&v, "Start"); b = node_box(&v, "Is it working?"); c = node_box(&v, "Great"); e = node_box(&v, "Done");
    CHECK(a && b && c && e);
    if (a && b && c && e) {
        CHECK(cy(a) < cy(b) && cy(b) < cy(c) && cy(c) < cy(e));
        CHECK(fabsf(cx(a) - cx(b)) < 0.5f);                /* the first two are in one column */
        CHECK(b->kind == EXD_DIAMOND && e->kind == EXD_ELLIPSE && e->w == e->h);
        CHECK(node_box(&v, "Debug") != NULL && cx(node_box(&v, "Debug")) > cx(c));
    }
    CHECK(count_kind(&v, EXD_LINE) == 5);
    for (i = 0; i < v.n; i++) if (v.it[i].kind == EXD_LINE && v.it[i].head_end == EXD_H_ARROW) heads++;
    CHECK(heads == 5);
    {   /* the loop D --> B goes back up: its last point is above its first */
        int up = 0;
        for (i = 0; i < v.n; i++) if (v.it[i].kind == EXD_LINE && v.pts[v.it[i].pt0 + 2 * (v.it[i].npt - 1) + 1] < v.pts[v.it[i].pt0 + 1] - 0.5f) up++;
        CHECK(up == 1);
    }
    CHECK(find_text(&v, "Yes") && find_text(&v, "No"));                         /* link labels, on a background that hides the line */
    CHECK(find_text(&v, "Yes")->stroke == EXD_C_MUTED && find_text(&v, "Yes")->size < 1.0f);
    ex_diagram_free(d);

    d = parse_file("flow2");                              /* left to right, a subgraph, shapes, link styles */
    v = view(d);
    a = node_box(&v, "Browser"); b = node_box(&v, "Gateway"); c = node_box(&v, "Database");
    CHECK(a && b && c);
    if (a && b && c) CHECK(cx(a) < cx(b) && cx(b) < cx(c));
    CHECK(b && b->kind == EXD_SUBROUTINE && c && c->kind == EXD_CYLINDER && node_box(&v, "Queue") && node_box(&v, "Queue")->kind == EXD_FLAG);
    CHECK(node_box(&v, "Worker") && node_box(&v, "Worker")->kind == EXD_HEXAGON);
    CHECK(count_kind(&v, EXD_LINE) == 8);                                      /* A & B --> C makes two */
    {
        int dashed = 0, thick = 0, circle = 0;
        for (i = 0; i < v.n; i++) if (v.it[i].kind == EXD_LINE) { dashed += v.it[i].dash == 1; thick += v.it[i].width > 0.15f; circle += v.it[i].head_end == EXD_H_CIRCLE; }
        CHECK(dashed == 1 && thick == 1 && circle == 1);
    }
    {   /* the subgraph box holds its two nodes and nothing else of the nodes; its title is bold */
        const ExDItem *panel = NULL, *title = find_text(&v, "Front end");
        for (i = 0; i < v.n; i++) if (is_shape(&v.it[i]) && v.it[i].fill == EXD_C_PANEL) panel = &v.it[i];
        CHECK(panel && title && title->bold);
        if (panel && a && b) CHECK(inside(panel, a->x, a->y, 0.01f) && inside(panel, b->x + b->w, b->y + b->h, 0.01f));
        if (panel && c) CHECK(!(inside(panel, c->x, c->y, 0.0f)));
    }
    CHECK(find_text(&v, "text on a line") != NULL);
    ex_diagram_free(d);

    d = parse_file("flow3");                              /* right to left, line breaks, a loop, an invisible link */
    v = view(d);
    a = node_box(&v, "Quoted text \n second line");
    if (!a) a = node_box(&v, "Quoted text \nsecond line");
    CHECK(a != NULL);
    b = node_box(&v, "Round"); c = node_box(&v, "Stadium");
    CHECK(b && c && a);
    if (a && b && c) CHECK(cx(a) > cx(b) && cx(b) > cx(c));
    CHECK(count_kind(&v, EXD_LINE) == 8);
    {
        int loops = 0, both = 0;
        for (i = 0; i < v.n; i++) if (v.it[i].kind == EXD_LINE) { loops += v.it[i].npt == 4 && v.it[i].head_end; both += v.it[i].head_start && v.it[i].head_end; }
        CHECK(loops >= 1 && both >= 2);
    }
    CHECK(node_box(&v, "Double") && node_box(&v, "Double")->kind == EXD_DOUBLE_CIRCLE);
    CHECK(node_box(&v, "Para") && node_box(&v, "Para")->kind == EXD_PARALLELOGRAM && node_box(&v, "Alt") && node_box(&v, "Alt")->kind == EXD_PARALLELOGRAM_ALT);
    CHECK(node_box(&v, "Trap") && node_box(&v, "Trap")->kind == EXD_TRAPEZOID && node_box(&v, "Trap alt")->kind == EXD_TRAPEZOID_ALT);
    ex_diagram_free(d);

    d = parse_file("flow4");                              /* bottom to top, nested subgraphs, a cycle */
    v = view(d);
    {
        const ExDItem *x = node_box(&v, "X"), *y = node_box(&v, "Y"), *z = node_box(&v, "Z"), *w = node_box(&v, "W");
        const ExDItem *outer = NULL, *inner = NULL;
        CHECK(x && y && z && w);
        if (x && y && z && w) CHECK(cy(x) > cy(y) && cy(y) > cy(z) && cy(z) > cy(w));
        for (i = 0; i < v.n; i++) if (is_shape(&v.it[i]) && v.it[i].fill == EXD_C_PANEL) { if (!outer) outer = &v.it[i]; else inner = &v.it[i]; }
        CHECK(outer && inner && x && z);
        if (outer && inner && x && z) {
            CHECK(inside(inner, x->x, x->y, 0.01f) && inside(inner, x->x + x->w, x->y + x->h, 0.01f));   /* the inner box holds X and Y */
            CHECK(inside(outer, inner->x, inner->y, 0.01f) && inside(outer, inner->x + inner->w, inner->y + inner->h, 0.01f));
            CHECK(inside(outer, z->x, z->y, 0.01f) && !inside(outer, node_box(&v, "W")->x, node_box(&v, "W")->y, 0.0f));
        }
    }
    ex_diagram_free(d);

    {   /* short forms: ids alone, semicolons, a chain of three, quoted text with a bracket in it */
        ExDiagram *s = parse("graph TD; A-->B-->C; D[\"x]y\"]; A-- label -->D");
        View sv = view(s);
        CHECK(!ex_diagram_error(s) && count_kind(&sv, EXD_LINE) == 3);
        CHECK(find_text(&sv, "x]y") != NULL && find_text(&sv, "label") != NULL);
        ex_diagram_free(s);
    }
}

static const ExDItem *find_msg(const View *v, int dash, int head, int nth) {
    size_t i;
    for (i = 0; i < v->n; i++) {
        const ExDItem *l = &v->it[i];
        if (l->kind == EXD_LINE && l->npt == 2 && v->pts[l->pt0 + 1] == v->pts[l->pt0 + 3] && l->dash == dash && l->head_end == head && l->stroke == EXD_C_LINE && nth-- == 0) return l;
    }
    return NULL;
}

static void test_sequences(void) {
    ExDiagram *d = parse_file("seq1");
    View v = view(d);
    const ExDItem *alice, *bob, *m1, *m2, *note, *ping;
    size_t i;
    CHECK(ex_diagram_kind(d) == EXD_SEQUENCE && !ex_diagram_error(d));
    CHECK(count_texts(&v, "Alice") == 2 && count_texts(&v, "Bob") == 2);          /* boxes at the top and the bottom */
    alice = find_text(&v, "Alice"); bob = find_text(&v, "Bob");
    CHECK(alice && bob && alice->x < bob->x);
    {   /* the actor has the stadium shape and the participant the plain box */
        const ExDItem *ba = node_box(&v, "Alice"), *bb = node_box(&v, "Bob");
        CHECK(ba && bb && ba->kind == EXD_RECT && bb->kind == EXD_STADIUM);
    }
    {   /* two lifelines, dashed, from the top boxes to the bottom ones */
        int life = 0;
        for (i = 0; i < v.n; i++) { const ExDItem *l = &v.it[i]; if (l->kind == EXD_LINE && l->npt == 2 && l->dash == 1 && v.pts[l->pt0] == v.pts[l->pt0 + 2] && l->stroke == EXD_C_NODE_STROKE) life++; }
        CHECK(life == 2);
    }
    m1 = find_msg(&v, 0, EXD_H_ARROW, 0);                  /* A->>+B */
    m2 = find_msg(&v, 1, EXD_H_ARROW, 0);                  /* B-->>-A */
    CHECK(m1 && m2);
    if (m1 && m2) {
        float x0 = v.pts[m1->pt0], x1 = v.pts[m1->pt0 + 2], y1 = v.pts[m1->pt0 + 1], y2 = v.pts[m2->pt0 + 1];
        CHECK(fabsf(x0 - alice->x) < 0.05f);                              /* from Alice's lifeline */
        CHECK(fabsf(x1 - (bob->x - 0.25f)) < 0.05f);                      /* to the activation bar on Bob's */
        CHECK(y2 > y1 + 1.0f);
        CHECK(v.pts[m2->pt0] > v.pts[m2->pt0 + 2]);                       /* the answer goes back to the left */
    }
    CHECK(find_text(&v, "1. Hello Bob, how are you?") && find_text(&v, "2. Great!") && find_text(&v, "3. ping"));      /* autonumber */
    ping = find_msg(&v, 0, EXD_H_OPEN, 0);                                /* A-)B */
    CHECK(ping != NULL);
    CHECK(find_msg(&v, 1, EXD_H_CROSS, 0) != NULL);                       /* B--xA */
    {   /* the activation bar: a thin box on Bob's lifeline between the request and the answer */
        const ExDItem *bar = NULL;
        for (i = 0; i < v.n; i++) if (is_shape(&v.it[i]) && v.it[i].fill == EXD_C_ACCENT_FILL) bar = &v.it[i];
        CHECK(bar && bar->w < 0.6f && m1 && m2 && fabsf(bar->y - v.pts[m1->pt0 + 1]) < 0.05f && fabsf(bar->y + bar->h - v.pts[m2->pt0 + 1]) < 0.05f);
    }
    {   /* the loop frame holds its message, the alt frame holds its two parts, and a note sits over both lifelines */
        const ExDItem *loop = NULL, *alt = NULL;
        for (i = 0; i < v.n; i++) {
            const ExDItem *b = &v.it[i];
            if (b->kind == EXD_RECT && b->fill == EXD_C_NONE) { if (!loop) loop = b; else if (!alt) alt = b; }
        }
        CHECK(loop && alt && ping);
        if (loop && ping) CHECK(v.pts[ping->pt0 + 1] > loop->y && v.pts[ping->pt0 + 1] < loop->y + loop->h);
        if (loop && alt) CHECK(alt->y > loop->y + loop->h);
        CHECK(find_text(&v, "loop") && find_text(&v, "loop")->bold && find_text(&v, "[Every minute]") && find_text(&v, "[fail]"));
        note = node_box(&v, "A typical interaction");
        (void)note;
        {
            const ExDItem *t = find_text(&v, "A typical interaction");
            CHECK(t && t->x > alice->x && t->x < bob->x);
        }
    }
    ex_diagram_free(d);

    d = parse_file("seq2");
    v = view(d);
    CHECK(!ex_diagram_error(d));
    {
        const ExDItem *john = find_text(&v, "John"), *server = find_text(&v, "Server"), *db = find_text(&v, "Database"), *req = find_text(&v, "Request with a long message text that needs room");
        CHECK(john && server && db && req);
        if (john && server && req) {
            float w = ex_diagram_text_width(g_font, ex_diagram_text(d, req), req->tlen) * 0.95f;
            CHECK(server->x - john->x > w);                                  /* the column spacing makes room for the longest message */
        }
        CHECK(find_msg(&v, 0, EXD_H_CROSS, 0) != NULL);                      /* J-xS */
        CHECK(find_text(&v, "par") && find_text(&v, "critical") && find_text(&v, "[Branch two]") && find_text(&v, "[Network timeout]"));
        {   /* three bars: Server (nested twice? no: once) and Database, each between its activate and deactivate */
            int bars = 0;
            for (i = 0; i < v.n; i++) if (is_shape(&v.it[i]) && v.it[i].fill == EXD_C_ACCENT_FILL) bars++;
            CHECK(bars == 2);
        }
        {   /* a message to oneself is a loop of four points */
            int selfm = 0;
            for (i = 0; i < v.n; i++) if (v.it[i].kind == EXD_LINE && v.it[i].npt == 4) selfm++;
            CHECK(selfm == 1);
        }
        {   /* the notes: left of John, over Server */
            const ExDItem *l = find_text(&v, "left note"), *o = find_text(&v, "over one");
            CHECK(l && o && john && server && l->x < john->x && fabsf(o->x - server->x) < 0.05f);
        }
        {   /* messages go down the page in the order written */
            float last = -1;
            int n = 0;
            for (i = 0; i < v.n; i++) {
                const ExDItem *l = &v.it[i];
                if (l->kind == EXD_LINE && l->npt == 2 && v.pts[l->pt0 + 1] == v.pts[l->pt0 + 3] && l->stroke == EXD_C_LINE) { CHECK(v.pts[l->pt0 + 1] > last); last = v.pts[l->pt0 + 1]; n++; }
            }
            CHECK(n == 8);                                                  /* the ninth message is the one to oneself, a loop */
        }
    }
    ex_diagram_free(d);
}

static void test_errors(void) {
    static const char *const bad[] = {
        "", "   \n\n", "nonsense", "flowchart TD\nA -->", "flowchart TD\nA[open", "flowchart XX\nA-->B", "flowchart TD\n--> B", "flowchart TD\nA --- --- B",
        "flowchart TD\nA ?? B", "sequenceDiagram\nloop x\nA->>B: hi", "sequenceDiagram\nend", "sequenceDiagram\nA ~~> B: x", "sequenceDiagram\nNote over: x",
        "sequenceDiagram\nelse x", "sequenceDiagram\nNote above A: x", "sequenceDiagram\nA->>: x", "sequenceDiagram\nalt a\nend\nend", NULL
    };
    size_t i;
    char *big;
    ExDiagram *d;
    for (i = 0; bad[i]; i++) {
        d = parse(bad[i]);
        CHECK(d != NULL);
        if (d) {
            size_t n;
            if (!ex_diagram_error(d)) fprintf(stderr, "no error for: %s\n", bad[i]);
            CHECK(ex_diagram_error(d) != NULL && ex_diagram_error(d)[0]);
            ex_diagram_items(d, &n);
            CHECK(n == 0);
            ex_diagram_free(d);
        }
    }
    big = malloc(41000);
    memset(big, 'x', 41000);
    d = ex_diagram_parse(g_font, big, 41000);
    CHECK(d && ex_diagram_error(d));
    ex_diagram_free(d);
    {   /* more nodes than the limit; and a long chain under it still lays out */
        char *src = malloc(60000);
        size_t len = 0;
        int k;
        len += (size_t)snprintf(src, 60000, "flowchart TD\n");
        for (k = 0; k < 700; k++) len += (size_t)snprintf(src + len, 60000 - len, "N%d --> N%d\n", k, k + 1);
        d = ex_diagram_parse(g_font, src, len);
        CHECK(d && ex_diagram_error(d));
        ex_diagram_free(d);
        len = (size_t)snprintf(src, 60000, "flowchart TD\n");
        for (k = 0; k < 400; k++) len += (size_t)snprintf(src + len, 60000 - len, "N%d --> N%d\n", k, k + 1);
        d = ex_diagram_parse(g_font, src, len);
        CHECK(d && !ex_diagram_error(d) && ex_diagram_height(d) > 400.0f);
        ex_diagram_free(d);
        free(src);
    }
    free(big);
    d = parse("%% a comment\n---\ntitle: x\n---\nflowchart LR\n  A --> B");   /* front matter and comments before the type are skipped */
    CHECK(d && !ex_diagram_error(d) && ex_diagram_kind(d) == EXD_FLOWCHART);
    ex_diagram_free(d);
    d = ex_diagram_parse(NULL, "x", 1);                    /* no font: an error, not a crash */
    CHECK(d != NULL && ex_diagram_error(d) != NULL);
    ex_diagram_free(d);
}

static void test_cache(void) {
    ExDiagramCache *c = ex_diagcache_create();
    const char *s = "flowchart TD\nA-->B", *t = "flowchart LR\nA-->B";
    const ExDiagram *a, *b, *d;
    CHECK(c != NULL);
    ex_diagcache_begin(c);
    a = ex_diagcache_get(c, g_font, s, strlen(s));
    b = ex_diagcache_get(c, g_font, s, strlen(s));
    d = ex_diagcache_get(c, g_font, t, strlen(t));
    CHECK(a && a == b && d && d != a && ex_diagcache_misses(c) == 2 && ex_diagcache_count(c) == 2);
    ex_diagcache_begin(c);
    CHECK(ex_diagcache_lookup(c, g_font, s, strlen(s)) == a);
    ex_diagcache_begin(c);
    CHECK(ex_diagcache_count(c) == 1);
    ex_diagcache_begin(c);
    ex_diagcache_begin(c);
    CHECK(ex_diagcache_count(c) == 0);
    ex_diagcache_free(c);
}

static void test_random(void) {
    static const char *const bits[] = { "flowchart TD\n", "sequenceDiagram\n", "A", "B", "-->", "---", "-.->", "==>", "--x", "-)", "->>", "-->>", "[", "]", "(", ")", "{", "}", "((", "))", "|", "|x|",
        "subgraph s", "end", "loop x", "alt y", "else", "Note over A,B: n", "activate A", "deactivate A", "&", ";", "\n", " ", ": text", "participant P as Q", "autonumber", "\"", "<br/>", "%%", "~~~", "<-->", "A[\"q\"]", "o--o", "\xC3",
        "classDiagram\n", "class A {", "}", "A <|-- B", "A \"1\" *-- \"n\" B : x", "<<interface>> A", "stateDiagram-v2\n", "[*] --> A", "state S {", "state C <<choice>>", "note right of A : n", "erDiagram\n", "A ||--o{ B : l", "A {", "int id PK", "pie\n", "\"x\" : 5", "title t", "showData",
        "gantt\n", "section s", "T : a, 2024-01-01, 3d", "after a", "dateFormat YYYY-MM-DD", "milestone", "until a" };
    unsigned long long st = 88172645463325252ull;
    int iter;
    for (iter = 0; iter < 1500; iter++) {
        char buf[300];
        size_t len = 0, k, parts;
        ExDiagram *d;
        st ^= st << 13; st ^= st >> 7; st ^= st << 17;
        parts = 2 + (size_t)(st % 16);
        for (k = 0; k < parts; k++) {
            const char *b;
            size_t bl;
            st ^= st << 13; st ^= st >> 7; st ^= st << 17;
            b = bits[st % (sizeof bits / sizeof bits[0])];
            bl = strlen(b);
            if (len + bl < sizeof buf) { memcpy(buf + len, b, bl); len += bl; }
        }
        d = ex_diagram_parse(g_font, buf, len);
        CHECK(d != NULL);
        ex_diagram_free(d);
    }
}

/* ---- the reading view ---- */
static void ink(const uint8_t *rgb, int w, int h, ExColor bg, int *count) {
    int x, y;
    *count = 0;
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) { const uint8_t *p = rgb + ((size_t)y * (size_t)w + (size_t)x) * 3; if (abs(p[0] - bg.r) + abs(p[1] - bg.g) + abs(p[2] - bg.b) > 60) (*count)++; }
}

static void test_reading(void) {
    ExRenderer *r;
    ExTheme theme;
    ExReadingView *v;
    ExColor bg;
    uint8_t *rgb;
    int w, h, n_ok = 0, n_bad = 0, i, draws = 0;
    const char *ok = "```mermaid\nflowchart TD\nA[Start] --> B[End]\n```\n", *bad = "```mermaid\nflowchart TD\nA -->\n```\n", *plain = "```c\nflowchart TD\nA[Start] --> B[End]\n```\n";
    float h_ok, h_bad, h_plain;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    r = ex_renderer_create("mermaid-reading", 600, 400);
    CHECK(r != NULL);
    if (!r) return;
    ex_theme_init_builtin(&theme, EX_THEME_DARK);
    bg = ex_theme_color(&theme, EX_COLOR_BACKGROUND);
    v = ex_readingview_create(ok, strlen(ok));
    ex_readingview_set_viewport(v, 0, 0, 600, 400);
    ex_renderer_clear(r, bg);
    ex_readingview_draw(v, r, &theme, 16.0f);
    h_ok = ex_readingview_content_height(v);
    rgb = ex_renderer_read_rgb(r, &w, &h);
    if (rgb) { ink(rgb, w, h, bg, &n_ok); free(rgb); }
    ex_readingview_free(v);
    v = ex_readingview_create(bad, strlen(bad));
    ex_readingview_set_viewport(v, 0, 0, 600, 400);
    ex_renderer_clear(r, bg);
    ex_readingview_draw(v, r, &theme, 16.0f);
    h_bad = ex_readingview_content_height(v);
    rgb = ex_renderer_read_rgb(r, &w, &h);
    if (rgb) { ink(rgb, w, h, bg, &n_bad); free(rgb); }
    ex_readingview_free(v);
    v = ex_readingview_create(plain, strlen(plain));
    ex_readingview_set_viewport(v, 0, 0, 600, 400);
    ex_renderer_clear(r, bg);
    ex_readingview_draw(v, r, &theme, 16.0f);
    h_plain = ex_readingview_content_height(v);
    ex_readingview_free(v);
    CHECK(n_ok > 200);                                  /* a diagram was drawn */
    CHECK(h_ok > h_plain + 20.0f);                      /* and it is taller than the same text as code */
    CHECK(h_bad > 0 && h_bad != h_ok);                  /* a bad diagram shows its source (and why) */
    { /* a note with many diagrams: a layout makes at most 3, the rest follow in the next frames, and it ends */
        char *big = malloc(20000);
        size_t len = 0;
        for (i = 0; i < 20; i++) len += (size_t)snprintf(big + len, 20000 - len, "```mermaid\nflowchart TD\nA%d --> B%d\n```\n\n", i, i);
        v = ex_readingview_create(big, len);
        ex_readingview_set_viewport(v, 0, 0, 600, 400);
        ex_renderer_clear(r, bg);
        ex_readingview_draw(v, r, &theme, 16.0f);
        CHECK(ex_readingview_diagram_pending(v) == 1);
        while (ex_readingview_diagram_pending(v) && draws < 50) { ex_readingview_draw(v, r, &theme, 16.0f); draws++; }
        CHECK(!ex_readingview_diagram_pending(v) && draws >= 5 && draws < 50);
        ex_readingview_free(v);
        free(big);
    }
    ex_renderer_destroy(r);
}


/* ===== 6d-iii: class, state, entity-relationship, pie and gantt ===== */
static int count_lines_with(const View *v, int hs, int he, int dash) {
    size_t i; int c = 0;
    for (i = 0; i < v->n; i++) if (v->it[i].kind == EXD_LINE && v->it[i].head_start == hs && v->it[i].head_end == he && v->it[i].dash == dash && v->it[i].stroke == EXD_C_LINE) c++;
    return c;
}
static int has_error(const char *src) {
    ExDiagram *d = parse(src);
    size_t n = 1;
    int bad;
    if (!d) return 0;
    ex_diagram_items(d, &n);
    bad = ex_diagram_error(d) != NULL && ex_diagram_error(d)[0] && n == 0;
    if (!bad) fprintf(stderr, "no error (or items left) for: %s\n", src);
    ex_diagram_free(d);
    return bad;
}
static int no_error(const char *src) {
    ExDiagram *d = parse(src);
    int ok = d && !ex_diagram_error(d);
    if (!ok && d) fprintf(stderr, "unexpected error for %s: %s\n", src, ex_diagram_error(d));
    ex_diagram_free(d);
    return ok;
}

static void test_class(void) {
    ExDiagram *d = parse_file("class1");
    View v = view(d);
    const ExDItem *animal, *dog, *duck, *owner, *flyer;
    size_t i;
    CHECK(ex_diagram_kind(d) == EXD_CLASS && !ex_diagram_error(d));
    test_no_overlap_and_endpoints("class1");
    animal = node_box(&v, "Animal"); dog = node_box(&v, "Dog"); duck = node_box(&v, "Duck"); owner = node_box(&v, "Owner"); flyer = node_box(&v, "Flyer");
    CHECK(animal && dog && duck && owner && flyer && node_box(&v, "Toy") && node_box(&v, "Food") && node_box(&v, "Wallet"));
    if (animal && dog && duck) CHECK(cy(animal) < cy(dog) && cy(animal) < cy(duck));     /* the parent written first is on top */
    CHECK(find_text(&v, "\xC2\xAB" "abstract\xC2\xBB") && find_text(&v, "\xC2\xAB" "interface\xC2\xBB"));
    CHECK(find_text(&v, "Animal")->bold);
    {   /* Animal's box holds its members and two separators (attributes | methods) */
        int seps = 0;
        CHECK(animal && find_text(&v, "+String name") && inside(animal, find_text(&v, "+makeSound() void")->x, find_text(&v, "+makeSound() void")->y, 0.0f));
        for (i = 0; i < v.n; i++) { const ExDItem *l = &v.it[i]; if (animal && l->kind == EXD_LINE && l->npt == 2 && fabsf(v.pts[l->pt0] - animal->x) < 0.01f && fabsf(v.pts[l->pt0 + 2] - (animal->x + animal->w)) < 0.01f) seps++; }
        CHECK(seps == 2);
        CHECK(find_text(&v, "+String name")->y < find_text(&v, "+makeSound() void")->y);
    }
    CHECK(find_text(&v, "+fly() void") != NULL && find_text(&v, "+int cash") != NULL);       /* `Name : member` lines */
    CHECK(count_lines_with(&v, EXD_H_TRIANGLE, 0, 0) == 2);                             /* <|-- twice */
    CHECK(count_lines_with(&v, 0, EXD_H_TRIANGLE, 1) == 1);                             /* ..|>  */
    CHECK(count_lines_with(&v, EXD_H_DIAMOND_FILLED, 0, 0) == 1 && count_lines_with(&v, EXD_H_DIAMOND, 0, 0) == 1);
    CHECK(count_lines_with(&v, 0, EXD_H_OPEN, 1) == 1 && count_lines_with(&v, 0, EXD_H_OPEN, 0) == 1);
    CHECK(find_text(&v, "inherits") && find_text(&v, "owns") && find_text(&v, "eats") && find_text(&v, "1") && find_text(&v, "0..*"));
    ex_diagram_free(d);
    {   /* generics, labels and the unknown */
        ExDiagram *g = parse("classDiagram\nclass Box~T~ {\n  +List~int~ items\n  +get(T) T\n}\nBox --> Shelf");
        View gv = view(g);
        CHECK(!ex_diagram_error(g) && find_text(&gv, "Box<T>") && find_text(&gv, "+List<int> items") && find_text(&gv, "+get(T) T"));
        ex_diagram_free(g);
    }
    CHECK(no_error("classDiagram\nclass A[\"Shown name\"]\nA : +x"));
    CHECK(has_error("classDiagram\nclass") && has_error("classDiagram\nA <|-- ") && has_error("classDiagram\nA \"1 --> B") && has_error("classDiagram\nclass A {\n+x")
          && has_error("classDiagram\nA ?? B") && has_error("classDiagram\nA --> B extra") && has_error("classDiagram\ndirection XX"));
}

static void test_state(void) {
    ExDiagram *d = parse_file("state1");
    View v = view(d);
    const ExDItem *still, *slow, *fast, *crash, *panel = NULL, *note = NULL, *choice = NULL;
    size_t i;
    int dots = 0;
    CHECK(ex_diagram_kind(d) == EXD_STATE && !ex_diagram_error(d));
    check_nodes("state1", 1);
    still = node_box(&v, "Still"); slow = node_box(&v, "Slow"); fast = node_box(&v, "Fast"); crash = node_box(&v, "Crash");
    CHECK(still && slow && fast && crash);
    for (i = 0; i < v.n; i++) {
        const ExDItem *b = &v.it[i];
        if (b->kind == EXD_ROUND && b->fill == EXD_C_PANEL) panel = b;
        if (b->kind == EXD_RECT && b->fill == EXD_C_NOTE_FILL) note = b;
        if (b->kind == EXD_DIAMOND) choice = b;
        if (b->kind == EXD_ELLIPSE && b->fill == EXD_C_LINE) dots++;
    }
    CHECK(panel && note && choice);
    CHECK(dots == 3);                                   /* the start of the diagram, the start of Moving, and the inner dot of the end */
    if (panel && slow && fast && crash) {
        CHECK(inside(panel, slow->x, slow->y, 0.01f) && inside(panel, fast->x + fast->w, fast->y + fast->h, 0.01f));   /* the composite holds its states */
        CHECK(!inside(panel, crash->x, crash->y, 0.0f) && cy(slow) < cy(fast));
        CHECK(find_text(&v, "Moving") && find_text(&v, "Moving")->bold);
    }
    CHECK(find_text(&v, "The resting state") && find_text(&v, "with a description"));
    if (still) CHECK(inside(still, find_text(&v, "The resting state")->x, find_text(&v, "The resting state")->y, 0.0f) && still->h > 3.0f);
    CHECK(find_text(&v, "Waiting for input") && note && find_text(&v, "Waiting for input")->x > note->x);
    CHECK(find_text(&v, "push") && find_text(&v, "stop") && find_text(&v, "accelerate") && find_text(&v, "brake") && find_text(&v, "ok") && find_text(&v, "fatal"));
    {   /* the dashed link from Still to its note */
        int dashed = 0;
        for (i = 0; i < v.n; i++) if (v.it[i].kind == EXD_LINE && v.it[i].dash == 1 && v.it[i].head_end == 0) dashed++;
        CHECK(dashed == 1);
    }
    ex_diagram_free(d);
    CHECK(no_error("stateDiagram\n[*] --> A\nA --> B : go\nB --> [*]\nstate \"Long name\" as L\nL --> A\nstate F <<fork>>\nA --> F\nF --> B\nnote left of A\n  two\n  lines\nend note"));
    CHECK(has_error("stateDiagram-v2\n[*] --> [*]") && has_error("stateDiagram-v2\nstate X {\n[*] --> A") && has_error("stateDiagram-v2\n}") && has_error("stateDiagram-v2\nnote right of A\ntext")
          && has_error("stateDiagram-v2\nA -> B") && has_error("stateDiagram-v2\nstate X <<weird>>") && has_error("stateDiagram-v2\nA --> ") && has_error("stateDiagram-v2\nnote above A : x"));
    {   /* the source is not NUL-terminated (a note's text is a slice of the file): a line ending in the middle of a keyword must not be read past its end */
        static const char *const cut[] = { "stateDiagram-v2\nstate X <<ch", "classDiagram\nclass A {\n<<abst", "erDiagram\nA ||--o", "gantt\nA : 2024-01-0", "pie\n\"A\" : 1.", "sequenceDiagram\nNote over", "flowchart TD\nA -- te" };
        size_t k;
        for (k = 0; k < sizeof cut / sizeof cut[0]; k++) {
            size_t len = strlen(cut[k]);
            char *b = malloc(len);
            ExDiagram *x;
            memcpy(b, cut[k], len);
            x = ex_diagram_parse(g_font, b, len);
            CHECK(x != NULL);
            ex_diagram_free(x);
            free(b);
        }
    }
    {   /* a transition from inside a composite to outside ends on the composite's box at the parent level */
        ExDiagram *c = parse("stateDiagram-v2\nstate G {\n  A --> B\n}\nB --> C\nC --> A");
        CHECK(c && !ex_diagram_error(c));
        ex_diagram_free(c);
    }
}

static void test_er(void) {
    ExDiagram *d = parse_file("er1");
    View v = view(d);
    const ExDItem *cust = node_box(&v, "CUSTOMER"), *order = node_box(&v, "ORDER");
    CHECK(ex_diagram_kind(d) == EXD_ER && !ex_diagram_error(d));
    test_no_overlap_and_endpoints("er1");
    CHECK(cust && order && node_box(&v, "PRODUCT") && node_box(&v, "LINE-ITEM") && node_box(&v, "REGION") && node_box(&v, "ACCOUNT"));
    CHECK(find_text(&v, "CUSTOMER")->bold && find_text(&v, "custNumber") && find_text(&v, "PK") && find_text(&v, "FK") && find_text(&v, "market sector"));
    if (cust && find_text(&v, "custNumber")) CHECK(inside(cust, find_text(&v, "custNumber")->x, find_text(&v, "custNumber")->y, 0.0f));
    CHECK(count_kind(&v, EXD_LINE) >= 5);
    CHECK(count_lines_with(&v, EXD_H_ONLY_ONE, EXD_H_ZERO_MANY, 0) == 1);          /* ||--o{ */
    CHECK(count_lines_with(&v, EXD_H_ONLY_ONE, EXD_H_ONE_MANY, 0) == 1);           /* ||--|{ */
    CHECK(count_lines_with(&v, EXD_H_ONLY_ONE, EXD_H_ZERO_ONE, 1) == 1);           /* ||..o| */
    CHECK(count_lines_with(&v, EXD_H_ZERO_MANY, EXD_H_ZERO_ONE, 0) == 1);          /* }o--o| */
    CHECK(count_lines_with(&v, EXD_H_ZERO_ONE, EXD_H_ONLY_ONE, 0) == 1);           /* |o--|| */
    CHECK(find_text(&v, "places") && find_text(&v, "contains") && find_text(&v, "appears in") && find_text(&v, "lives in") && find_text(&v, "has"));
    if (cust && order) CHECK(cy(cust) < cy(order));
    ex_diagram_free(d);
    CHECK(no_error("erDiagram\ndirection LR\nA ||--|| B : x\nA {\n int id PK, FK\n}\np[Person] ||--o{ A : \"has many\""));
    CHECK(has_error("erDiagram\nA ||--o B") && has_error("erDiagram\nA {\n int id") && has_error("erDiagram\nA {\n justone\n}") && has_error("erDiagram\nA {\n string x ZZ\n}")
          && has_error("erDiagram\nA ||==o{ B") && has_error("erDiagram\nA ||--o{") && has_error("erDiagram\nA ||--o{ B extra") && has_error("erDiagram\n-- nonsense"));
}

static void test_pie(void) {
    ExDiagram *d = parse_file("pie1");
    View v = view(d);
    size_t i;
    int n = 0, texts = 0;
    float prev = 0, total = 0, first_span = 0;
    CHECK(ex_diagram_kind(d) == EXD_PIE && !ex_diagram_error(d));
    for (i = 0; i < v.n; i++) {
        const ExDItem *w = &v.it[i];
        if (w->kind != EXD_WEDGE) continue;
        if (n == 0) { CHECK(fabsf(w->a0) < 1e-4f); first_span = w->a1 - w->a0; }
        else CHECK(fabsf(w->a0 - prev) < 1e-4f);                                     /* contiguous, clockwise from the top */
        CHECK(w->a1 > w->a0 && w->w > 5.0f);
        prev = w->a1; total += w->a1 - w->a0; n++;
    }
    CHECK(n == 4 && fabsf(total - 6.2831853f) < 1e-3f);
    CHECK(fabsf(first_span / 6.2831853f - 50.05f / 108.02f) < 1e-3f);               /* the largest first */
    CHECK(find_text(&v, "46%") && find_text(&v, "40%") && find_text(&v, "9%") && find_text(&v, "5%"));
    CHECK(find_text(&v, "46%")->stroke == EXD_C_BEST);
    CHECK(find_text(&v, "Potassium [50.05]") && find_text(&v, "Calcium [42.96]") && find_text(&v, "Iron [5]"));
    CHECK(find_text(&v, "Key elements in Product X") && find_text(&v, "Key elements in Product X")->size > 1.0f);
    for (i = 0; i < v.n; i++) if (v.it[i].kind == EXD_TEXT) texts++;
    CHECK(texts >= 9);
    ex_diagram_free(d);
    {
        ExDiagram *p = parse("pie\n\"Only\" : 3");                              /* one slice is a full circle, and no showData means no values */
        View pv = view(p);
        CHECK(!ex_diagram_error(p) && count_kind(&pv, EXD_WEDGE) == 1 && pv.it[0].a1 - pv.it[0].a0 > 6.28f && find_text(&pv, "Only") && find_text(&pv, "100%"));
        ex_diagram_free(p);
    }
    CHECK(no_error("pie title Pets\n  \"Dogs\" : 386\n  \"Cats\" : 85.5\n  \"Rats\" : 15"));
    CHECK(has_error("pie") && has_error("pie\n\"A\" : -1") && has_error("pie\n\"A\" : x") && has_error("pie\n\"A : 1") && has_error("pie\n\"A\" : 0") && has_error("pie\nwords")
          && has_error("pie\n\"A\" 3") && has_error("pie frobnicate") && has_error("pie\n\"A\" : 1 2"));
}

static const ExDItem *bar_of(const View *v, const char *name) {   /* the bar whose text is inside it or just right of it */
    const ExDItem *t = find_text(v, name);
    size_t i;
    const ExDItem *best = NULL;
    if (!t) return NULL;
    for (i = 0; i < v->n; i++) {
        const ExDItem *b = &v->it[i];
        if ((b->kind == EXD_ROUND || b->kind == EXD_DIAMOND) && b->y <= t->y && t->y <= b->y + b->h && (t->x >= b->x - 0.01f)) { if (!best || b->x > best->x) { if (b->x <= t->x + 0.01f) best = b; } }
    }
    return best;
}

static void test_gantt(void) {
    ExDiagram *d = parse_file("gantt1");
    View v = view(d);
    const ExDItem *a, *b, *done, *crit, *last, *ms;
    CHECK(ex_diagram_kind(d) == EXD_GANTT && !ex_diagram_error(d));
    a = bar_of(&v, "A task"); b = bar_of(&v, "Another task"); done = bar_of(&v, "Done task"); crit = bar_of(&v, "Critical thing"); last = bar_of(&v, "The last one"); ms = bar_of(&v, "Milestone");
    CHECK(a && b && done && crit && last && ms && bar_of(&v, "Task in sec"));
    if (a && b) {
        CHECK(fabsf(b->x - (a->x + a->w)) < 0.02f);                                  /* after a1: starts where it ends */
        CHECK(fabsf(a->w / b->w - 30.0f / 20.0f) < 0.02f);                           /* widths follow the durations */
        CHECK(b->y > a->y);
    }
    if (done && crit) CHECK(fabsf(crit->x - (done->x + done->w)) < 0.02f);           /* after des1 */
    if (last && bar_of(&v, "Milestone")) CHECK(fabsf(last->w - 24.0f * (a->w / 30.0f)) < 0.05f && last->x > ms->x);    /* with no start: after the task before (the milestone) */
    CHECK(ms && ms->kind == EXD_DIAMOND && crit && crit->stroke == EXD_C_ERROR && crit->fill == EXD_C_ERROR && done && done->fill == EXD_C_NODE_FILL);
    CHECK(find_text(&v, "Section") && find_text(&v, "Section")->bold && find_text(&v, "Another") && find_text(&v, "A Gantt Diagram") && find_text(&v, "A Gantt Diagram")->size > 1.0f);
    {   /* axisFormat: month and day; ticks run left to right at equal distances */
        const ExDItem *t1 = find_text(&v, "Jan 03"), *t2 = find_text(&v, "Jan 08"), *t3 = find_text(&v, "Jan 13");
        CHECK(t1 && t2 && t3 && fabsf((t2->x - t1->x) - (t3->x - t2->x)) < 0.05f && t1->x < t2->x);
    }
    ex_diagram_free(d);
    {   /* the default axis format, leap day arithmetic and the weekday codes */
        ExDiagram *g = parse("gantt\ndateFormat YYYY-MM-DD\nT : 2024-02-28, 3d");
        View gv = view(g);
        CHECK(!ex_diagram_error(g) && find_text(&gv, "2024-02-28") && find_text(&gv, "2024-02-29") && find_text(&gv, "2024-03-01") && !find_text(&gv, "2024-02-30"));
        ex_diagram_free(g);
        g = parse("gantt\naxisFormat %a %A %j %y %e\nT : 2024-01-01, 2d");
        gv = view(g);
        CHECK(!ex_diagram_error(g) && find_text(&gv, "Mon Monday 1 24 1") && find_text(&gv, "Tue Tuesday 2 24 2"));
        ex_diagram_free(g);
    }
    CHECK(no_error("gantt\nsection S\nA : a, 2024-01-01, 2024-01-05\nC : c, 2024-01-10, 1w\nB : after a, until c\nD : milestone, 2024-01-03, 0d\nE : crit, 3h"));
    CHECK(has_error("gantt\ndateFormat DD-MM-YYYY\nA : 2024-01-01, 1d") && has_error("gantt\nA : 2024-01-05, 2024-01-01") && has_error("gantt\nA : after nothing, 1d") && has_error("gantt\nA no colon")
          && has_error("gantt\nA : abc") && has_error("gantt\ntitle only") && has_error("gantt\nA : 2024-13-01, 1d") && has_error("gantt\nA : x, y, z, w, v, u, t, s, r") && has_error("gantt\nA : 2024-01-01"));
}

static void test_more(void) {
    test_golden("class1"); test_golden("state1"); test_golden("er1"); test_golden("pie1"); test_golden("gantt1");
    test_class();
    test_state();
    test_er();
    test_pie();
    test_gantt();
}

static int write_dump(const char *file) {
    size_t len = 0;
    char *src = ex_platform_read_file(file, &len), *buf = malloc(1 << 20);
    ExDiagram *d;
    if (!src || !buf) return 1;
    d = ex_diagram_parse(g_font, src, len);
    ex_diagram_dump(d, buf, 1 << 20);
    fputs(buf, stdout);
    ex_diagram_free(d); free(buf); free(src);
    return 0;
}

static int write_ppm(const char *file, const char *out) {
    size_t len = 0;
    char *src = ex_platform_read_file(file, &len);
    ExDiagram *d;
    ExRenderer *r;
    ExTheme theme;
    int w, h;
    uint8_t *rgb;
    FILE *f;
    float size = 16.0f;
    if (!src) return 1;
    d = ex_diagram_parse(g_font, src, len);
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    r = ex_renderer_create("m", (int)(ex_diagram_width(d) * size) + 4, (int)(ex_diagram_height(d) * size) + 4);
    if (!r) return 1;
    ex_theme_init_builtin(&theme, EX_THEME_DARK);
    ex_renderer_clear(r, ex_theme_color(&theme, EX_COLOR_BACKGROUND));
    if (ex_diagram_error(d)) fprintf(stderr, "error: %s\n", ex_diagram_error(d));
    ex_diagram_draw(r, &theme, d, size, 2, 2);
    rgb = ex_renderer_read_rgb(r, &w, &h);
    f = fopen(out, "wb");
    if (!rgb || !f) return 1;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    fwrite(rgb, 3, (size_t)w * (size_t)h, f);
    fclose(f);
    free(rgb); ex_renderer_destroy(r); ex_diagram_free(d); free(src);
    return 0;
}

int main(int argc, char **argv) {
    ExFontLib *lib = ex_fontlib_create();
    CHECK(lib != NULL);
    g_font = ex_font_builtin(lib, EX_FONT_UI_REGULAR);
    CHECK(g_font != NULL);
    if (!g_font) return 1;
    if (argc > 2 && !strcmp(argv[1], "--dump")) return write_dump(argv[2]);
    if (argc > 3 && !strcmp(argv[1], "--ppm")) return write_ppm(argv[2], argv[3]);
    if (argc > 1) g_dir = argv[1];
    {
        int a, more = 0;
        for (a = 2; a < argc; a++) { if (!strcmp(argv[a], "--update")) update_goldens = 1; else if (!strcmp(argv[a], "--more")) more = 1; }
        if (more) test_more();
        else {
            test_golden("flow1"); test_golden("flow2"); test_golden("flow3"); test_golden("flow4"); test_golden("seq1"); test_golden("seq2");
            test_flowcharts();
            test_sequences();
            test_errors();
            test_cache();
            test_random();
            test_reading();
        }
    }
    ex_fontlib_destroy(lib);
    if (failures) fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
