/* The graph's layout (sub-slice 5a): a force-directed simulation with the Barnes-Hut approximation, run on a worker thread so the UI
 * thread never computes a force. The forces are the ones d3-force uses (repulsion between every pair of nodes, a spring along every
 * edge, a pull to the centre, cooling), so a graph settles into the familiar shape; repulsion is computed with a quadtree (a distant
 * group of nodes acts as one), which costs about n log n instead of n squared.
 *
 * Threads. ex_layout_create starts a worker. Each step it writes the positions into one of three buffers and swaps it in with one
 * atomic exchange (a triple buffer: no lock, and neither side ever waits for the other). The UI thread calls ex_layout_positions once a
 * frame and draws what it returns. Every function except ex_layout_create and ex_layout_free may be called from the UI thread only.
 *
 * Space. Positions are in layout units (the spring length is 30). The view scales and moves them; the layout knows nothing of pixels. */
#ifndef EXHAUST_LAYOUT_H
#define EXHAUST_LAYOUT_H
#include <stddef.h>
#include <stdint.h>
#include "exhaust_graph.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExLayout ExLayout;

typedef struct ExLayoutStats {
    int nodes;
    unsigned long iterations;        /* steps taken */
    unsigned long interactions;      /* repulsion terms of the last step: a node against a node or against a group of them */
    float alpha;                     /* the temperature: 1 at the start, falls each step, the layout rests below 0.001 */
    int settled;                     /* 1 once it has cooled and stopped stepping (ex_layout_reheat wakes it) */
    float start_alpha;               /* the temperature it started at (1 cold; less when it started from an earlier layout): fixed at creation, for the tests */
} ExLayoutStats;

/* Starts the layout of g. prev_g and prev_xy (n_prev * 2 floats, from ex_layout_positions of an earlier layout) are optional: a node
 * that was in the earlier graph (same kind and path) starts where it was and the new ones near a neighbour, so a changed vault moves
 * the picture a little instead of shuffling it, and the layout starts warm. The graph is copied; g may be freed at once. Returns NULL
 * on failure. seed makes the start reproducible. */
ExLayout *ex_layout_create(const ExGraph *g, const ExGraph *prev_g, const float *prev_xy, uint32_t seed);
/* Stops the worker and waits for it. */
void ex_layout_free(ExLayout *l);

/* UI thread: the newest finished positions, x and y interleaved, one pair per node of the graph, valid until the next call. generation
 * grows with every publication, so the caller can tell whether anything changed. Never blocks. */
const float *ex_layout_positions(ExLayout *l, uint64_t *generation);
/* For the tests: 1 if the buffer the UI is reading is whole (written start to end by one step); a torn copy would return 0. */
int ex_layout_front_consistent(const ExLayout *l);
/* The forces (5b). charge is the repulsion (-30 by default: negative pushes apart), link_strength a multiple of the spring's strength (1),
 * link_distance the spring's length (30) and center the pull to the middle (0 to 1; 0 by default here, which is what 5a did). They take
 * effect at the next step and wake a layout that has settled. */
typedef struct ExLayoutParams { float center, charge, link_strength, link_distance; } ExLayoutParams;
void ex_layout_set_params(ExLayout *l, const ExLayoutParams *p);
void ex_layout_reheat(ExLayout *l);      /* wakes a settled layout (after a change of forces), at a middling temperature */
void ex_layout_stats(const ExLayout *l, ExLayoutStats *out);

/* The repulsion on every node by the two methods, for the tests: exact (all pairs) and Barnes-Hut (theta is the opening angle: 0.9 is
 * what the layout uses; a smaller one is more exact and slower). fx and fy have n entries each; the BH one returns the number of
 * interactions it made. */
void ex_layout_repulsion_exact(const float *xy, int n, float charge, float *fx, float *fy);
unsigned long ex_layout_repulsion_bh(const float *xy, int n, float charge, float theta, float *fx, float *fy);

#ifdef __cplusplus
}
#endif
#endif
