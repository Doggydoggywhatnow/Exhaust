/* The export to PDF as a job on a thread of its own (sub-slice 6e-iv): the interface starts it with the note's text, the options and the pictures it
 * resolved, and receives messages in its queue: progress, then one that says how it ended. The thread writes the file (atomically: the old file is
 * kept if anything fails) and touches nothing of the interface. */
#ifndef EXHAUST_EXPORTJOB_H
#define EXHAUST_EXPORTJOB_H

#include <stddef.h>
#include "exhaust_pdfexport.h"
#include "exhaust_worker.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { EX_MSG_EXPORT = 0x300 };

typedef struct ExExportMsg {
    int done;                    /* 0: progress, 1: the end */
    int permille, page;          /* progress */
    int ok, cancelled, pages;    /* the end */
    char message[300];           /* the end: what to tell the user */
} ExExportMsg;

typedef struct ExExportJob ExExportJob;

typedef struct ExExportStart {
    const char *title;
    const char *text; size_t len;
    ExPdfExportOptions opt;
    const char *out_path;                      /* the file to write (the caller has asked about replacing it) */
    const char *const *names, *const *paths;   /* embeds resolved by the caller: name as written, full path */
    size_t nres;
    ExQueue *results;                          /* receives ExExportMsg payloads (type EX_MSG_EXPORT) */
} ExExportStart;

/* Copies everything it needs. NULL if the thread cannot be started. */
ExExportJob *ex_exportjob_start(const ExExportStart *s);
void ex_exportjob_cancel(ExExportJob *j);
/* Waits for the thread (it has sent its last message or was cancelled) and frees the job. */
void ex_exportjob_free(ExExportJob *j);

/* The names of the pictures and other files a note embeds (`![[name|size]]` and `![alt](name)`), without an address on the web: malloc'ed strings, at most
 * `max`. */
size_t ex_exportjob_embed_names(const char *text, size_t len, char ***names, size_t max);
void ex_exportjob_names_free(char **names, size_t n);

#ifdef __cplusplus
}
#endif
#endif
