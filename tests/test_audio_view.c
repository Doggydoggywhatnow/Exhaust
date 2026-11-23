/* audio_view (6c-ii): the audio tab through the real UI with a real player on SDL's dummy output driver. Opening the tab asks for the file once; the answer
 * (this test plays the service thread: it opens the decoder and the device) attaches it paused; the play button, Space, the position bar, the arrow keys
 * and the volume bar do what they say; a file that cannot be played says why and offers "Open externally"; a second tab with another file shows its own
 * state while the player holds the first.
 * Usage: test_audio_view <folder with the sample files> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_audioview.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define W 900
#define H 600

static ExRenderer *R;
static ExUi *UI;
static ExWidget *ROOT, *AW;
static ExPlayer *PLAYER;
static const char *DIR;
static int loads, externals;
static char loaded_path[200];

static void load_cb(void *u, const char *path) { (void)u; loads++; snprintf(loaded_path, sizeof loaded_path, "%s", path); }
static void ext_cb(void *u, const char *path) { (void)u; externals++; snprintf(loaded_path, sizeof loaded_path, "%s", path); }

/* the service thread's part: reads, opens the decoder and the device, and the UI thread attaches */
static void serve(ExWidget *w, const char *path) {
    char full[600], err[200] = "";
    size_t len = 0;
    uint8_t *bytes;
    ExAudioDecoder *d;
    snprintf(full, sizeof full, "%s/%s", DIR, path);
    bytes = (uint8_t *)ex_platform_read_file(full, &len);
    d = bytes ? ex_audio_decoder_open(bytes, len, err, sizeof err) : NULL;
    if (!d) { ex_audioview_loaded(w, err[0] ? err : "This file cannot be read."); return; }
    if (ex_player_prepare(PLAYER, err, sizeof err) != 0 || ex_player_attach(PLAYER, d) != 0) { ex_audioview_loaded(w, "No sound output."); return; }
    ex_player_set_path(PLAYER, path);
    ex_audioview_loaded(w, NULL);
}

static void frame(void) {
    ex_ui_layout(UI, ROOT, W, H);
    ex_renderer_clear(R, (ExColor){0, 0, 0, 255});
    ex_ui_draw(UI, ROOT, 1);
    SDL_FlushRenderer(SDL_GetRenderer(ex_renderer_window(R)));
}
static void open_view(const char *path) {
    ExAudioViewHandlers h;
    memset(&h, 0, sizeof h);
    h.user = NULL; h.load = load_cb; h.open_external = ext_cb;
    if (AW) { ex_ui_release(UI, AW); ex_widget_remove(ROOT, AW); ex_widget_free(AW); AW = NULL; }
    AW = ex_widget_audioview(UI, PLAYER, path, &h);
    ex_widget_add(ROOT, AW);
    frame();
}
static void click(float x, float y) { ex_ui_mouse(UI, ROOT, EX_MOUSE_DOWN, x, y, 0, 1); ex_ui_mouse(UI, ROOT, EX_MOUSE_UP, x, y, 0, 1); frame(); }
static void key(ExKey k) { ex_ui_key(UI, k, 0); frame(); }

int main(int argc, char **argv) {
    ExTheme theme;
    ExAudioViewStats st;
    if (argc < 2) return 2;
    DIR = argv[1];
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    CHECK(ex_theme_init_builtin(&theme, EX_THEME_DARK) == 0);
    R = ex_renderer_create("audio view", W, H);
    if (!R) { fprintf(stderr, "renderer: %s\n", ex_render_error()); return 1; }
    UI = ex_ui_create(R, &theme, 16.0f);
    ROOT = ex_widget_box(UI, 1, "root");
    ROOT->width = ex_size_grow(); ROOT->height = ex_size_grow();
    PLAYER = ex_player_create();

    /* ---- opening the tab asks for the file, once ---- */
    open_view("tone16.wav");
    ex_audioview_stats(AW, &st);
    CHECK(loads == 1 && !strcmp(loaded_path, "tone16.wav") && st.loading && !st.attached);
    frame(); frame();
    CHECK(loads == 1);
    serve(AW, "tone16.wav");
    frame();
    ex_audioview_stats(AW, &st);
    CHECK(!st.loading && !st.failed && st.attached && !st.playing && st.duration > 0.39 && st.duration < 0.41);

    /* ---- the play button and Space ---- */
    click(st.btn_x, st.btn_y);
    SDL_Delay(80);
    frame();
    ex_audioview_stats(AW, &st);
    CHECK(st.playing && st.position > 0.0);
    click(st.btn_x, st.btn_y);
    ex_audioview_stats(AW, &st);
    CHECK(!st.playing);
    key(EX_KEY_SPACE);
    ex_audioview_stats(AW, &st);
    CHECK(st.playing);
    key(EX_KEY_SPACE);
    ex_audioview_stats(AW, &st);
    CHECK(!st.playing);

    /* ---- the position bar and the arrow keys ---- */
    click(st.bar_x + st.bar_w * 0.5f, st.bar_y);
    SDL_Delay(100);
    frame();
    ex_audioview_stats(AW, &st);
    CHECK(st.position > 0.17 && st.position < 0.23);                             /* half of 0.4 s */
    key(EX_KEY_RIGHT);                                                           /* 5 seconds on: the end of a 0.4 s file */
    SDL_Delay(100);
    ex_audioview_stats(AW, &st);
    CHECK(st.position >= 0.39);
    key(EX_KEY_HOME);
    SDL_Delay(100);
    ex_audioview_stats(AW, &st);
    CHECK(st.position < 0.05);

    /* ---- the volume bar and the up and down keys ---- */
    click(st.vol_x + st.vol_w * 0.5f, st.vol_y);
    CHECK(ex_player_volume(PLAYER) > 0.45f && ex_player_volume(PLAYER) < 0.55f);
    key(EX_KEY_DOWN);
    CHECK(ex_player_volume(PLAYER) > 0.40f && ex_player_volume(PLAYER) < 0.50f);
    key(EX_KEY_UP); key(EX_KEY_UP);
    CHECK(ex_player_volume(PLAYER) > 0.50f && ex_player_volume(PLAYER) < 0.60f);

    /* ---- a second tab for another file while the player holds the first: not attached, loads on its own ---- */
    open_view("tone.flac");
    ex_audioview_stats(AW, &st);
    CHECK(loads == 2 && !st.attached && st.loading);
    serve(AW, "tone.flac");
    frame();
    ex_audioview_stats(AW, &st);
    CHECK(st.attached && !st.playing);

    /* ---- a file that cannot be played says why and offers to open it elsewhere ---- */
    open_view("tone.raw");                                                       /* raw samples: not an audio file */
    serve(AW, "tone.raw");
    frame();
    ex_audioview_stats(AW, &st);
    CHECK(st.failed && !st.loading && !st.attached);
    click(st.ext_x, st.ext_y);
    CHECK(externals == 1 && !strcmp(loaded_path, "tone.raw"));

    /* ---- Play before the file is loaded plays it when it arrives ---- */
    loads = 0;
    open_view("tone16.wav");
    click(W * 0.5f, H - 20.0f);                                                  /* focus the tab */
    key(EX_KEY_SPACE);
    CHECK(loads == 1);                                                           /* the load asked for at open is the one that is on its way */
    serve(AW, "tone16.wav");
    SDL_Delay(60);
    frame();
    ex_audioview_stats(AW, &st);
    CHECK(st.attached && st.playing);

    ex_ui_release(UI, AW);
    ex_widget_remove(ROOT, AW);
    ex_widget_free(AW);
    ex_player_free(PLAYER);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("audio_view: ok\n");
    return 0;
}
