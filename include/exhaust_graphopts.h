/* The graph's settings (sub-slice 5b): what the gear panel changes. Filters and groups decide which nodes the graph has and which are
 * coloured (applied on the service thread by ex_graph_apply_options); forces change the layout; display options change only how the
 * view draws. The keys are the [graph] keys of docs/formats.md. */
#ifndef EXHAUST_GRAPHOPTS_H
#define EXHAUST_GRAPHOPTS_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EX_GRAPH_MAX_GROUPS 6
#define EX_GRAPH_QUERY_MAX 200

typedef struct ExGraphGroup { char query[EX_GRAPH_QUERY_MAX]; int color; } ExGraphGroup;   /* color: 0 to 5, a slot (formats.md) */

typedef struct ExGraphOptions {
    /* filters (change the data) */
    char search[EX_GRAPH_QUERY_MAX];
    int show_orphans, show_unresolved, show_attachments;
    int local_depth;                 /* the local graph (5c): how many links from the open note, 1 to 5 */
    ExGraphGroup groups[EX_GRAPH_MAX_GROUPS];
    /* forces (change the layout) */
    float center, repel, link_strength, link_distance;
    /* display (change the drawing) */
    int arrows;
    float text_fade, node_size, link_thickness;
} ExGraphOptions;

void ex_graphopts_defaults(ExGraphOptions *o);
void ex_graphopts_clamp(ExGraphOptions *o);                     /* puts every value in its range, and the strings' ends in place */
/* 1 if the filters and groups are the same (a change of them needs the graph read again; the others do not). */
int ex_graphopts_filters_equal(const ExGraphOptions *a, const ExGraphOptions *b);
int ex_graphopts_equal(const ExGraphOptions *a, const ExGraphOptions *b);   /* every field the same */
/* The keys of the [graph] section. get returns the text of a key or NULL; set stores one. Unknown or bad values keep the default. */
void ex_graphopts_load(ExGraphOptions *o, const char *(*get)(void *user, const char *key), void *user);
void ex_graphopts_save(const ExGraphOptions *o, void (*set)(void *user, const char *key, const char *value), void *user);

#ifdef __cplusplus
}
#endif
#endif
