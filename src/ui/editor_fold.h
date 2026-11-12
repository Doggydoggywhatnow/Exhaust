#ifndef EXHAUST_EDITOR_FOLD_H
#define EXHAUST_EDITOR_FOLD_H

#include <stddef.h>

#include "exhaust_buffer.h"
#include "exhaust_markdown.h"

/* A region that can be folded: the header line stays, the lines first..last (inclusive) are hidden. A heading
 * covers the lines up to the next heading of the same or a higher level (or the end); a list item covers
 * its continuation lines and nested items. hs and he are the byte offsets of the start of line `first` and
 * of the start of the line after `last` (or the end of the text). depth is how many other regions enclose it. */
typedef struct ExFoldRange {
    size_t hdr, first, last;
    size_t hs, he;
    unsigned depth;
    int heading;
} ExFoldRange;

typedef struct ExFolds {
    ExFoldRange *r;          /* ordered by header line */
    size_t n, cap;
} ExFolds;

void ex_folds_free(ExFolds *f);
/* Computes the regions from the Markdown tokens: top-level headings if `headings`, list items if `lists`. */
int ex_folds_compute(ExFolds *f, const ExMd *md, const ExBuffer *b, int headings, int lists);
/* Index of the region whose first hidden line starts at byte hs, or -1. */
long ex_folds_find_hs(const ExFolds *f, size_t hs);
/* Index of the region with this header line, or -1. */
long ex_folds_find_hdr(const ExFolds *f, size_t hdr);

#endif
