/* The panel service: one worker thread with its own reader connection to the index. The panels send it requests and get rows
 * back, ready to put in a list, so the interface thread never waits for the index or for a note's file. */
#ifndef EXHAUST_SERVICE_H
#define EXHAUST_SERVICE_H
#include <stddef.h>
#include <stdint.h>
#include "exhaust_index.h"
#include "exhaust_ui.h"
#include "exhaust_links.h"
#include "exhaust_nav.h"
#include "exhaust_props.h"
#include "exhaust_query.h"
#include "exhaust_rename.h"
#include "exhaust_search.h"
#include "exhaust_worker.h"

typedef struct QRow { char *text, *detail, *action, *key; int indent, glyph, muted, bold; int64_t id; size_t aux1, aux2; } QRow;
typedef struct QRows { QRow *v; size_t n, cap; } QRows;
void qrows_free(QRows *r);
size_t qrows_add(QRows *r, const char *text, const char *detail, const char *key, int indent, int glyph, int muted, int bold, int64_t id, size_t aux1, size_t aux2);

enum { QJ_SEARCH = 1, QJ_BACKLINKS, QJ_UNLINKED, QJ_OUTGOING, QJ_TAGS, QJ_PROPS, QJ_OUTLINE, QJ_FOOTNOTES, QJ_PREVIEW, QJ_SNAPSHOT, QJ_REWRITE, QJ_PLAN, QJ_PROPOP, QJ_LINKIT, QJ_EXPAND };
enum { EX_MSG_SERVICE = 0x200 };

typedef struct QJob {
    int kind;
    unsigned long req;
    char *a, *b, *c;               /* a path, a query, a name */
    char *text;                    /* a snapshot of a note's text */
    size_t len;
    int i1, i2;
    ExSearchOptions so;
    ExResultView view;
    ExMove *moves; size_t nmoves;  /* the strings are owned */
    int include_attachments;
} QJob;

typedef struct QRes {
    int kind;
    unsigned long req;
    int ok;
    char summary[220];             /* a line for the panel's header, or the reason it failed */
    QRows rows;
    QRows rows2;                   /* unlinked mentions, unresolved links */
    char *text;                    /* a preview */
    ExSwitcher *snapshot;
    ExRenameReport report;
    size_t changed;
    size_t total;
} QRes;
void qres_free(QRes *r);

typedef struct QService QService;
QService *qservice_start(const char *index_path, const char *vault_root, ExQueue *results, uint32_t worker_id);
void qservice_post(QService *s, QJob *job);          /* takes the job */
void qservice_stop(QService *s);
QJob *qjob_new(int kind, unsigned long req);
#endif
