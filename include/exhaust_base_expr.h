/* The Bases expression engine (sub-slice 6b-i): the language of filters, formulas and custom summaries. It is a module of its own: it reads no file, knows no
 * vault and opens no window; what it needs from the program (a note's properties, a file's fields, the links between files, the clock) comes through an
 * ExExprHost. The language and its functions are in docs/formats.md (*Bases expressions*); the function list follows the Bases help of Obsidian (68 functions)
 * and adds the four list statistics its own summary example uses (`sum`, `mean`, `median`, `stddev`): 72.
 *
 * Values live in an arena (ExArena): everything an evaluation makes is freed together, so a view makes one arena per row (or per frame), evaluates what it
 * shows, copies what it keeps and resets it. Nothing here blocks or allocates without bound: nesting, formula depth and the size of lists and strings
 * made by repeat() are limited, and an error is a value (EXV_ERROR) that shows in a cell, never a crash. */
#ifndef EXHAUST_BASE_EXPR_H
#define EXHAUST_BASE_EXPR_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExArena ExArena;
ExArena *ex_arena_new(void);
void ex_arena_free(ExArena *a);
void ex_arena_reset(ExArena *a);                       /* frees everything allocated from it, keeps the arena */
void *ex_arena_alloc(ExArena *a, size_t n);            /* zeroed; NULL when out of memory */
char *ex_arena_strndup(ExArena *a, const char *s, size_t n);
char *ex_arena_strdup(ExArena *a, const char *s);

typedef enum ExVType {
    EXV_NULL, EXV_BOOL, EXV_NUM, EXV_STR, EXV_DATE, EXV_DURATION, EXV_LIST, EXV_OBJECT, EXV_LINK, EXV_URL, EXV_FILE, EXV_REGEXP,
    EXV_HTML, EXV_IMAGE, EXV_ICON, EXV_ERROR,
    EXV_NS                                             /* internal: `note` and `formula` before their member is named; never a result */
} ExVType;

typedef struct ExValue ExValue;
struct ExValue {
    ExVType t;
    union {
        double num;                                     /* EXV_NUM; EXV_BOOL is 0 or 1 */
        const char *str;                                /* EXV_STR, EXV_HTML, EXV_ICON, EXV_ERROR (the message) */
        struct { double ms; int has_time; } date;       /* milliseconds since 1970 UTC; has_time: made with a time of day */
        struct { double ms; int64_t months; } dur;      /* a length of time: exact milliseconds and calendar months (years are 12 months) */
        struct { const ExValue *v; size_t n; } list;
        struct { const char *const *keys; const ExValue *v; size_t n; } obj;
        struct { const char *path, *sub, *display; } link;   /* the text of the link target without [[ ]], a #heading or #^block (or NULL), the text shown (or NULL) */
        struct { const char *url, *display; } url;
        const char *file;                               /* EXV_FILE: the path in the vault */
        struct { const char *pattern, *flags; } re;
        const char *image;                              /* EXV_IMAGE: a path or an address */
        int ns;                                         /* EXV_NS: 0 note, 1 formula */
    } u;
};

/* Constructors. Strings are copied into the arena. */
ExValue ex_v_null(void);
ExValue ex_v_bool(int b);
ExValue ex_v_num(double d);
ExValue ex_v_str(ExArena *a, const char *s);
ExValue ex_v_strn(ExArena *a, const char *s, size_t n);
ExValue ex_v_date(double ms, int has_time);
ExValue ex_v_duration(double ms, int64_t months);
ExValue ex_v_list(ExArena *a, const ExValue *items, size_t n);        /* copies the array */
ExValue ex_v_object(ExArena *a, const char *const *keys, const ExValue *vals, size_t n);   /* copies keys and values */
ExValue ex_v_link(ExArena *a, const char *path, const char *sub, const char *display);
ExValue ex_v_url(ExArena *a, const char *url, const char *display);
ExValue ex_v_file(ExArena *a, const char *path);
ExValue ex_v_error(ExArena *a, const char *fmt, ...);

/* What an evaluation asks of the program. Every callback may be NULL (the name is then not found). */
typedef struct ExExpr ExExpr;
typedef struct ExExprHost {
    void *user;
    /* a bare name or `note.<name>` of the note of the row: 1 and *out when it has such a property */
    int (*note_prop)(void *user, ExArena *a, const char *name, ExValue *out);
    /* a field of a file: name basename path folder ext size properties tags links embeds backlinks ctime mtime (for `file.<field>`, `this.file.<field>` and the files
     * links lead to); 1 and *out, or 0 when the file has no such field. `properties` is an object, `tags` a list of strings with their #, `links` a list of links. */
    int (*file_field)(void *user, ExArena *a, const char *path, const char *field, ExValue *out);
    /* the file a link names, as a link from the file `from` ("" for none) would find it: 1 and its path, or 0 */
    int (*resolve)(void *user, const char *from, const char *text, char *path, size_t cap);
    /* the parsed expression of a formula property of the base (`formula.<name>`), or NULL */
    const ExExpr *(*formula)(void *user, const char *name);
    const char *file_path;                              /* the row's file, for `file` */
    const char *this_path;                              /* the file `this` stands for, or NULL */
    double now_ms;                                      /* the clock: now() and today() */
    int tz_min;                                         /* minutes east of UTC, for dates shown and for today() */
    double (*random)(void *user);                       /* random(): a number from 0 to 1 (NULL: the C library's) */
} ExExprHost;

/* Parsing. On failure returns NULL, writes the reason to err and the byte it is about to *err_pos (if given). */
ExExpr *ex_expr_parse(const char *text, char *err, size_t errcap, int *err_pos);
void ex_expr_free(ExExpr *e);
/* Evaluates. `values` is the list a custom summary sees as `values` (NULL: the name is a property like any other). An error is returned as EXV_ERROR. */
ExValue ex_expr_eval(const ExExpr *e, ExArena *a, const ExExprHost *host, const ExValue *values);
/* Parses and evaluates in one call (for tests and one-off filters). A parse error is an EXV_ERROR. */
ExValue ex_expr_run(const char *text, ExArena *a, const ExExprHost *host, const ExValue *values);

/* The functions: `global:if`, `string:contains`, `list:map`... In the order of the documentation. */
size_t ex_expr_function_count(void);
const char *ex_expr_function_name(size_t i);

/* Values. */
int ex_value_truthy(ExValue v);                          /* false, 0, NaN, "", null, empty list and empty object are false */
int ex_value_is_empty(ExValue v);                        /* null, "", an empty list or object, NaN */
int ex_value_equal(const ExExprHost *host, ExValue a, ExValue b);   /* ==: same type and content; a link equals a link or a file that resolves to the same file */
/* The order sort() and the views use: null, booleans, numbers, strings, dates, durations, then the rest by type; returns -1, 0 or 1. */
int ex_value_compare(ExValue a, ExValue b);
/* The text shown in a cell and made by toString(): numbers as JavaScript writes them, dates as YYYY-MM-DD or with the time, lists joined with ", ", null as "". */
char *ex_value_to_string(ExArena *a, const ExExprHost *host, ExValue v);
/* A deterministic text for tests: strings quoted, lists and objects as JSON, `date("...")`, `link("...")`, `#ERR(...)`. */
char *ex_value_repr(ExArena *a, ExValue v);
const char *ex_value_type_name(ExValue v);               /* "null" "boolean" "number" "string" "date" "duration" "list" "object" "link" "url" "file" "regexp" "html" "image" "icon" "error" */
/* A number as JavaScript writes it (1, 0.5, 1e+21, 1e-7, NaN, Infinity). Returns the length. */
size_t ex_number_to_string(double d, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
