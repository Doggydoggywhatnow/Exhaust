/* Reading mode, the model: a note as blocks of styled inline pieces, built with md4c plus Exhaust's
 * own handling of what md4c does not know (front matter, comments, callouts, wikilinks and embeds,
 * highlights, tags, footnotes, block IDs). The reading view draws it; the PDF export will too.
 * Nothing here changes the note. */
#ifndef EXHAUST_READING_H
#define EXHAUST_READING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { EX_RS_BOLD = 1, EX_RS_ITALIC = 2, EX_RS_STRIKE = 4, EX_RS_HIGHLIGHT = 8, EX_RS_CODE = 16, EX_RS_SUB = 32, EX_RS_SUP = 64, EX_RS_UNDERLINE = 128 };

typedef enum ExRSpanKind {
    EX_RSK_TEXT,
    EX_RSK_LINK_EXTERNAL,    /* target is a URL */
    EX_RSK_LINK_INTERNAL,    /* target is a note name, optionally with #Heading or #^block */
    EX_RSK_TAG,              /* target is the tag without the # */
    EX_RSK_FOOTNOTE_REF,     /* text is the number, target the footnote's name */
    EX_RSK_IMAGE,            /* a placeholder until images are drawn (6c); text is the alt text, target the source */
    EX_RSK_EMBED,            /* ![[target]]: a placeholder until embeds are drawn (6c) */
    EX_RSK_MATH,             /* text is the LaTeX source (drawn as source until 6d) */
    EX_RSK_BREAK             /* a hard line break */
} ExRSpanKind;

typedef struct ExRSpan {
    ExRSpanKind kind;
    unsigned style;
    char *text;
    char *target;
} ExRSpan;

typedef enum ExRBlockKind {
    EX_RBK_PARAGRAPH, EX_RBK_HEADING, EX_RBK_CODE, EX_RBK_MATH, EX_RBK_HR, EX_RBK_TABLE_ROW, EX_RBK_CALLOUT_TITLE, EX_RBK_FOOTNOTES_TITLE
} ExRBlockKind;

typedef struct ExRCell { ExRSpan *spans; size_t n; int align; } ExRCell;   /* align: 0 none, 1 left, 2 center, 3 right */

#define EX_READ_MAX_FRAMES 8

typedef struct ExRBlock {
    ExRBlockKind kind;
    int level;                          /* heading level 1 to 6 */
    ExRSpan *spans; size_t nspans;      /* paragraph, heading, callout title */
    char *code; char *lang;             /* code block text and language; math block source */
    ExRCell *cells; size_t ncells; int header_row;   /* table row */
    /* the containers this block is in, outermost first: a quote has callout -1, a callout its type (0 to 12) */
    int nframes; int frame_callout[EX_READ_MAX_FRAMES]; int frame_id[EX_READ_MAX_FRAMES]; int frame_fold[EX_READ_MAX_FRAMES];
    int list_depth;
    char *marker;                       /* the bullet or number of a list item, on its first block */
    int task;                           /* 0 none, 1 open, 2 done, 3 other */
} ExRBlock;

typedef struct ExReading ExReading;

/* Builds the reading model of a note's text (front matter, comments and footnote definitions removed from the flow). */
ExReading *ex_reading_build(const char *text, size_t len);
void ex_reading_free(ExReading *r);
size_t ex_reading_block_count(const ExReading *r);
const ExRBlock *ex_reading_block(const ExReading *r, size_t i);
/* The note as plain text, one block to a line (for screen readers, search and tests). malloc'ed. */
char *ex_reading_plain_text(const ExReading *r);
/* A readable dump of the model, one line per block, with the inline styles written as Markdown-like marks (the golden file of the tests). malloc'ed. */
char *ex_reading_dump(const ExReading *r);

#ifdef __cplusplus
}
#endif
#endif
