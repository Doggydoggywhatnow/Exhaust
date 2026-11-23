/* A YAML document as a tree that keeps everything (sub-slice 6b-ii): the order of keys, comments excepted, the style of every scalar (plain, quoted, block), anchors,
 * tags and aliases, so a `.base` file read and written again keeps every key Exhaust does not know. It is read and written with libyaml.
 * Only the first document of a file is read. Comments are not kept (libyaml does not report them). */
#ifndef EXHAUST_YAML_H
#define EXHAUST_YAML_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExYKind { EXY_SCALAR, EXY_SEQ, EXY_MAP, EXY_ALIAS } ExYKind;
typedef enum ExYStyle { EXY_PLAIN = 1, EXY_SINGLE = 2, EXY_DOUBLE = 3, EXY_LITERAL = 4, EXY_FOLDED = 5 } ExYStyle;   /* the values of libyaml's scalar styles */

typedef struct ExYNode {
    ExYKind kind;
    char *value;                 /* a scalar's text, or an alias's anchor name */
    char *anchor, *tag;          /* or NULL */
    int style;                   /* a scalar's ExYStyle; for a list or mapping 1 when written in flow style ([a, b], {a: 1}) */
    struct ExYNode **kids;       /* a list's items, or a mapping's key and value in turn */
    size_t n, cap;
} ExYNode;

/* Reads a document. An empty file is an empty mapping. NULL with a reason in err when it is not valid YAML. */
ExYNode *ex_y_parse(const char *text, size_t len, char *err, size_t errcap);
/* Writes it with two spaces of indent, lists indented under their keys, no line folding (malloc'ed, NUL terminated). */
char *ex_y_write(const ExYNode *root, size_t *len);
void ex_y_free(ExYNode *n);
ExYNode *ex_y_copy(const ExYNode *n);

ExYNode *ex_y_scalar(const char *text);                  /* plain when it can be, else quoted (text that YAML would read as a number, a boolean or null is quoted) */
ExYNode *ex_y_scalar_style(const char *text, int style);
ExYNode *ex_y_map(void);
ExYNode *ex_y_seq(void);
int ex_y_push(ExYNode *seq, ExYNode *item);              /* takes the item; 0, or -1 */
/* Mappings: keys are compared as scalar text. get returns the value node or NULL. set replaces the value (freeing the old one) or appends the pair; takes the value. */
ExYNode *ex_y_get(const ExYNode *map, const char *key);
int ex_y_set(ExYNode *map, const char *key, ExYNode *value);
int ex_y_remove(ExYNode *map, const char *key);
size_t ex_y_count(const ExYNode *n);                     /* items of a list, pairs of a mapping */
const char *ex_y_key(const ExYNode *map, size_t i);      /* the text of key i, "" when the key is not a scalar */
ExYNode *ex_y_val(const ExYNode *map, size_t i);         /* value i of a mapping */
ExYNode *ex_y_item(const ExYNode *seq, size_t i);
const char *ex_y_str(const ExYNode *n);                  /* a scalar's text, else NULL */
int ex_y_is_null(const ExYNode *n);                      /* NULL, or an empty plain scalar, or null, Null, NULL, ~ */

#ifdef __cplusplus
}
#endif
#endif
