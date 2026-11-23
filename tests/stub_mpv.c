/* A stand-in for libmpv (6c-iii tests): the part of the client and render API that Exhaust uses, behaving like a very small player. A file whose name ends
 * in `.bad` fails to play; any other file is 2 seconds long and 64 by 48 pixels; time runs while it is not paused; the software renderer draws a flat colour
 * whose red value is the position in 1/100 s (a frame that is torn between two positions would show two values); at the end an end-of-file event arrives. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
static void nap(void) { Sleep(1); }
#define MUTEX CRITICAL_SECTION
#define MUTEX_INIT(m) InitializeCriticalSection(m)
#define MUTEX_FREE(m) DeleteCriticalSection(m)
#define LOCK(c) EnterCriticalSection(&(c)->mu)
#define UNLOCK(c) LeaveCriticalSection(&(c)->mu)
#else
#include <pthread.h>
static void nap(void) { struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = 1000000L; nanosleep(&ts, NULL); }
#define MUTEX pthread_mutex_t
#define MUTEX_INIT(m) pthread_mutex_init(m, NULL)
#define MUTEX_FREE(m) pthread_mutex_destroy(m)
#define LOCK(c) pthread_mutex_lock(&(c)->mu)
#define UNLOCK(c) pthread_mutex_unlock(&(c)->mu)
#endif

#include "mpv/client.h"
#include "mpv/render.h"

typedef struct Ctx {
    int paused, loaded, failed, ended, eof_sent, sent_loaded, sent_end;
    double pos, volume, duration;
    double t_last;
    char path[400];
    mpv_event ev;
    mpv_event_end_file end;
    MUTEX mu;   /* the player thread, the render thread and the UI thread all call in: the real library is thread safe, so the stand-in must be */
} Ctx;

/* The monotonic clock: timespec_get is not declared on macOS when _POSIX_C_SOURCE is defined (the time stood still there and the end never came: CI run 45163695250). */
static double now_s(void) {
    struct timespec ts;
#ifdef _WIN32
    timespec_get(&ts, TIME_UTC);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void advance(Ctx *c) {
    double t = now_s();
    if (c->loaded && !c->paused && !c->ended) { c->pos += t - c->t_last; if (c->pos >= c->duration) { c->pos = c->duration; c->ended = 1; } }
    c->t_last = t;
}

MPV_EXPORT unsigned long mpv_client_api_version(void) { return MPV_MAKE_VERSION(2, 5); }
MPV_EXPORT const char *mpv_error_string(int e) { return e == MPV_ERROR_LOADING_FAILED ? "loading failed" : e == MPV_ERROR_INVALID_PARAMETER ? "invalid parameter" : "error"; }
MPV_EXPORT mpv_handle *mpv_create(void) { Ctx *c = calloc(1, sizeof *c); if (c) { c->paused = 1; c->volume = 100.0; c->duration = 2.0; c->t_last = now_s(); MUTEX_INIT(&c->mu); } return (mpv_handle *)c; }
MPV_EXPORT int mpv_initialize(mpv_handle *h) { (void)h; return 0; }
MPV_EXPORT void mpv_terminate_destroy(mpv_handle *h) { if (h) MUTEX_FREE(&((Ctx *)h)->mu); free(h); }
MPV_EXPORT int mpv_set_option_string(mpv_handle *h, const char *n, const char *d) { (void)h; (void)n; (void)d; return 0; }
MPV_EXPORT int mpv_set_property_string(mpv_handle *h, const char *n, const char *d) { (void)h; (void)n; (void)d; return 0; }
MPV_EXPORT int mpv_set_property(mpv_handle *h, const char *n, mpv_format f, void *d) {
    Ctx *c = (Ctx *)h;
    int rc = MPV_ERROR_PROPERTY_NOT_FOUND;
    LOCK(c);
    advance(c);
    if (!strcmp(n, "pause") && f == MPV_FORMAT_FLAG) { c->paused = *(int *)d; rc = 0; }
    else if (!strcmp(n, "volume") && f == MPV_FORMAT_DOUBLE) { c->volume = *(double *)d; rc = 0; }
    UNLOCK(c);
    return rc;
}
MPV_EXPORT int mpv_get_property(mpv_handle *h, const char *n, mpv_format f, void *d) {
    Ctx *c = (Ctx *)h;
    int rc = MPV_ERROR_PROPERTY_NOT_FOUND;
    LOCK(c);
    advance(c);
    if (!c->loaded) rc = MPV_ERROR_PROPERTY_UNAVAILABLE;
    else if (!strcmp(n, "time-pos") && f == MPV_FORMAT_DOUBLE) { *(double *)d = c->pos; rc = 0; }
    else if (!strcmp(n, "duration") && f == MPV_FORMAT_DOUBLE) { *(double *)d = c->duration; rc = 0; }
    else if (!strcmp(n, "pause") && f == MPV_FORMAT_FLAG) { *(int *)d = c->paused; rc = 0; }
    else if (!strcmp(n, "width") && f == MPV_FORMAT_INT64) { *(int64_t *)d = 64; rc = 0; }
    else if (!strcmp(n, "height") && f == MPV_FORMAT_INT64) { *(int64_t *)d = 48; rc = 0; }
    else if (!strcmp(n, "volume") && f == MPV_FORMAT_DOUBLE) { *(double *)d = c->volume; rc = 0; }
    UNLOCK(c);
    return rc;
}
MPV_EXPORT int mpv_command(mpv_handle *h, const char **args) {
    Ctx *c = (Ctx *)h;
    int rc = MPV_ERROR_INVALID_PARAMETER;
    LOCK(c);
    advance(c);
    if (!args || !args[0]) rc = MPV_ERROR_INVALID_PARAMETER;
    else if (!strcmp(args[0], "loadfile") && args[1]) {
        size_t n = strlen(args[1]);
        strncpy(c->path, args[1], sizeof c->path - 1);
        c->pos = 0; c->ended = 0; c->eof_sent = 0; c->sent_loaded = 0; c->sent_end = 0;
        c->failed = n >= 4 && !strcmp(args[1] + n - 4, ".bad");
        c->loaded = !c->failed;
        rc = 0;
    } else if (!strcmp(args[0], "seek") && args[1]) {
        c->pos = atof(args[1]); if (c->pos < 0) c->pos = 0; if (c->pos > c->duration) c->pos = c->duration; c->ended = c->pos >= c->duration;
        rc = 0;
    }
    UNLOCK(c);
    return rc;
}
MPV_EXPORT mpv_event *mpv_wait_event(mpv_handle *h, double timeout) {
    Ctx *c = (Ctx *)h;
    double t0 = now_s();
    do {
        int have = 0;
        LOCK(c);
        advance(c);
        memset(&c->ev, 0, sizeof c->ev);
        if (c->failed && !c->sent_end) { c->sent_end = 1; memset(&c->end, 0, sizeof c->end); c->end.reason = MPV_END_FILE_REASON_ERROR; c->end.error = MPV_ERROR_LOADING_FAILED; c->ev.event_id = MPV_EVENT_END_FILE; c->ev.data = &c->end; have = 1; }
        else if (c->loaded && !c->sent_loaded) { c->sent_loaded = 1; c->ev.event_id = MPV_EVENT_FILE_LOADED; have = 1; }
        else if (c->ended && !c->eof_sent) { c->eof_sent = 1; memset(&c->end, 0, sizeof c->end); c->end.reason = MPV_END_FILE_REASON_EOF; c->ev.event_id = MPV_EVENT_END_FILE; c->ev.data = &c->end; have = 1; }
        UNLOCK(c);
        if (have) return &c->ev;
        if (timeout <= 0) break;
        nap();
    } while (now_s() - t0 < timeout);
    c->ev.event_id = MPV_EVENT_NONE;
    return &c->ev;
}

typedef struct Render { Ctx *c; double last_frame_pos; int started; } Render;
MPV_EXPORT int mpv_render_context_create(mpv_render_context **res, mpv_handle *mpv, mpv_render_param *params) {
    Render *r;
    int ok = 0;
    for (; params && params->type != MPV_RENDER_PARAM_INVALID; params++) if (params->type == MPV_RENDER_PARAM_API_TYPE && !strcmp((const char *)params->data, MPV_RENDER_API_TYPE_SW)) ok = 1;
    if (!ok) return MPV_ERROR_INVALID_PARAMETER;
    r = calloc(1, sizeof *r);
    if (!r) return MPV_ERROR_NOMEM;
    r->c = (Ctx *)mpv; r->last_frame_pos = -1.0;
    *res = (mpv_render_context *)r;
    return 0;
}
MPV_EXPORT uint64_t mpv_render_context_update(mpv_render_context *ctx) {
    Render *r = (Render *)ctx;
    uint64_t rc;
    LOCK(r->c);
    advance(r->c);
    rc = !r->c->loaded ? 0 : (r->c->pos - r->last_frame_pos >= 0.03 || r->c->pos < r->last_frame_pos || !r->started ? MPV_RENDER_UPDATE_FRAME : 0);
    UNLOCK(r->c);
    return rc;
}
MPV_EXPORT int mpv_render_context_render(mpv_render_context *ctx, mpv_render_param *params) {
    Render *r = (Render *)ctx;
    int w = 0, h = 0, x, y;
    size_t stride = 0;
    uint8_t *px = NULL;
    const char *fmt = NULL;
    for (; params && params->type != MPV_RENDER_PARAM_INVALID; params++) {
        if (params->type == MPV_RENDER_PARAM_SW_SIZE) { w = ((int *)params->data)[0]; h = ((int *)params->data)[1]; }
        else if (params->type == MPV_RENDER_PARAM_SW_STRIDE) stride = *(size_t *)params->data;
        else if (params->type == MPV_RENDER_PARAM_SW_POINTER) px = params->data;
        else if (params->type == MPV_RENDER_PARAM_SW_FORMAT) fmt = params->data;
    }
    if (w < 1 || h < 1 || !px || stride < (size_t)w * 4 || !fmt || strcmp(fmt, "rgb0")) return MPV_ERROR_INVALID_PARAMETER;
    LOCK(r->c);
    advance(r->c);
    {
        uint8_t red = (uint8_t)(r->c->pos * 100.0 > 255.0 ? 255 : (int)(r->c->pos * 100.0));
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) { uint8_t *p = px + (size_t)y * stride + (size_t)x * 4; p[0] = red; p[1] = (uint8_t)x; p[2] = (uint8_t)y; p[3] = 0; }
        r->last_frame_pos = r->c->pos; r->started = 1;
    }
    UNLOCK(r->c);
    return 0;
}
MPV_EXPORT void mpv_render_context_free(mpv_render_context *ctx) { free(ctx); }
