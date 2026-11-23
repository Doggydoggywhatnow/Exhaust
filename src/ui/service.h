/* The panel service: one worker thread with its own reader connection to the index. The panels send it requests and get rows
 * back, ready to put in a list, so the interface thread never waits for the index or for a note's file. */
#ifndef EXHAUST_SERVICE_H
#define EXHAUST_SERVICE_H
#include <stddef.h>
#include <stdint.h>
#include "exhaust_graph.h"
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

enum { QJ_SEARCH = 1, QJ_BACKLINKS, QJ_UNLINKED, QJ_OUTGOING, QJ_TAGS, QJ_PROPS, QJ_OUTLINE, QJ_FOOTNOTES, QJ_PREVIEW, QJ_SNAPSHOT, QJ_REWRITE, QJ_PLAN, QJ_PROPOP, QJ_LINKIT, QJ_EXPAND, QJ_FILEPROPS, QJ_GRAPH, QJ_HISTORY, QJ_FILETEXT, QJ_BASE, QJ_IMAGE, QJ_AUDIO, QJ_MPV, QJ_PDF };
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
    void *ptr;                     /* QJ_AUDIO: the player whose output device the worker opens (the UI does not touch it until the answer comes) */
    ExGraphOptions gopts;          /* QJ_GRAPH: the filters and groups to apply (5b) */
    /* QJ_HISTORY (7e): b is the folder of the snapshots; i1 0 lists those of the note a (rows, newest first: text the label, key the file, id the index,
     * aux1 the time, aux2 the size, glyph the kind), 1 reads the snapshot named a (rows2[0].text its text, res->text the changes to the note's text in `text`),
     * 2 deletes every automatic snapshot (changed is how many). `text` is the note's text as it is now. */
    /* QJ_PDF (6c-iv): a is the path in the vault; the answer's rows[0].text is `a` again. */
    /* QJ_MPV (6c-iii): a is the path in the vault, i1 is 1 for a video (a picture) and 0 for audio only; the answer's rows[0].text is `a` again. */
    /* QJ_AUDIO (6c-ii): a is the path in the vault, ptr the ExPlayer; the answer's rows[0].text is `a` again. */
    /* QJ_IMAGE (6c-i): a is the path in the vault, i1 the width an SVG is drawn at (0: its own size); the answer's rows[0].text is `a` again and `total` is i1. */
    /* QJ_BASE (6b-iii): `text` is the base file as the view's document holds it, i1 the view, a the path of the file (what `this` is). */
    /* QJ_FILETEXT (6a): the start of a note for a canvas card. a is `path`, `path#Heading` or `path#^block`; the answer's rows[0].text is `a` again and text the
     * excerpt (about 1500 bytes cut at a line), or ok = 0 when the file is not a note the index knows. */
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
    struct ExPdf *pdf;             /* QJ_PDF (6c-iv): the opened document (the receiver takes it and sets this to NULL); summary is the error when pdf is NULL */
    struct ExMpv *mpv;             /* QJ_MPV (6c-iii): the opened libmpv player (the receiver takes it and sets this to NULL); summary is the error when mpv is NULL */
    struct ExAudioDecoder *audio;  /* QJ_AUDIO (6c-ii): the opened decoder (the receiver takes it and sets this to NULL); summary is the error when audio is NULL */
    struct ExImage *image;         /* QJ_IMAGE (6c-i): the decoded picture (the receiver takes it and sets this to NULL); summary is the error when image is NULL */
    struct ExBaseResult *base;     /* QJ_BASE (6b-iii): the view run over the vault (the receiver takes it and sets this to NULL); summary is the error when base is NULL */
    ExGraph *graph;                /* QJ_GRAPH: the whole graph (the receiver takes it and sets this to NULL) */
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
