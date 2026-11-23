/* Bases (sub-slice 6b-ii): the `.base` file and what a view of it shows.
 *
 * A base is YAML (include/exhaust_yaml.h keeps the whole document, so keys Exhaust does not know are written back): global `filters`, `formulas`,
 * `properties` (display names), custom `summaries` and a list of `views`, each with a type, a name, `filters`, `sort`, `groupBy`, `groupOrder`, `limit`,
 * `order` (the columns) and `summaries`. The syntax is in docs/formats.md (*Bases files*) and follows the Bases help of Obsidian.
 *
 * Running a view is a pure function of the base, the files it is given (ExBaseData: what the index knows about every file) and `this`; it reads no disk. The
 * application builds the data and runs it on the service thread, and the interface thread draws the finished ExBaseResult. */
#ifndef EXHAUST_BASE_H
#define EXHAUST_BASE_H
#include <stddef.h>
#include "exhaust_base_expr.h"
#include "exhaust_yaml.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the file ---- */

typedef struct ExBase ExBase;
ExBase *ex_base_new(void);                                    /* one table view called Table */
/* From the text of a `.base` file or of a `base` code block. NULL with the reason in err when it is not valid YAML or not a mapping. */
ExBase *ex_base_parse(const char *text, size_t len, char *err, size_t errcap);
char *ex_base_write(const ExBase *b, size_t *len);            /* malloc'ed, NUL terminated */
void ex_base_free(ExBase *b);
ExYNode *ex_base_root(ExBase *b);                             /* the document: the application edits it (a view's name, its columns...) and writes it back */
const ExYNode *ex_base_root_const(const ExBase *b);
size_t ex_base_view_count(const ExBase *b);
const ExYNode *ex_base_view(const ExBase *b, size_t i);       /* the view's mapping */
const char *ex_base_view_name(const ExBase *b, size_t i);     /* "" when it has none */
const char *ex_base_view_type(const ExBase *b, size_t i);     /* "table" when it has none */
int ex_base_find_view(const ExBase *b, const char *name);     /* the first view of that name (without regard to case), or -1 */
/* The text a view is shown with: its display name, or a default for the property id (`file.name` is "File name"). Points into a static table or the base. */
const char *ex_base_display_name(const ExBase *b, const char *property_id, char *buf, size_t cap);

/* ---- the files a view is run over ---- */

typedef enum ExBasePropType { EXB_TEXT = 0, EXB_LIST, EXB_NUMBER, EXB_CHECKBOX, EXB_DATE, EXB_DATETIME, EXB_LINKS } ExBasePropType;   /* the index's property types */
typedef struct ExBaseProp { char *name; int type; char *value; } ExBaseProp;       /* a list's value is its items joined with \n */
typedef struct ExBaseFile {
    char *path;
    int kind;                                                 /* ex_file_kind() */
    double ctime_ms, mtime_ms, size;
    ExBaseProp *props; size_t nprops;                         /* the frontmatter of a note */
    char **tags; size_t ntags;                                /* with their # */
    char **links; size_t nlinks;                              /* targets as written, without [[ ]], # parts kept */
    char **embeds; size_t nembeds;
} ExBaseFile;
typedef struct ExBaseData {
    ExBaseFile *files; size_t n, cap;
} ExBaseData;
void ex_base_data_free(ExBaseData *d);
ExBaseFile *ex_base_data_add(ExBaseData *d, const char *path, int kind, double ctime_ms, double mtime_ms, double size);
int ex_base_file_prop(ExBaseFile *f, const char *name, int type, const char *value);
int ex_base_file_tag(ExBaseFile *f, const char *tag);
int ex_base_file_link(ExBaseFile *f, const char *target);
int ex_base_file_embed(ExBaseFile *f, const char *target);

/* ---- a view's result ---- */

typedef struct ExBaseColumn {
    const char *id;                                           /* `file.name`, `note.age`, `formula.ppu` or a bare property name */
    const char *name;                                         /* the heading */
    const char *summary_name;                                 /* the summary asked for, or NULL */
    ExValue summary;                                          /* its value (null when there is none) */
} ExBaseColumn;
typedef struct ExBaseGroup { const char *label; ExValue key; size_t first, count; } ExBaseGroup;   /* rows first .. first+count-1 */
typedef struct ExBaseResult {
    ExArena *arena;                                           /* everything below lives here */
    size_t ncols; ExBaseColumn *cols;
    size_t nrows;
    const ExBaseFile **files;                                 /* the file of each row, in the order shown (by group when grouped) */
    ExValue *cells;                                           /* nrows * ncols */
    size_t ngroups; ExBaseGroup *groups;                      /* none when the view does not group */
    size_t total;                                             /* the files that passed the filters, before `limit` */
    const char **errors; size_t nerrors;                      /* what went wrong in a filter, a formula or a summary (distinct messages, at most 16) */
    int tz_min;                                               /* the zone the dates are shown in */
    char **texts;                                             /* the cells as text, made when asked for (ex_base_cell_text) */
} ExBaseResult;

/* Runs view `view` of the base over the files. this_path is the file `this` stands for (NULL: none). now_ms and tz_min are the clock and zone for now(), today()
 * and the dates shown. Returns 0 (errors in a cell or a filter do not fail it), -1 for a bad view number or out of memory. */
int ex_base_run(const ExBase *b, size_t view, const ExBaseData *data, const char *this_path, double now_ms, int tz_min, ExBaseResult *out);
void ex_base_result_free(ExBaseResult *r);
const char *ex_base_cell_text(const ExBaseResult *r, size_t row, size_t col);        /* the cell as shown (in the result's arena) */
/* The view as CSV (RFC 4180: quoted when needed, CRLF line ends) and as a Markdown table (for the clipboard; a group is a line of its own above its rows). malloc'ed. */
char *ex_base_result_csv(const ExBaseResult *r, size_t *len);
char *ex_base_result_markdown(const ExBaseResult *r, size_t *len);

#ifdef __cplusplus
}
#endif
#endif
