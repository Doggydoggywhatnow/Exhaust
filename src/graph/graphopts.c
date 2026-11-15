#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_graphopts.h"

void ex_graphopts_defaults(ExGraphOptions *o) {
    int i;
    memset(o, 0, sizeof *o);
    o->show_orphans = o->show_unresolved = o->show_attachments = 1;
    for (i = 0; i < EX_GRAPH_MAX_GROUPS; i++) o->groups[i].color = i;
    o->center = 0.3f; o->repel = 10.0f; o->link_strength = 1.0f; o->link_distance = 30.0f;
    o->text_fade = 0.0f; o->node_size = 1.0f; o->link_thickness = 1.0f;
}

static float cl(float x, float lo, float hi) { return !(x >= lo) ? lo : x > hi ? hi : x; }

void ex_graphopts_clamp(ExGraphOptions *o) {
    int i;
    o->search[EX_GRAPH_QUERY_MAX - 1] = 0;
    o->show_orphans = o->show_orphans != 0; o->show_unresolved = o->show_unresolved != 0; o->show_attachments = o->show_attachments != 0;
    o->arrows = o->arrows != 0;
    for (i = 0; i < EX_GRAPH_MAX_GROUPS; i++) {
        o->groups[i].query[EX_GRAPH_QUERY_MAX - 1] = 0;
        if (o->groups[i].color < 0 || o->groups[i].color > 5) o->groups[i].color = i;
    }
    o->center = cl(o->center, 0.0f, 1.0f); o->repel = cl(o->repel, 0.0f, 20.0f); o->link_strength = cl(o->link_strength, 0.0f, 1.0f);
    o->link_distance = cl(o->link_distance, 10.0f, 200.0f); o->text_fade = cl(o->text_fade, -3.0f, 3.0f);
    o->node_size = cl(o->node_size, 0.3f, 3.0f); o->link_thickness = cl(o->link_thickness, 0.3f, 4.0f);
}

int ex_graphopts_filters_equal(const ExGraphOptions *a, const ExGraphOptions *b) {
    int i;
    if (strcmp(a->search, b->search) || a->show_orphans != b->show_orphans || a->show_unresolved != b->show_unresolved || a->show_attachments != b->show_attachments) return 0;
    for (i = 0; i < EX_GRAPH_MAX_GROUPS; i++) if (strcmp(a->groups[i].query, b->groups[i].query)) return 0;   /* a group's colour is the view's business */
    return 1;
}

static void put(char *dst, const char *s) { if (!s) return; strncpy(dst, s, EX_GRAPH_QUERY_MAX - 1); dst[EX_GRAPH_QUERY_MAX - 1] = 0; }
static void getb(int *dst, const char *s) { if (s) *dst = !strcmp(s, "true") || !strcmp(s, "1"); }
static void getf(float *dst, const char *s) { char *end; float v; if (!s || !*s) return; v = strtof(s, &end); if (*end == 0) *dst = v; }

void ex_graphopts_load(ExGraphOptions *o, const char *(*get)(void *, const char *), void *user) {
    int i;
    char key[40];
    ex_graphopts_defaults(o);
    put(o->search, get(user, "search"));
    getb(&o->show_orphans, get(user, "show_orphans")); getb(&o->show_unresolved, get(user, "show_unresolved")); getb(&o->show_attachments, get(user, "show_attachments"));
    for (i = 0; i < EX_GRAPH_MAX_GROUPS; i++) {
        const char *c;
        snprintf(key, sizeof key, "group_%d_query", i + 1); put(o->groups[i].query, get(user, key));
        snprintf(key, sizeof key, "group_%d_color", i + 1); c = get(user, key); if (c && *c) o->groups[i].color = atoi(c);
    }
    getf(&o->center, get(user, "center_force")); getf(&o->repel, get(user, "repel_force")); getf(&o->link_strength, get(user, "link_force"));
    getf(&o->link_distance, get(user, "link_distance"));
    getb(&o->arrows, get(user, "arrows")); getf(&o->text_fade, get(user, "text_fade")); getf(&o->node_size, get(user, "node_size"));
    getf(&o->link_thickness, get(user, "link_thickness"));
    ex_graphopts_clamp(o);
}

static void setf(void (*set)(void *, const char *, const char *), void *user, const char *key, float v) { char b[32]; snprintf(b, sizeof b, "%.3g", (double)v); set(user, key, b); }
static void setb(void (*set)(void *, const char *, const char *), void *user, const char *key, int v) { set(user, key, v ? "true" : "false"); }

void ex_graphopts_save(const ExGraphOptions *o, void (*set)(void *, const char *, const char *), void *user) {
    int i;
    char key[40], b[16];
    set(user, "search", o->search);
    setb(set, user, "show_orphans", o->show_orphans); setb(set, user, "show_unresolved", o->show_unresolved); setb(set, user, "show_attachments", o->show_attachments);
    for (i = 0; i < EX_GRAPH_MAX_GROUPS; i++) {
        snprintf(key, sizeof key, "group_%d_query", i + 1); set(user, key, o->groups[i].query);
        snprintf(key, sizeof key, "group_%d_color", i + 1); snprintf(b, sizeof b, "%d", o->groups[i].color); set(user, key, b);
    }
    setf(set, user, "center_force", o->center); setf(set, user, "repel_force", o->repel); setf(set, user, "link_force", o->link_strength);
    setf(set, user, "link_distance", o->link_distance);
    setb(set, user, "arrows", o->arrows); setf(set, user, "text_fade", o->text_fade); setf(set, user, "node_size", o->node_size);
    setf(set, user, "link_thickness", o->link_thickness);
}

int ex_graphopts_equal(const ExGraphOptions *a, const ExGraphOptions *b) {
    int i;
    if (!ex_graphopts_filters_equal(a, b) || a->arrows != b->arrows) return 0;
    for (i = 0; i < EX_GRAPH_MAX_GROUPS; i++) if (a->groups[i].color != b->groups[i].color) return 0;
    return a->center == b->center && a->repel == b->repel && a->link_strength == b->link_strength && a->link_distance == b->link_distance &&
           a->text_fade == b->text_fade && a->node_size == b->node_size && a->link_thickness == b->link_thickness;
}
