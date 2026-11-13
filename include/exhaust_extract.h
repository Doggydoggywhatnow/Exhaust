/* What the index takes out of a note: links, headings (sections), block ids, tasks, tags, the line starts and the
 * properties of the frontmatter. Pure functions of the text (the Markdown tokenizer decides what is a link and
 * what is code), so they are also what the fuzz target and the tests call. Offsets are bytes of the text;
 * lines count from 1. */
#ifndef EXHAUST_EXTRACT_H
#define EXHAUST_EXTRACT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum ExLinkKind {
    EX_LINK_WIKI = 0,       /* [[Note]] */
    EX_LINK_EMBED = 1,      /* ![[Note]] */
    EX_LINK_MD = 2,         /* [text](Note.md) */
    EX_LINK_MD_EMBED = 3,   /* ![alt](image.png) */
    EX_LINK_PROP = 4        /* [[Note]] inside a property of the frontmatter */
} ExLinkKind;
typedef struct ExXLink {
    int kind;
    char *target;       /* the note or file as written, without #heading, ^block and alias; "" for a link inside the same note */
    char *heading;      /* after '#', or NULL */
    char *block;        /* after '#^', or NULL */
    char *alias;        /* the shown text, or NULL */
    size_t start, end;  /* the whole link, brackets included */
    uint32_t line;
} ExXLink;
typedef struct ExXHeading { int level; char *text; size_t start, end, section_end; uint32_t line; } ExXHeading;   /* start..end: the heading; ..section_end: its whole section */
typedef struct ExXBlock { char *id; size_t start, end; uint32_t line; } ExXBlock;                                  /* start..end: the paragraph, item or quote the id ends */
typedef struct ExXTask { int state; size_t start, end; uint32_t line; char *text; } ExXTask;                      /* state 0 open, 1 done, 2 any other mark; start..end: the line */
typedef struct ExXTag { char *tag; size_t start; uint32_t line; int in_props; } ExXTag;
/* A property of the frontmatter. value is the text as written after "name:" (trimmed), items the elements of a list. */
typedef enum ExPropType { EX_PROP_TEXT = 0, EX_PROP_LIST, EX_PROP_NUMBER, EX_PROP_CHECKBOX, EX_PROP_DATE, EX_PROP_DATETIME, EX_PROP_LINKS } ExPropType;
typedef struct ExXProp {
    char *name;
    int type;           /* ExPropType, from the value */
    char *value;        /* scalar: the text; list: the items joined by a newline */
    size_t start, end;  /* the lines of the property in the note, end after the last line break */
    uint32_t line;
} ExXProp;
typedef struct ExExtract {
    ExXLink *links; size_t nlinks;
    ExXHeading *headings; size_t nheadings;
    ExXBlock *blocks; size_t nblocks;
    ExXTask *tasks; size_t ntasks;
    ExXTag *tags; size_t ntags;
    ExXProp *props; size_t nprops;
    size_t front_start, front_end;      /* the frontmatter block (the lines between the dashes excluded), 0, 0 if none */
    int front_valid;                    /* the frontmatter parsed as YAML */
    uint32_t *line_starts; size_t nlines;
} ExExtract;
ExExtract *ex_extract(const char *text, size_t len);
void ex_extract_free(ExExtract *x);
/* The 1-based line that holds a byte offset. */
uint32_t ex_extract_line_of(const ExExtract *x, size_t offset);
/* Splits a link as written in [[...]] into its parts (the way the tokenizer does for wikilinks): fills target, heading
 * and block (malloc'ed or NULL). Used by the link suggestions and by rename. */
#ifdef __cplusplus
}
#endif
#endif
