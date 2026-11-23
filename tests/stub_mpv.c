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
#else
static void nap(void) { struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = 1000000L; nanosleep(&ts, NULL); }
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
} Ctx;

static double now_s(void) { struct timespec ts; timespec_get(&ts, TIME_UTC); return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9; }

static void advance(Ctx *c) {
    double t = now_s();
    if (c->loaded && !c->paused && !c->ended) { c->pos += t - c->t_last; if (c->pos >= c->duration) { c->pos = c->duration; c->ended = 1; } }
    c->t_last = t;
}

MPV_EXPORT unsigned long mpv_client_api_version(void) { return MPV_MAKE_VERSION(2, 5); }
MPV_EXPORT const char *mpv_error_string(int e) { return e == MPV_ERROR_LOADING_FAILED ? "loading failed" : e == MPV_ERROR_INVALID_PARAMETER ? "invalid parameter" : "error"; }
MPV_EXPORT mpv_handle *mpv_create(void) { Ctx *c = calloc(1, sizeof *c); if (c) { c->paused = 1; c->volume = 100.0; c->duration = 2.0; c->t_last = now_s(); } return (mpv_handle *)c; }
MPV_EXPORT int mpv_initialize(mpv_handle *h) { (void)h; return 0; }
MPV_EXPORT void mpv_terminate_destroy(mpv_handle *h) { free(h); }
MPV_EXPORT int mpv_set_option_string(mpv_handle *h, const char *n, const char *d) { (void)h; (void)n; (void)d; return 0; }
MPV_EXPORT int mpv_set_property_string(mpv_handle *h, const char *n, const char *d) { (void)h; (void)n; (void)d; return 0; }
MPV_EXPORT int mpv_set_property(mpv_handle *h, const char *n, mpv_format f, void *d) {
    Ctx *c = (Ctx *)h;
    advance(c);
    if (!strcmp(n, "pause") && f == MPV_FORMAT_FLAG) { c->paused = *(int *)d; return 0; }
    if (!strcmp(n, "volume") && f == MPV_FORMAT_DOUBLE) { c->volume = *(double *)d; return 0; }
    return MPV_ERROR_PROPERTY_NOT_FOUND;
}
MPV_EXPORT int mpv_get_property(mpv_handle *h, const char *n, mpv_format f, void *d) {
    Ctx *c = (Ctx *)h;
    advance(c);
    if (!c->loaded) return MPV_ERROR_PROPERTY_UNAVAILABLE;
    if (!strcmp(n, "time-pos") && f == MPV_FORMAT_DOUBLE) { *(double *)d = c->pos; return 0; }
    if (!strcmp(n, "duration") && f == MPV_FORMAT_DOUBLE) { *(double *)d = c->duration; return 0; }
    if (!strcmp(n, "pause") && f == MPV_FORMAT_FLAG) { *(int *)d = c->paused; return 0; }
    if (!strcmp(n, "width") && f == MPV_FORMAT_INT64) { *(int64_t *)d = 64; return 0; }
    if (!strcmp(n, "height") && f == MPV_FORMAT_INT64) { *(int64_t *)d = 48; return 0; }
    if (!strcmp(n, "volume") && f == MPV_FORMAT_DOUBLE) { *(double *)d = c->volume; return 0; }
    return MPV_ERROR_PROPERTY_NOT_FOUND;
}
MPV_EXPORT int mpv_command(mpv_handle *h, const char **args) {
    Ctx *c = (Ctx *)h;
    advance(c);
    if (!args || !args[0]) return MPV_ERROR_INVALID_PARAMETER;
    if (!strcmp(args[0], "loadfile") && args[1]) {
        size_t n = strlen(args[1]);
        strncpy(c->path, args[1], sizeof c->path - 1);
        c->pos = 0; c->ended = 0; c->eof_sent = 0; c->sent_loaded = 0; c->sent_end = 0;
        c->failed = n >= 4 && !strcmp(args[1] + n - 4, ".bad");
        c->loaded = !c->failed;
        return 0;
    }
    if (!strcmp(args[0], "seek") && args[1]) { c->pos = atof(args[1]); if (c->pos < 0) c->pos = 0; if (c->pos > c->duration) c->pos = c->duration; c->ended = c->pos >= c->duration; return 0; }
    return MPV_ERROR_INVALID_PARAMETER;
}
MPV_EXPORT mpv_event *mpv_wait_event(mpv_handle *h, double timeout) {
    Ctx *c = (Ctx *)h;
    double t0 = now_s();
    do {
        advance(c);
        memset(&c->ev, 0, sizeof c->ev);
        if (c->failed && !c->sent_end) { c->sent_end = 1; memset(&c->end, 0, sizeof c->end); c->end.reason = MPV_END_FILE_REASON_ERROR; c->end.error = MPV_ERROR_LOADING_FAILED; c->ev.event_id = MPV_EVENT_END_FILE; c->ev.data = &c->end; return &c->ev; }
        if (c->loaded && !c->sent_loaded) { c->sent_loaded = 1; c->ev.event_id = MPV_EVENT_FILE_LOADED; return &c->ev; }
        if (c->ended && !c->eof_sent) { c->eof_sent = 1; memset(&c->end, 0, sizeof c->end); c->end.reason = MPV_END_FILE_REASON_EOF; c->ev.event_id = MPV_EVENT_END_FILE; c->ev.data = &c->end; return &c->ev; }
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
    advance(r->c);
    if (!r->c->loaded) return 0;
    return r->c->pos - r->last_frame_pos >= 0.03 || r->c->pos < r->last_frame_pos || !r->started ? MPV_RENDER_UPDATE_FRAME : 0;
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
    advance(r->c);
    {
        uint8_t red = (uint8_t)(r->c->pos * 100.0 > 255.0 ? 255 : (int)(r->c->pos * 100.0));
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) { uint8_t *p = px + (size_t)y * stride + (size_t)x * 4; p[0] = red; p[1] = (uint8_t)x; p[2] = (uint8_t)y; p[3] = 0; }
        r->last_frame_pos = r->c->pos; r->started = 1;
    }
    return 0;
}
MPV_EXPORT void mpv_render_context_free(mpv_render_context *ctx) { free(ctx); }
