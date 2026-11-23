/* libmpv at run time: see include/exhaust_mpv.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "mpv/client.h"
#include "mpv/render.h"
#include "exhaust_mpv.h"

/* ---- the library ---- */

typedef struct Api {
    SDL_SharedObject *lib;
    unsigned long (*client_api_version)(void);
    mpv_handle *(*create)(void);
    int (*initialize)(mpv_handle *);
    void (*terminate_destroy)(mpv_handle *);
    int (*set_option_string)(mpv_handle *, const char *, const char *);
    int (*set_property_string)(mpv_handle *, const char *, const char *);
    int (*set_property)(mpv_handle *, const char *, mpv_format, void *);
    int (*get_property)(mpv_handle *, const char *, mpv_format, void *);
    int (*command)(mpv_handle *, const char **);
    mpv_event *(*wait_event)(mpv_handle *, double);
    const char *(*error_string)(int);
    int (*render_context_create)(mpv_render_context **, mpv_handle *, mpv_render_param *);
    int (*render_context_render)(mpv_render_context *, mpv_render_param *);
    uint64_t (*render_context_update)(mpv_render_context *);
    void (*render_context_free)(mpv_render_context *);
} Api;

static Api g_api;
static SDL_AtomicInt g_open_players;      /* players that may still call into the library, one closing in the background included (ex_mpv_unload waits for them) */
static SDL_Mutex *g_load_lock;
static char g_load_err[300];

static void *sym(const char *name) { return (void *)SDL_LoadFunction(g_api.lib, name); }

static int try_load(const char *path) {
    SDL_SharedObject *lib = SDL_LoadObject(path);
    Api a;
    if (!lib) return 0;
    memset(&a, 0, sizeof a);
    a.lib = lib;
    g_api.lib = lib;
    a.client_api_version = (unsigned long (*)(void))sym("mpv_client_api_version");
    a.create = (mpv_handle *(*)(void))sym("mpv_create");
    a.initialize = (int (*)(mpv_handle *))sym("mpv_initialize");
    a.terminate_destroy = (void (*)(mpv_handle *))sym("mpv_terminate_destroy");
    a.set_option_string = (int (*)(mpv_handle *, const char *, const char *))sym("mpv_set_option_string");
    a.set_property_string = (int (*)(mpv_handle *, const char *, const char *))sym("mpv_set_property_string");
    a.set_property = (int (*)(mpv_handle *, const char *, mpv_format, void *))sym("mpv_set_property");
    a.get_property = (int (*)(mpv_handle *, const char *, mpv_format, void *))sym("mpv_get_property");
    a.command = (int (*)(mpv_handle *, const char **))sym("mpv_command");
    a.wait_event = (mpv_event *(*)(mpv_handle *, double))sym("mpv_wait_event");
    a.error_string = (const char *(*)(int))sym("mpv_error_string");
    a.render_context_create = (int (*)(mpv_render_context **, mpv_handle *, mpv_render_param *))sym("mpv_render_context_create");
    a.render_context_render = (int (*)(mpv_render_context *, mpv_render_param *))sym("mpv_render_context_render");
    a.render_context_update = (uint64_t (*)(mpv_render_context *))sym("mpv_render_context_update");
    a.render_context_free = (void (*)(mpv_render_context *))sym("mpv_render_context_free");
    if (!a.client_api_version || !a.create || !a.initialize || !a.terminate_destroy || !a.set_option_string || !a.set_property_string || !a.set_property || !a.get_property ||
        !a.command || !a.wait_event || !a.error_string || !a.render_context_create || !a.render_context_render || !a.render_context_update || !a.render_context_free) {
        SDL_UnloadObject(lib); memset(&g_api, 0, sizeof g_api);
        snprintf(g_load_err, sizeof g_load_err, "%s is not a libmpv this version of Exhaust can use", path);
        return 0;
    }
    if ((a.client_api_version() >> 16) != 2) {                       /* the client API's major version is 2 for every libmpv of the last years */
        SDL_UnloadObject(lib); memset(&g_api, 0, sizeof g_api);
        snprintf(g_load_err, sizeof g_load_err, "%s has an API version Exhaust does not know", path);
        return 0;
    }
    g_api = a;
    return 1;
}

int ex_mpv_load(const char *path, char *err, size_t errcap) {
    int ok = 0;
    if (!g_load_lock) g_load_lock = SDL_CreateMutex();                // first call: not thread safe against another first call, which happens on the service thread only
    SDL_LockMutex(g_load_lock);
    if (g_api.lib) ok = 1;
    else {
        const char *base = SDL_GetBasePath(), *env = getenv("EXHAUST_MPV_LIB");
        static const char *const names[] = {
#if defined(_WIN32)
            "libmpv-2.dll", "mpv-2.dll", "mpv-1.dll", "libmpv.dll"
#elif defined(__APPLE__)
            "libmpv.2.dylib", "libmpv.dylib", "/opt/homebrew/lib/libmpv.dylib", "/usr/local/lib/libmpv.dylib"
#else
            "libmpv.so.2", "libmpv.so.1", "libmpv.so"
#endif
        };
        size_t i;
        g_load_err[0] = 0;
        if (path && *path) ok = try_load(path);
        else if (env && *env) ok = try_load(env);
        else {
            for (i = 0; i < sizeof names / sizeof *names && !ok; i++) {
                char full[900];
                if (base) { snprintf(full, sizeof full, "%saddons/%s", base, names[i]); ok = try_load(full); }
                if (!ok) ok = try_load(names[i]);
            }
        }
        if (!ok && !g_load_err[0]) snprintf(g_load_err, sizeof g_load_err, "libmpv is not installed (the media add-on)");
    }
    SDL_UnlockMutex(g_load_lock);
    if (!ok && err && errcap) snprintf(err, errcap, "%s", g_load_err);
    return ok;
}
int ex_mpv_loaded(void) { return g_api.lib != NULL; }
unsigned long ex_mpv_api_version(void) { return g_api.lib ? g_api.client_api_version() : 0; }
void ex_mpv_unload(void) {
    int i;
    for (i = 0; i < 10000 && SDL_GetAtomicInt(&g_open_players) > 0; i++) SDL_Delay(1);   /* a player closing in the background still calls the library */
    if (g_api.lib) SDL_UnloadObject(g_api.lib);
    memset(&g_api, 0, sizeof g_api);
}

/* ---- a player ---- */

struct ExMpv {
    mpv_handle *h;
    mpv_render_context *rc;
    SDL_Thread *thread;
    SDL_AtomicInt stop;
    SDL_AtomicInt state;
    SDL_AtomicInt pos_ms, dur_ms, vw, vh, tw, th;       /* milliseconds, the file's size, the size asked for */
    SDL_AtomicInt want_pause;                           /* -1 nothing, 0 play, 1 pause */
    SDL_AtomicInt seek_ms, volume;                      /* -1 nothing; 0..100 */
    SDL_AtomicInt frames, slowest_us;
    SDL_Mutex *lock;                                    /* guards the two frame buffers' pointers and `serial`, nothing else */
    uint8_t *ready, *front, *back;
    int rw, rh, fw, fh;                                 /* sizes of ready and front */
    unsigned long serial, front_serial;
    int video;
    char err[300];
};

static void pump_props(ExMpv *m) {
    double d;
    int flag;
    int64_t i;
    if (g_api.get_property(m->h, "time-pos", MPV_FORMAT_DOUBLE, &d) >= 0) SDL_SetAtomicInt(&m->pos_ms, (int)(d * 1000.0));
    if (g_api.get_property(m->h, "duration", MPV_FORMAT_DOUBLE, &d) >= 0) SDL_SetAtomicInt(&m->dur_ms, (int)(d * 1000.0));
    if (g_api.get_property(m->h, "width", MPV_FORMAT_INT64, &i) >= 0) SDL_SetAtomicInt(&m->vw, (int)i);
    if (g_api.get_property(m->h, "height", MPV_FORMAT_INT64, &i) >= 0) SDL_SetAtomicInt(&m->vh, (int)i);
    if (SDL_GetAtomicInt(&m->state) == EX_MPV_PAUSED || SDL_GetAtomicInt(&m->state) == EX_MPV_PLAYING) {
        if (g_api.get_property(m->h, "pause", MPV_FORMAT_FLAG, &flag) >= 0) SDL_SetAtomicInt(&m->state, flag ? EX_MPV_PAUSED : EX_MPV_PLAYING);
    }
}

static void pump_commands(ExMpv *m) {
    int w = SDL_SetAtomicInt(&m->want_pause, -1), sk = SDL_SetAtomicInt(&m->seek_ms, -1), vol = SDL_SetAtomicInt(&m->volume, -1);
    if (w >= 0) { int flag = w; g_api.set_property(m->h, "pause", MPV_FORMAT_FLAG, &flag); SDL_SetAtomicInt(&m->state, w ? EX_MPV_PAUSED : EX_MPV_PLAYING); }
    if (sk >= 0) {
        char t[40];
        const char *cmd[] = { "seek", t, "absolute", NULL };
        snprintf(t, sizeof t, "%.3f", (double)sk / 1000.0);
        g_api.command(m->h, cmd);
        if (SDL_GetAtomicInt(&m->state) == EX_MPV_ENDED) SDL_SetAtomicInt(&m->state, EX_MPV_PAUSED);
        SDL_SetAtomicInt(&m->pos_ms, sk);
    }
    if (vol >= 0) { double d = (double)vol; g_api.set_property(m->h, "volume", MPV_FORMAT_DOUBLE, &d); }
}

static void pump_render(ExMpv *m, uint8_t **back, int *bw, int *bh) {
    int w = SDL_GetAtomicInt(&m->tw), h = SDL_GetAtomicInt(&m->th);
    uint64_t flags;
    Uint64 t0, t1;
    if (!m->rc || w < 2 || h < 2) return;
    flags = g_api.render_context_update(m->rc);
    if (!(flags & MPV_RENDER_UPDATE_FRAME) && *back && *bw == w && *bh == h && SDL_GetAtomicInt(&m->frames) > 0) return;      /* nothing new at this size */
    if (!*back || *bw != w || *bh != h) {
        uint8_t *nb = realloc(*back, (size_t)w * (size_t)h * 4);
        if (!nb) return;
        *back = nb; *bw = w; *bh = h;
    }
    {
        int size[2]; size_t stride = (size_t)w * 4; int r;
        mpv_render_param p[5];
        size[0] = w; size[1] = h;
        p[0].type = MPV_RENDER_PARAM_SW_SIZE; p[0].data = size;
        p[1].type = MPV_RENDER_PARAM_SW_FORMAT; p[1].data = (void *)"rgb0";
        p[2].type = MPV_RENDER_PARAM_SW_STRIDE; p[2].data = &stride;
        p[3].type = MPV_RENDER_PARAM_SW_POINTER; p[3].data = *back;
        p[4].type = MPV_RENDER_PARAM_INVALID; p[4].data = NULL;
        t0 = SDL_GetPerformanceCounter();
        r = g_api.render_context_render(m->rc, p);
        t1 = SDL_GetPerformanceCounter();
        if (r < 0) return;
        {
            int us = (int)((t1 - t0) * 1000000 / SDL_GetPerformanceFrequency()), old = SDL_GetAtomicInt(&m->slowest_us);
            if (us > old) SDL_SetAtomicInt(&m->slowest_us, us);
        }
        {   /* "rgb0" leaves the fourth byte undefined: make it opaque */
            size_t k, n = (size_t)w * (size_t)h;
            for (k = 0; k < n; k++) (*back)[k * 4 + 3] = 255;
        }
    }
    SDL_LockMutex(m->lock);
    { uint8_t *t = m->ready; int tw2 = m->rw, th2 = m->rh; m->ready = *back; m->rw = *bw; m->rh = *bh; *back = t; *bw = tw2; *bh = th2; }
    m->serial++;
    SDL_UnlockMutex(m->lock);
    SDL_AddAtomicInt(&m->frames, 1);
}

static int SDLCALL pump(void *arg) {
    ExMpv *m = arg;
    uint8_t *back = NULL;
    int bw = 0, bh = 0, tick = 0;
    while (!SDL_GetAtomicInt(&m->stop)) {
        mpv_event *ev = g_api.wait_event(m->h, m->video && SDL_GetAtomicInt(&m->state) == EX_MPV_PLAYING ? 0.008 : 0.03);
        while (ev && ev->event_id != MPV_EVENT_NONE) {
            if (ev->event_id == MPV_EVENT_END_FILE) {
                mpv_event_end_file *e = ev->data;
                if (e && e->reason == MPV_END_FILE_REASON_ERROR) { snprintf(m->err, sizeof m->err, "%s", g_api.error_string(e->error)); SDL_SetAtomicInt(&m->state, EX_MPV_FAILED); }
                else if (e && (e->reason == MPV_END_FILE_REASON_EOF)) SDL_SetAtomicInt(&m->state, EX_MPV_ENDED);
            } else if (ev->event_id == MPV_EVENT_FILE_LOADED) { if (SDL_GetAtomicInt(&m->state) == EX_MPV_LOADING) SDL_SetAtomicInt(&m->state, EX_MPV_PAUSED); }
            else if (ev->event_id == MPV_EVENT_SHUTDOWN) { SDL_SetAtomicInt(&m->stop, 1); break; }
            ev = g_api.wait_event(m->h, 0);
        }
        pump_commands(m);
        if ((tick++ & 3) == 0) pump_props(m);
        if (m->video) pump_render(m, &back, &bw, &bh);
    }
    free(back);
    return 0;
}

ExMpv *ex_mpv_open(const char *abs_path, int video, char *err, size_t errcap) {
    ExMpv *m;
    const char *cmd[] = { "loadfile", abs_path, "replace", NULL };
    int r;
    if (!ex_mpv_loaded() && !ex_mpv_load(NULL, err, errcap)) return NULL;
    if (!abs_path || !*abs_path) { if (err && errcap) snprintf(err, errcap, "no file"); return NULL; }
    m = calloc(1, sizeof *m);
    if (!m) { if (err && errcap) snprintf(err, errcap, "out of memory"); return NULL; }
    m->video = video;
    m->lock = SDL_CreateMutex();
    SDL_SetAtomicInt(&m->want_pause, -1); SDL_SetAtomicInt(&m->seek_ms, -1); SDL_SetAtomicInt(&m->volume, -1);
    SDL_SetAtomicInt(&m->state, EX_MPV_LOADING);
    SDL_SetAtomicInt(&m->tw, 640); SDL_SetAtomicInt(&m->th, 360);
    m->h = g_api.create();
    if (!m->h) { if (err && errcap) snprintf(err, errcap, "libmpv could not start"); SDL_DestroyMutex(m->lock); free(m); return NULL; }
    /* what libmpv may and may not do: a local file only, no scripts, no user configuration, no downloader, no terminal, no window of its own */
    g_api.set_option_string(m->h, "config", "no");
    g_api.set_option_string(m->h, "load-scripts", "no");
    g_api.set_option_string(m->h, "ytdl", "no");
    g_api.set_option_string(m->h, "terminal", "no");
    g_api.set_option_string(m->h, "input-default-bindings", "no");
    g_api.set_option_string(m->h, "input-vo-keyboard", "no");
    g_api.set_option_string(m->h, "osc", "no");
    g_api.set_option_string(m->h, "load-unsafe-playlists", "no");
    g_api.set_option_string(m->h, "access-references", "no");
    g_api.set_option_string(m->h, "use-filedir-conf", "no");
    g_api.set_option_string(m->h, "pause", "yes");
    g_api.set_option_string(m->h, "keep-open", "yes");
    g_api.set_option_string(m->h, "idle", "yes");
    g_api.set_option_string(m->h, "vo", video ? "libmpv" : "null");
    if (!video) g_api.set_option_string(m->h, "vid", "no");
    g_api.set_option_string(m->h, "audio-display", "no");
    r = g_api.initialize(m->h);
    if (r < 0) { if (err && errcap) snprintf(err, errcap, "libmpv: %s", g_api.error_string(r)); g_api.terminate_destroy(m->h); SDL_DestroyMutex(m->lock); free(m); return NULL; }
    if (video) {
        mpv_render_param p[2];
        p[0].type = MPV_RENDER_PARAM_API_TYPE; p[0].data = (void *)MPV_RENDER_API_TYPE_SW;
        p[1].type = MPV_RENDER_PARAM_INVALID; p[1].data = NULL;
        r = g_api.render_context_create(&m->rc, m->h, p);
        if (r < 0) { if (err && errcap) snprintf(err, errcap, "libmpv: %s", g_api.error_string(r)); g_api.terminate_destroy(m->h); SDL_DestroyMutex(m->lock); free(m); return NULL; }
    }
    r = g_api.command(m->h, cmd);
    if (r < 0) {
        if (err && errcap) snprintf(err, errcap, "libmpv: %s", g_api.error_string(r));
        if (m->rc) g_api.render_context_free(m->rc);
        g_api.terminate_destroy(m->h); SDL_DestroyMutex(m->lock); free(m);
        return NULL;
    }
    m->thread = SDL_CreateThread(pump, "exhaust-mpv", m);
    if (!m->thread) {
        if (err && errcap) snprintf(err, errcap, "the player thread could not start");
        if (m->rc) g_api.render_context_free(m->rc);
        g_api.terminate_destroy(m->h); SDL_DestroyMutex(m->lock); free(m);
        return NULL;
    }
    SDL_AddAtomicInt(&g_open_players, 1);
    return m;
}

void ex_mpv_close(ExMpv *m) {
    if (!m) return;
    SDL_SetAtomicInt(&m->stop, 1);
    if (m->thread) SDL_WaitThread(m->thread, NULL);
    if (m->rc) g_api.render_context_free(m->rc);                    /* the render context goes before the handle */
    g_api.terminate_destroy(m->h);
    free(m->ready); free(m->front);
    SDL_DestroyMutex(m->lock);
    free(m);
    SDL_AddAtomicInt(&g_open_players, -1);
}

static int SDLCALL closer(void *arg) { ex_mpv_close((ExMpv *)arg); return 0; }
void ex_mpv_close_async(ExMpv *m) {
    SDL_Thread *t;
    if (!m) return;
    t = SDL_CreateThread(closer, "exhaust-mpv-close", m);
    if (t) SDL_DetachThread(t); else ex_mpv_close(m);
}

void ex_mpv_play(ExMpv *m) { if (m) { if (SDL_GetAtomicInt(&m->state) == EX_MPV_ENDED) SDL_SetAtomicInt(&m->seek_ms, 0); SDL_SetAtomicInt(&m->want_pause, 0); } }
void ex_mpv_pause(ExMpv *m) { if (m) SDL_SetAtomicInt(&m->want_pause, 1); }
void ex_mpv_toggle(ExMpv *m) { if (!m) return; if (SDL_GetAtomicInt(&m->state) == EX_MPV_PLAYING) ex_mpv_pause(m); else ex_mpv_play(m); }
void ex_mpv_seek(ExMpv *m, double seconds) { if (m) { int ms = (int)(seconds * 1000.0); SDL_SetAtomicInt(&m->seek_ms, ms < 0 ? 0 : ms); } }
void ex_mpv_set_volume(ExMpv *m, float v) { if (m) { int p = (int)(v * 100.0f + 0.5f); SDL_SetAtomicInt(&m->volume, p < 0 ? 0 : p > 100 ? 100 : p); } }
ExMpvState ex_mpv_state(const ExMpv *m) { return m ? (ExMpvState)SDL_GetAtomicInt((SDL_AtomicInt *)&m->state) : EX_MPV_FAILED; }
double ex_mpv_position(const ExMpv *m) { return m ? (double)SDL_GetAtomicInt((SDL_AtomicInt *)&m->pos_ms) / 1000.0 : 0.0; }
double ex_mpv_duration(const ExMpv *m) { return m ? (double)SDL_GetAtomicInt((SDL_AtomicInt *)&m->dur_ms) / 1000.0 : 0.0; }
const char *ex_mpv_error(const ExMpv *m) { return m ? m->err : ""; }
void ex_mpv_video_size(const ExMpv *m, int *w, int *h) { *w = m ? SDL_GetAtomicInt((SDL_AtomicInt *)&m->vw) : 0; *h = m ? SDL_GetAtomicInt((SDL_AtomicInt *)&m->vh) : 0; }
void ex_mpv_set_target_size(ExMpv *m, int w, int h) { if (!m) return; SDL_SetAtomicInt(&m->tw, w < 2 ? 2 : w > 4096 ? 4096 : w); SDL_SetAtomicInt(&m->th, h < 2 ? 2 : h > 4096 ? 4096 : h); }

const uint8_t *ex_mpv_frame(ExMpv *m, int *w, int *h, unsigned long *serial) {
    if (!m) return NULL;
    SDL_LockMutex(m->lock);
    if (m->serial != m->front_serial && m->ready) { uint8_t *t = m->front; int tw = m->fw, th = m->fh; m->front = m->ready; m->fw = m->rw; m->fh = m->rh; m->ready = t; m->rw = tw; m->rh = th; m->front_serial = m->serial; }
    SDL_UnlockMutex(m->lock);
    if (w) *w = m->fw;
    if (h) *h = m->fh;
    if (serial) *serial = m->front_serial;
    return m->front;
}

void ex_mpv_stats(const ExMpv *m, unsigned long *frames, double *slowest_ms) {
    if (frames) *frames = m ? (unsigned long)SDL_GetAtomicInt((SDL_AtomicInt *)&m->frames) : 0;
    if (slowest_ms) *slowest_ms = m ? (double)SDL_GetAtomicInt((SDL_AtomicInt *)&m->slowest_us) / 1000.0 : 0.0;
}
