/* media_view (6c-iii): the media tab through the real UI with the stand-in libmpv (tests/stub_mpv.c). Opening the tab asks for the file once; the answer
 * brings a paused player; the button, Space, the position bar and the arrow keys work; the picture the player's thread draws is uploaded as a texture and
 * drawn; a file that fails says so and offers "Open externally"; without the library the tab says the media add-on is missing, offers the same, and
 * nothing fails.
 * Usage: test_media_view <path of the stand-in library> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_mediaview.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define W 900
#define H 600

static ExRenderer *R;
static ExUi *UI;
static ExWidget *ROOT, *MW;
static ExMpv *PLAYER;
static char PLAYER_PATH[200];
static int loads, externals;
static const char *STUB;

static void load_cb(void *u, const char *path) { (void)u; loads++; (void)path; }
static ExMpv *current_cb(void *u, const char *path) { (void)u; return PLAYER && !strcmp(PLAYER_PATH, path) ? PLAYER : NULL; }
static void ext_cb(void *u, const char *path) { (void)u; externals++; (void)path; }

/* the service thread's part */
static void serve(const char *path) {
    char err[300] = "";
    ExMpv *m = ex_mpv_open(path, 1, err, sizeof err);
    if (!m) { ex_mediaview_loaded(MW, err[0] ? err : "cannot play"); return; }
    if (PLAYER) ex_mpv_close(PLAYER);
    PLAYER = m; snprintf(PLAYER_PATH, sizeof PLAYER_PATH, "%s", path);
    ex_mediaview_loaded(MW, NULL);
}

static void frame(void) {
    ex_ui_layout(UI, ROOT, W, H);
    ex_renderer_clear(R, (ExColor){0, 0, 0, 255});
    ex_ui_draw(UI, ROOT, 1);
    SDL_FlushRenderer(SDL_GetRenderer(ex_renderer_window(R)));
}
static void open_view(const char *path) {
    ExMediaViewHandlers h;
    memset(&h, 0, sizeof h);
    h.load = load_cb; h.current = current_cb; h.open_external = ext_cb;
    if (MW) { ex_ui_release(UI, MW); ex_widget_remove(ROOT, MW); ex_widget_free(MW); MW = NULL; }
    MW = ex_widget_mediaview(UI, path, &h);
    ex_widget_add(ROOT, MW);
    frame();
}
static void click(float x, float y) { ex_ui_mouse(UI, ROOT, EX_MOUSE_DOWN, x, y, 0, 1); ex_ui_mouse(UI, ROOT, EX_MOUSE_UP, x, y, 0, 1); frame(); }
static void key(ExKey k) { ex_ui_key(UI, k, 0); frame(); }
/* The player works on its own thread: wait until a condition holds (up to about 8 seconds on a slow runner) instead of sleeping a fixed time. The condition may read `st`. */
#define WAIT_FOR(expr) do { int w_; for (w_ = 0; w_ < 4000; w_++) { frame(); ex_mediaview_stats(MW, &st); if (expr) break; SDL_Delay(2); } } while (0)

int main(int argc, char **argv) {
    ExTheme theme;
    ExMediaViewStats st;
    char err[300];
    int i;
    if (argc < 2) return 2;
    STUB = argv[1];
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    CHECK(ex_theme_init_builtin(&theme, EX_THEME_DARK) == 0);
    R = ex_renderer_create("media view", W, H);
    if (!R) { fprintf(stderr, "renderer: %s\n", ex_render_error()); return 1; }
    UI = ex_ui_create(R, &theme, 16.0f);
    ROOT = ex_widget_box(UI, 1, "root");
    ROOT->width = ex_size_grow(); ROOT->height = ex_size_grow();

    /* ---- without libmpv: the tab says so and offers the file to the system; nothing fails ---- */
    open_view("/media/a.mp4");
    CHECK(loads == 1);
    serve("/media/a.mp4");                                                        /* no library loaded: the open fails with the reason */
    frame();
    ex_mediaview_stats(MW, &st);
    CHECK(st.failed && !st.attached);
    click(st.ext_x, st.ext_y);
    CHECK(externals == 1);

    /* ---- with the stand-in ---- */
    CHECK(ex_mpv_load(STUB, err, sizeof err) == 1);                               /* "b.long.mp4" is 40 seconds in the stand-in: at two seconds a slow macOS runner reached the end before the first pause, and Space then meant "play again" (ISSUES 76) */
    loads = 0;
    open_view("/media/b.long.mp4");
    CHECK(loads == 1);
    frame(); frame();
    CHECK(loads == 1);                                                            /* asked once */
    serve("/media/b.long.mp4");
    SDL_Delay(150);
    frame();
    ex_mediaview_stats(MW, &st);
    CHECK(st.attached && !st.playing && !st.failed && !st.loading);
    CHECK(st.has_picture && st.frames_uploaded >= 1);                              /* the first frame is drawn even while paused */
    click(st.btn_x, st.btn_y);
    SDL_Delay(200);
    {
        unsigned long u0;
        frame();
        ex_mediaview_stats(MW, &st);
        CHECK(st.playing);
        u0 = st.frames_uploaded;
        for (i = 0; i < 30; i++) { SDL_Delay(8); frame(); }
        ex_mediaview_stats(MW, &st);
        CHECK(st.frames_uploaded > u0 + 3);                                       /* new frames keep arriving */
    }
    key(EX_KEY_SPACE);
    WAIT_FOR(!st.playing);
    CHECK(!st.playing);
    click(st.bar_x + st.bar_w * 0.75f, st.bar_y);
    WAIT_FOR(ex_mpv_position(PLAYER) > 29.5 && ex_mpv_position(PLAYER) < 30.5);
    CHECK(ex_mpv_position(PLAYER) > 29.5 && ex_mpv_position(PLAYER) < 30.5);       /* three quarters of 40 seconds */
    key(EX_KEY_HOME);
    WAIT_FOR(ex_mpv_position(PLAYER) < 0.1);
    CHECK(ex_mpv_position(PLAYER) < 0.1);
    key(EX_KEY_SPACE);                                                            /* Space plays again */
    WAIT_FOR(st.playing);
    if (!st.playing) fprintf(stderr, "media_view: Space did not play again: state %d, position %.3f, frames %lu (macOS CI, ISSUES 76)\n", (int)ex_mpv_state(PLAYER), ex_mpv_position(PLAYER), st.frames_uploaded);
    CHECK(st.playing);

    /* ---- a file that fails to play ---- */
    open_view("/media/c.bad");
    serve("/media/c.bad");                                                        /* opens, then fails: the tab shows the player's error */
    SDL_Delay(150);
    frame();
    ex_mediaview_stats(MW, &st);
    CHECK(st.attached && !st.playing);
    CHECK(ex_mpv_state(PLAYER) == EX_MPV_FAILED);
    frame();

    ex_ui_release(UI, MW);
    ex_widget_remove(ROOT, MW);
    ex_widget_free(MW);
    if (PLAYER) ex_mpv_close(PLAYER);
    ex_mpv_unload();
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("media_view: ok\n");
    return 0;
}
