/* The Markdown tokenizer. It turns a note's text into spans (a kind, a byte range, a level or flags,
 * and the span each one sits inside) and never changes the text. The kinds, the rules and the
 * callout types are defined in docs/formats.md ("Markdown tokens and the HTML subset").
 *
 * The text is cut into top-level blocks (a paragraph, a list with all its items, a table, a quote,
 * ...) separated by blank lines; each block keeps its own spans with offsets relative to its start.
 * Parsing a block depends only on the text from its first line on, so after an edit only the blocks
 * around it are tokenized again (ex_md_update) and everything after them is moved, not re-read. The
 * result of an incremental update is always equal to tokenizing the whole text. */
#ifndef EXHAUST_MARKDOWN_H
#define EXHAUST_MARKDOWN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExMdKind {
    /* blocks */
    EX_MD_FRONTMATTER = 1, EX_MD_HEADING, EX_MD_PARAGRAPH, EX_MD_BLOCKQUOTE, EX_MD_CALLOUT, EX_MD_LIST, EX_MD_LIST_ITEM, EX_MD_TASK,
    EX_MD_CODE_BLOCK, EX_MD_CODE_INFO, EX_MD_CODE_BODY, EX_MD_MATH_BLOCK, EX_MD_TABLE, EX_MD_TABLE_ROW, EX_MD_TABLE_CELL, EX_MD_HR,
    EX_MD_FOOTNOTE_DEF, EX_MD_COMMENT_BLOCK, EX_MD_HTML_BLOCK, EX_MD_CALLOUT_TYPE, EX_MD_CALLOUT_TITLE,
    /* inline */
    EX_MD_EMPHASIS, EX_MD_STRONG, EX_MD_STRIKE, EX_MD_HIGHLIGHT, EX_MD_CODE_SPAN, EX_MD_MATH_INLINE, EX_MD_LINK, EX_MD_IMAGE,
    EX_MD_LINK_TEXT, EX_MD_LINK_DEST, EX_MD_LINK_TITLE, EX_MD_WIKILINK, EX_MD_EMBED, EX_MD_LINK_TARGET, EX_MD_LINK_HEADING,
    EX_MD_LINK_BLOCK, EX_MD_LINK_ALIAS, EX_MD_AUTOLINK, EX_MD_FOOTNOTE_REF, EX_MD_INLINE_FOOTNOTE, EX_MD_TAG, EX_MD_BLOCK_ID,
    EX_MD_COMMENT_INLINE, EX_MD_HTML_INLINE, EX_MD_ESCAPE, EX_MD_HARD_BREAK,
    /* syntax characters of any construct */
    EX_MD_MARKER,
    EX_MD_KIND_COUNT
} ExMdKind;

/* Callout types (the level of an EX_MD_CALLOUT span). */
typedef enum ExCalloutType {
    EX_CALLOUT_NOTE, EX_CALLOUT_ABSTRACT, EX_CALLOUT_INFO, EX_CALLOUT_TODO, EX_CALLOUT_TIP, EX_CALLOUT_SUCCESS, EX_CALLOUT_QUESTION,
    EX_CALLOUT_WARNING, EX_CALLOUT_FAILURE, EX_CALLOUT_DANGER, EX_CALLOUT_BUG, EX_CALLOUT_EXAMPLE, EX_CALLOUT_QUOTE, EX_CALLOUT_COUNT
} ExCalloutType;
/* Fold state (the flags of an EX_MD_CALLOUT span). */
enum { EX_CALLOUT_NOT_FOLDABLE = 0, EX_CALLOUT_FOLD_OPEN = 1, EX_CALLOUT_FOLD_CLOSED = 2 };

/* The canonical type for a name or an alias, without regard to case; unknown names are NOTE.
 * *known is set to 1 if the name is one of the documented types or aliases. */
ExCalloutType ex_callout_type(const char *name, size_t len, int *known);
const char *ex_callout_name(ExCalloutType t);     /* "note", "abstract", ... */
const char *ex_md_kind_name(ExMdKind k);          /* "heading", "wikilink", ... (the names in docs/formats.md) */

typedef struct ExMdSpan {
    ExMdKind kind;
    uint8_t flags;
    uint16_t level;
    size_t start, end;                 /* bytes of the text, end exclusive */
    ExMdKind parent_kind;              /* the span this one sits inside, or 0 at the top of a block */
    size_t parent_start, parent_end;
} ExMdSpan;

typedef struct ExMd ExMd;
ExMd *ex_md_create(void);
void ex_md_free(ExMd *m);

/* Tokenizes the whole text (replacing everything). */
void ex_md_tokenize(ExMd *m, const char *text, size_t len);

typedef struct ExMdRange { size_t start, end; } ExMdRange;
/* The text changed: `removed` bytes at `offset` were replaced by `inserted` bytes; `text` is the new
 * text, `len` its length. Tokenizes again only from the block before the edit up to the first block
 * boundary after it that is unchanged. Returns the range of the new text whose spans may differ from
 * before (everything outside it only moved). Equal in result to ex_md_tokenize. */
ExMdRange ex_md_update(ExMd *m, const char *text, size_t len, size_t offset, size_t removed, size_t inserted);

/* Calls fn for every span that overlaps [from, to), ordered by start and, for equal starts, outer
 * before inner. Return non-zero from fn to stop. */
typedef int (*ExMdVisit)(const ExMdSpan *span, void *user);
void ex_md_visit(const ExMd *m, size_t from, size_t to, ExMdVisit fn, void *user);

size_t ex_md_block_count(const ExMd *m);
void ex_md_block_range(const ExMd *m, size_t i, size_t *start, size_t *end);
size_t ex_md_span_count(const ExMd *m);
size_t ex_md_last_retokenized(const ExMd *m);   /* how many top-level blocks the last call tokenized (for tests) */

#ifdef __cplusplus
}
#endif
#endif
