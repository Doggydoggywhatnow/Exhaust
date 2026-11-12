#ifndef EXHAUST_MD_INTERNAL_H
#define EXHAUST_MD_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "exhaust_markdown.h"

/* A span while a block is being parsed: absolute offsets, no parent yet. */
typedef struct RawSpan { uint8_t kind, flags; uint16_t level; size_t start, end; uint32_t seq; } RawSpan;

typedef struct Ctx {
    const char *t;
    size_t len;
    RawSpan *sp;
    size_t nsp, cap;
    uint32_t seq;
    int oom;
} Ctx;

void md_emit(Ctx *c, ExMdKind kind, size_t start, size_t end, int flags, int level);

/* A line of a container's content: the range after the container's prefix, without the line break. */
typedef struct VLine { size_t s, e; } VLine;

/* The pieces of one inline run: a paragraph's lines, or the single range of a heading or a table cell. */
void md_inline(Ctx *c, const VLine *lines, size_t n);

#define MD_MAX_DEPTH 32

#endif
