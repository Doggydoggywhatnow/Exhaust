/* Native file watching: see include/exhaust_watch_native.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_watch_native.h"

/* ---- the queue of changed paths ---- */

#define BUCKETS 4096

typedef struct QNode { char *s; struct QNode *next; } QNode;           /* in the hash table of what waits */

struct ExPathQueue {
    SDL_Mutex *mu;
    char **items; size_t head, n, cap;                                 /* what waits, in the order it came */
    QNode *table[BUCKETS];
    int overflow;
};

static unsigned hash_of(const char *s) { unsigned h = 2166136261u; while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; } return h % BUCKETS; }

ExPathQueue *ex_pathq_create(void) {
    ExPathQueue *q = calloc(1, sizeof *q);
    if (!q) return NULL;
    q->mu = SDL_CreateMutex();
    if (!q->mu) { free(q); return NULL; }
    return q;
}

static void table_remove(ExPathQueue *q, const char *s) {
    QNode **pp = &q->table[hash_of(s)];
    while (*pp) {
        if (!strcmp((*pp)->s, s)) { QNode *d = *pp; *pp = d->next; free(d); return; }
        pp = &(*pp)->next;
    }
}

void ex_pathq_free(ExPathQueue *q) {
    size_t i;
    if (!q) return;
    for (i = 0; i < q->n; i++) free(q->items[(q->head + i) % q->cap]);
    free(q->items);
    for (i = 0; i < BUCKETS; i++) { QNode *d = q->table[i]; while (d) { QNode *nx = d->next; free(d); d = nx; } }
    SDL_DestroyMutex(q->mu);
    free(q);
}

void ex_pathq_push(ExPathQueue *q, const char *rel) {
    unsigned b = hash_of(rel);
    QNode *nd;
    char *copy;
    SDL_LockMutex(q->mu);
    for (nd = q->table[b]; nd; nd = nd->next) if (!strcmp(nd->s, rel)) { SDL_UnlockMutex(q->mu); return; }
    copy = malloc(strlen(rel) + 1);
    nd = malloc(sizeof *nd);
    if (!copy || !nd) { free(copy); free(nd); SDL_UnlockMutex(q->mu); return; }
    strcpy(copy, rel);
    if (q->n == q->cap) {                                              /* grow the ring, keeping the order */
        size_t nc = q->cap ? q->cap * 2 : 64, i;
        char **ni = malloc(nc * sizeof *ni);
        if (!ni) { free(copy); free(nd); SDL_UnlockMutex(q->mu); return; }
        for (i = 0; i < q->n; i++) ni[i] = q->items[(q->head + i) % q->cap];
        free(q->items);
        q->items = ni; q->cap = nc; q->head = 0;
    }
    q->items[(q->head + q->n) % q->cap] = copy;
    q->n++;
    nd->s = copy; nd->next = q->table[b]; q->table[b] = nd;           /* the node shares the string of the item */
    SDL_UnlockMutex(q->mu);
}

void ex_pathq_overflow(ExPathQueue *q) { SDL_LockMutex(q->mu); q->overflow = 1; SDL_UnlockMutex(q->mu); }

int ex_pathq_pop(ExPathQueue *q, char *rel, size_t cap) {
    int got = 0;
    SDL_LockMutex(q->mu);
    if (q->overflow) { q->overflow = 0; if (cap) rel[0] = 0; got = 1; }
    else if (q->n) {
        char *s = q->items[q->head];
        q->head = (q->head + 1) % q->cap;
        q->n--;
        table_remove(q, s);                                            /* before the string is freed: the node points at it */
        snprintf(rel, cap, "%s", s);
        free(s);
        got = 1;
    }
    SDL_UnlockMutex(q->mu);
    return got;
}

size_t ex_pathq_count(ExPathQueue *q) { size_t n; SDL_LockMutex(q->mu); n = q->n + (size_t)q->overflow; SDL_UnlockMutex(q->mu); return n; }

/* ---- a name that starts with a dot is never reported ---- */

#if defined(_WIN32) || defined(__APPLE__)
static int hidden_path(const char *rel) {
    const char *p;
    for (p = rel; *p; p++) if ((p == rel || p[-1] == '/') && *p == '.') return 1;
    return 0;
}
#endif

#if defined(_WIN32)
/* ---- Windows: ReadDirectoryChangesW, with overlapped I/O, on a thread of its own ---- */

#include <windows.h>
#include <stdio.h>

struct ExNativeWatch {
    HANDLE dir, thread, stop_ev, io_ev, ready_ev;
    int ok;                                   /* the first read was accepted */
    ExPathQueue *q;
    DWORD buf[16384];                         /* 64 KB, aligned for FILE_NOTIFY_INFORMATION */
};

static void push_name(ExNativeWatch *w, const WCHAR *name, DWORD bytes) {
    char utf8[2048];
    int n = WideCharToMultiByte(CP_UTF8, 0, name, (int)(bytes / sizeof(WCHAR)), utf8, (int)sizeof utf8 - 1, NULL, NULL), i;
    if (n <= 0) return;
    utf8[n] = 0;
    for (i = 0; i < n; i++) if (utf8[i] == '\\') utf8[i] = '/';
    if (!hidden_path(utf8)) ex_pathq_push(w->q, utf8);
}

static DWORD WINAPI watch_thread(LPVOID arg) {
    ExNativeWatch *w = arg;
    int first = 1;
    for (;;) {
        OVERLAPPED ov;
        DWORD got = 0, wait;
        HANDLE h[2];
        memset(&ov, 0, sizeof ov);
        ResetEvent(w->io_ev);
        ov.hEvent = w->io_ev;
        if (!ReadDirectoryChangesW(w->dir, w->buf, sizeof w->buf, TRUE,
                                   FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
                                   FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION, NULL, &ov, NULL)) {
            if (first) { w->ok = 0; SetEvent(w->ready_ev); }
            else ex_pathq_overflow(w->q);                                 /* it stopped working: everything may have changed */
            break;
        }
        if (first) { w->ok = 1; first = 0; SetEvent(w->ready_ev); }
        h[0] = w->io_ev; h[1] = w->stop_ev;
        wait = WaitForMultipleObjects(2, h, FALSE, INFINITE);
        if (wait != WAIT_OBJECT_0) { CancelIo(w->dir); GetOverlappedResult(w->dir, &ov, &got, TRUE); break; }
        if (!GetOverlappedResult(w->dir, &ov, &got, FALSE) || got == 0) { ex_pathq_overflow(w->q); continue; }   /* the buffer overflowed: events were lost */
        {
            BYTE *p = (BYTE *)w->buf;
            for (;;) {
                FILE_NOTIFY_INFORMATION *fi = (FILE_NOTIFY_INFORMATION *)p;
                push_name(w, fi->FileName, fi->FileNameLength);           /* added, removed, changed, and both names of a rename */
                if (fi->NextEntryOffset == 0) break;
                p += fi->NextEntryOffset;
            }
        }
    }
    return 0;
}

ExNativeWatch *ex_native_watch_start(const char *root) {
    ExNativeWatch *w;
    WCHAR wroot[2048];
    if (MultiByteToWideChar(CP_UTF8, 0, root, -1, wroot, 2048) <= 0) return NULL;
    {   /* a network drive: its change notices are not reliable, so it is polled */
        WCHAR drive[4];
        if (wroot[0] && wroot[1] == L':') { drive[0] = wroot[0]; drive[1] = L':'; drive[2] = L'\\'; drive[3] = 0; if (GetDriveTypeW(drive) == DRIVE_REMOTE) return NULL; }
        else if (wroot[0] == L'\\' && wroot[1] == L'\\') return NULL;      /* a UNC path */
    }
    w = calloc(1, sizeof *w);
    if (!w) return NULL;
    w->q = ex_pathq_create();
    w->stop_ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    w->io_ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    w->ready_ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    w->dir = CreateFileW(wroot, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
    if (!w->q || !w->stop_ev || !w->io_ev || !w->ready_ev || w->dir == INVALID_HANDLE_VALUE) { ex_native_watch_stop(w); return NULL; }
    w->thread = CreateThread(NULL, 0, watch_thread, w, 0, NULL);
    if (!w->thread) { ex_native_watch_stop(w); return NULL; }
    WaitForSingleObject(w->ready_ev, 5000);
    if (!w->ok) { ex_native_watch_stop(w); return NULL; }                  /* the system refused: poll instead */
    return w;
}

int ex_native_watch_read(ExNativeWatch *w, char *rel, size_t cap) { return ex_pathq_pop(w->q, rel, cap); }

void ex_native_watch_stop(ExNativeWatch *w) {
    if (!w) return;
    if (w->thread) { SetEvent(w->stop_ev); WaitForSingleObject(w->thread, 5000); CloseHandle(w->thread); }
    if (w->dir && w->dir != INVALID_HANDLE_VALUE) CloseHandle(w->dir);
    if (w->stop_ev) CloseHandle(w->stop_ev);
    if (w->io_ev) CloseHandle(w->io_ev);
    if (w->ready_ev) CloseHandle(w->ready_ev);
    ex_pathq_free(w->q);
    free(w);
}

#elif defined(__APPLE__)
/* ---- macOS: FSEvents, delivered on a dispatch queue of its own ---- */

#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <limits.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/param.h>

struct ExNativeWatch {
    FSEventStreamRef stream;
    dispatch_queue_t queue;
    char root[PATH_MAX];                      /* the real path: FSEvents reports real paths (/private/var, not /var) */
    size_t rootlen;
    ExPathQueue *q;
};

static void fse_callback(ConstFSEventStreamRef s, void *info, size_t n, void *paths, const FSEventStreamEventFlags flags[], const FSEventStreamEventId ids[]) {
    ExNativeWatch *w = info;
    char **p = paths;
    size_t i;
    (void)s; (void)ids;
    for (i = 0; i < n; i++) {
        const char *rel;
        if (flags[i] & (kFSEventStreamEventFlagMustScanSubDirs | kFSEventStreamEventFlagUserDropped | kFSEventStreamEventFlagKernelDropped | kFSEventStreamEventFlagRootChanged)) { ex_pathq_overflow(w->q); continue; }
        if (strncmp(p[i], w->root, w->rootlen) != 0) continue;
        rel = p[i] + w->rootlen;
        if (*rel == '/') rel++;
        if (!*rel || hidden_path(rel)) continue;
        ex_pathq_push(w->q, rel);                                          /* created, removed, renamed (both names) or changed */
    }
}

ExNativeWatch *ex_native_watch_start(const char *root) {
    ExNativeWatch *w;
    struct statfs sf;
    CFStringRef cfroot;
    CFArrayRef arr;
    FSEventStreamContext ctx;
    if (statfs(root, &sf) == 0 && (!strcmp(sf.f_fstypename, "smbfs") || !strcmp(sf.f_fstypename, "nfs") || !strcmp(sf.f_fstypename, "afpfs") || !strcmp(sf.f_fstypename, "webdav")))
        return NULL;                                                        /* a network volume: its change notices are not reliable, so it is polled */
    w = calloc(1, sizeof *w);
    if (!w) return NULL;
    if (!realpath(root, w->root)) { free(w); return NULL; }
    w->rootlen = strlen(w->root);
    w->q = ex_pathq_create();
    if (!w->q) { free(w); return NULL; }
    cfroot = CFStringCreateWithCString(NULL, w->root, kCFStringEncodingUTF8);
    arr = cfroot ? CFArrayCreate(NULL, (const void **)&cfroot, 1, &kCFTypeArrayCallBacks) : NULL;
    memset(&ctx, 0, sizeof ctx);
    ctx.info = w;
    if (arr) w->stream = FSEventStreamCreate(NULL, fse_callback, &ctx, arr, kFSEventStreamEventIdSinceNow, 0.05, kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer);
    if (arr) CFRelease(arr);
    if (cfroot) CFRelease(cfroot);
    if (!w->stream) { ex_native_watch_stop(w); return NULL; }
    w->queue = dispatch_queue_create("exhaust.fsevents", DISPATCH_QUEUE_SERIAL);
    FSEventStreamSetDispatchQueue(w->stream, w->queue);
    if (!FSEventStreamStart(w->stream)) { ex_native_watch_stop(w); return NULL; }
    return w;
}

int ex_native_watch_read(ExNativeWatch *w, char *rel, size_t cap) { return ex_pathq_pop(w->q, rel, cap); }

void ex_native_watch_stop(ExNativeWatch *w) {
    if (!w) return;
    if (w->stream) {
        FSEventStreamStop(w->stream);
        FSEventStreamInvalidate(w->stream);
        FSEventStreamRelease(w->stream);
    }
    if (w->queue) dispatch_release(w->queue);
    ex_pathq_free(w->q);
    free(w);
}

#else
/* Linux has inotify in src/notes/watcher.c; nothing native here. */
ExNativeWatch *ex_native_watch_start(const char *root) { (void)root; return NULL; }
int ex_native_watch_read(ExNativeWatch *w, char *rel, size_t cap) { (void)w; (void)rel; (void)cap; return 0; }
void ex_native_watch_stop(ExNativeWatch *w) { (void)w; }
#endif
