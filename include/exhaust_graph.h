/* The graph's data (sub-slice 5a): every note, the notes and files links lead to, and the links between them, as arrays ready for a
 * layout. Built from the index's `links` table by one pass on the service thread (never on the UI thread); the result is an immutable
 * snapshot that the view and the layout share.
 *
 * Nodes. A node for every Markdown note (also one with no links), one for every other file that a link resolves to (an image, a PDF: an
 * attachment), and one for every name a link points at that does not exist yet (an unresolved link; every note that links to the same
 * name, compared by its name key, shares the one node). Edges. One for each pair of nodes that a link joins, from the note that holds
 * the link to what it points at: repeated links give one edge, a link to the note itself gives none. A node's degree is the number of
 * edges that touch it (it sets the size of the circle). Links are resolved by the rules of exhaust_links.h; the answer for the same
 * name from the same folder is remembered, so a repeated link asks the index once (links to many different names still ask once each: the
 * performance vault's 20,000 links took about 19,000 questions and 0.4 s in a build without optimization, on the service thread). */
#ifndef EXHAUST_GRAPH_H
#define EXHAUST_GRAPH_H
#include <stddef.h>
#include <stdint.h>
#include "exhaust_graphopts.h"
#include "exhaust_index.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExGraphKind { EX_GRAPH_NOTE, EX_GRAPH_UNRESOLVED, EX_GRAPH_ATTACHMENT } ExGraphKind;

typedef struct ExGraphNode {
    char *path;          /* a note or attachment: its path from the vault; unresolved: the name the link uses */
    int kind;            /* ExGraphKind */
    uint32_t degree;
    int64_t ctime;       /* a note: when its file was created (the index's files.ctime; 0 if unknown); other nodes: 0 */
    uint8_t group;       /* 0: in no group; n: in the group n of the options (5b), which colours it */
} ExGraphNode;

typedef struct ExGraphEdge { uint32_t from, to; } ExGraphEdge;   /* node indexes; from holds the link */

typedef struct ExGraph {
    ExGraphNode *nodes;
    size_t n;
    ExGraphEdge *edges;  /* sorted by (from, to), no duplicates */
    size_t ne;
    size_t links_read;   /* rows of the links table */
    size_t resolves;     /* questions put to the index to resolve links (at most links_read; fewer when a name from one folder repeats) */
} ExGraph;

/* Reads the whole graph. Returns 0, or -1 if the index cannot be read (out is left empty). */
int ex_graph_load(ExIndex *r, ExGraph *out);
void ex_graph_free(ExGraph *g);
/* Applies the filters and groups of the options (sub-slice 5b) to a graph just read: the notes a search finds (the attachments and unresolved
 * names they link to stay), the toggles for orphans, unresolved names and attachments, and the group of every note. Removes nodes and
 * the edges that touch them, and counts degrees again. A query that does not parse is ignored (-1 for it) and the rest is applied: *bad
 * (may be NULL) gets 1 for the search, or 2 to 7 for group 1 to 6, as a bit set, and msg (may be NULL) gets one line for each such query saying why it
 * did not parse (\n between the lines, cut to msgcap). Runs the search engine, so call it on the service thread.
 * Returns 0, or -1 on a failed search. */
int ex_graph_apply_options(ExIndex *r, const char *vault_root, const ExGraphOptions *o, ExGraph *g, unsigned *bad, char *msg, size_t msgcap);
/* The local graph (sub-slice 5c): keeps the node of the note `center` (a path) and every node at most `depth` links from it, a link counting in
 * either direction, and removes the rest with the edges that touch them (degrees are counted again). Works on the graph in memory, in
 * time proportional to its size: call it on the service thread. Returns 0, or 1 if the centre is not a node of the graph (the graph is then
 * left empty), or -1 on failure. */
int ex_graph_local(ExGraph *g, const char *center, int depth);
/* The name to show for a node: the file name without a folder and without ".md" (a pointer into the node's path). */
const char *ex_graph_label(const ExGraphNode *n, size_t *len);

#ifdef __cplusplus
}
#endif
#endif
