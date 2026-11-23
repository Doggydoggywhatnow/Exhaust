/* The importers as a job on a thread of their own (sub-slice 6e-v): the interface starts it with the format, the source and the options, and receives
 * messages in its queue: progress is not reported (an import is a walk over files), the end message says what was imported. See exhaust_exportjob.h. */
#ifndef EXHAUST_IMPORTJOB_H
#define EXHAUST_IMPORTJOB_H

#include <stddef.h>
#include "exhaust_import.h"
#include "exhaust_worker.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { EX_MSG_IMPORT = 0x301 };

typedef struct ExImportMsg {
    int ok, cancelled;
    ExImportReport report;
    char message[400];            /* what to tell the user */
} ExImportMsg;

typedef struct ExImportJob ExImportJob;

typedef struct ExImportStart {
    ExImportFormat format;
    const char *source, *vault_root;
    ExImportOptions opt;                 /* the strings are copied */
    ExQueue *results;                    /* receives one ExImportMsg (type EX_MSG_IMPORT) */
} ExImportStart;

ExImportJob *ex_importjob_start(const ExImportStart *s);
void ex_importjob_cancel(ExImportJob *j);
void ex_importjob_free(ExImportJob *j);    /* joins the thread */

#ifdef __cplusplus
}
#endif
#endif
