/* Importers (sub-slice 6e-v): Markdown and plain text folders, HTML files, CSV files and Textbundle bundles become notes in the vault. The importers read
 * the source, never change it, and never replace a note of the vault: a name that is taken gets a number. They run on a thread of their own (the
 * interface starts one job and shows its report). See docs/formats.md (Import). No UI here. */
#ifndef EXHAUST_IMPORT_H
#define EXHAUST_IMPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExImportFormat { EX_IMPORT_MARKDOWN = 0, EX_IMPORT_TEXT, EX_IMPORT_HTML, EX_IMPORT_CSV, EX_IMPORT_TEXTBUNDLE, EX_IMPORT_FORMAT_COUNT } ExImportFormat;

const char *ex_import_format_name(ExImportFormat f);        /* "Markdown", "Plain text", "HTML", "CSV", "Textbundle" */
const char *ex_import_format_help(ExImportFormat f);        /* one line: what the source is */
int ex_import_wants_file(ExImportFormat f);                 /* 1 when the source is one file (CSV), 0 when it is a folder (or, for HTML, either) */
/* The other applications' importers: listed, disabled, labelled (later). */
size_t ex_import_later_count(void);
const char *ex_import_later_name(size_t i);

typedef struct ExImportOptions {
    const char *folder;          /* the folder of the vault the notes go into ("" the top; default "Imported") */
    const char *note_name;       /* the pattern of a note's name; {{title}}, {{filename}}, {{date}}, {{time}} and, for CSV, {{Column name}} (default {{title}}) */
    int keep_folders;            /* keep the source's folders below `folder` */
    const char *template_text;   /* the importer template: a note's text with {{content}}; NULL for none */
    size_t template_len;
    const char *date_format, *time_format;
    double now_ms; int tz_min;   /* the moment {{date}} and {{time}} show */
} ExImportOptions;

typedef struct ExImportReport {
    size_t notes, files;         /* notes written, other files copied */
    size_t renamed;              /* names that were taken: they got a number */
    size_t skipped;              /* sources that were left out (too big, not readable, empty names) */
    int cancelled;
    char first_problem[240];     /* the first thing that went wrong, or empty */
} ExImportReport;

/* Imports `source` (a folder, or for CSV a file; a Textbundle may be a folder or a .textpack archive) into the vault at `vault_root`. 0 when something was
 * imported or there was nothing wrong, -1 when the source cannot be read at all (see first_problem). `cancel` may be NULL. */
int ex_import_run(ExImportFormat f, const char *source, const char *vault_root, const ExImportOptions *o, ExImportReport *r, const volatile int *cancel);

/* ---- the parts, tested and fuzzed on their own ---- */

/* HTML to Markdown: headings, paragraphs, line breaks, bold, italic, code, links, images (src as written), lists (nested), quotes, preformatted text, tables,
 * rules, strike-through and highlights; scripts, styles, comments and unknown tags are dropped, entities are read. *title is the document's title
 * (<title>, else the first <h1>) or NULL (malloc'ed). The output is malloc'ed and NUL-terminated. Never reads outside the text; the depth of nesting is
 * limited. */
char *ex_html_to_markdown(const char *html, size_t len, char **title, size_t *out_len);

/* CSV: fields separated by `,` (or `;` or a tab when the first line has more of those), quotes with doubled quotes, line breaks inside quotes, a byte-order
 * mark, CRLF or LF. The callback gets each row's fields (valid during the call); return nonzero to stop. At most 1000 fields a row, 4 MB a field. Returns the
 * number of rows read. */
typedef int (*ExCsvRowFn)(const char *const *fields, const size_t *lens, size_t n, size_t row, void *user);
size_t ex_csv_parse(const char *text, size_t len, ExCsvRowFn fn, void *user);

/* ZIP (the .textpack): lists the entries and extracts one. Only stored and deflated entries; names with `..`, a leading `/` or a backslash are refused; an
 * entry may be at most 64 MB. */
typedef struct ExZip ExZip;
ExZip *ex_zip_open(const unsigned char *data, size_t len);      /* the data must outlive it */
void ex_zip_close(ExZip *z);
size_t ex_zip_count(const ExZip *z);
const char *ex_zip_name(const ExZip *z, size_t i);
int ex_zip_is_dir(const ExZip *z, size_t i);
unsigned char *ex_zip_extract(const ExZip *z, size_t i, size_t *len);   /* malloc'ed, NUL-terminated; NULL on any error */

#ifdef __cplusplus
}
#endif
#endif
