/* mpv_loader (6c-iii): libmpv loaded at run time. Without the library everything says so and nothing fails; with a stand-in library (tests/stub_mpv.c) a
 * player opens a file, plays, pauses, seeks, reaches the end and plays again, draws frames at the size asked for from its own thread (no frame is torn),
 * a file that fails says why, an audio-only player draws nothing, and closing joins the thread.
 * Usage: test_mpv <path of the stand-in library> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_mpv.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void wait_state(ExMpv *m, ExMpvState want, int ms) { int i; for (i = 0; i < ms && ex_mpv_state(m) != want; i++) SDL_Delay(1); }

int main(int argc, char **argv) {
    char err[300] = "";
    ExMpv *m;
    unsigned long serial = 0, s0, frames;
    int w = 0, h = 0, vw, vh, i;
    const uint8_t *px;
    double slow;
    if (argc < 2) return 2;

    /* ---- without the library: a reason, no crash, and "Open externally" is the application's to offer ---- */
    CHECK(!ex_mpv_loaded() && ex_mpv_api_version() == 0);
    CHECK(ex_mpv_load("/nonexistent/folder/libmpv.so.2", err, sizeof err) == 0 && err[0]);
    CHECK(!ex_mpv_loaded());
    CHECK(ex_mpv_load(argv[0], err, sizeof err) == 0 && err[0]);                              /* a file that is not libmpv */
    CHECK(ex_mpv_open("/tmp/x.mp4", 1, err, sizeof err) == NULL && err[0]);
    ex_mpv_play(NULL); ex_mpv_seek(NULL, 1.0); ex_mpv_close(NULL);                          /* calls on nothing are harmless */
    CHECK(ex_mpv_state(NULL) == EX_MPV_FAILED && ex_mpv_position(NULL) == 0.0);

    /* ---- with the stand-in ---- */
    CHECK(ex_mpv_load(argv[1], err, sizeof err) == 1);
    CHECK(ex_mpv_loaded() && ex_mpv_api_version() >> 16 == 2);
    CHECK(ex_mpv_load(NULL, err, sizeof err) == 1);                                          /* already loaded */
    m = ex_mpv_open("/media/a.mp4", 1, err, sizeof err);
    CHECK(m != NULL);
    if (!m) { fprintf(stderr, "open: %s\n", err); return 1; }
    wait_state(m, EX_MPV_PAUSED, 1000);
    CHECK(ex_mpv_state(m) == EX_MPV_PAUSED);                                                  /* loaded, paused at the start */
    SDL_Delay(80);
    CHECK(ex_mpv_duration(m) > 1.9 && ex_mpv_duration(m) < 2.1);
    ex_mpv_video_size(m, &vw, &vh);
    CHECK(vw == 64 && vh == 48);
    CHECK(ex_mpv_position(m) < 0.05);
    ex_mpv_set_target_size(m, 100, 60);
    SDL_Delay(100);
    px = ex_mpv_frame(m, &w, &h, &serial);
    CHECK(px != NULL && w == 100 && h == 60 && serial > 0);                                   /* a first frame is drawn even while paused */
    if (px) CHECK(px[3] == 255 && px[4 + 1] == 1 && px[(size_t)(5 * 100 + 7) * 4 + 2] == 5);   /* x and y written by the stand-in; the fourth byte made opaque */

    ex_mpv_play(m);
    SDL_Delay(300);
    CHECK(ex_mpv_state(m) == EX_MPV_PLAYING && ex_mpv_position(m) > 0.15);
    s0 = serial;
    {   /* a frame is never torn: every pixel of it has the same red value */
        int torn = 0, got = 0;
        for (i = 0; i < 100; i++) {
            px = ex_mpv_frame(m, &w, &h, &serial);
            if (px && w == 100 && h == 60) {
                int k, r0 = px[0];
                got++;
                for (k = 0; k < w * h; k++) if (px[k * 4] != r0) { torn++; break; }
            }
            SDL_Delay(4);
        }
        CHECK(got > 50 && torn == 0 && serial > s0);                                          /* new frames keep arriving */
    }
    ex_mpv_pause(m);
    SDL_Delay(120);
    {
        double p1 = ex_mpv_position(m);
        unsigned long f1, f2;
        ex_mpv_stats(m, &f1, &slow);
        SDL_Delay(200);
        ex_mpv_stats(m, &f2, &slow);
        CHECK(ex_mpv_state(m) == EX_MPV_PAUSED && ex_mpv_position(m) == p1);                   /* paused: it stands still */
        CHECK(f2 - f1 <= 2);                                                                  /* and draws no new frames */
    }
    ex_mpv_seek(m, 1.5);
    SDL_Delay(150);
    CHECK(ex_mpv_position(m) > 1.45 && ex_mpv_position(m) < 1.6);
    ex_mpv_play(m);
    wait_state(m, EX_MPV_ENDED, 1500);
    CHECK(ex_mpv_state(m) == EX_MPV_ENDED);
    ex_mpv_play(m);                                                                           /* play after the end starts again */
    SDL_Delay(200);
    CHECK(ex_mpv_state(m) == EX_MPV_PLAYING && ex_mpv_position(m) < 1.0);
    ex_mpv_set_volume(m, 0.4f);
    SDL_Delay(60);
    ex_mpv_stats(m, &frames, &slow);
    CHECK(frames > 5 && slow < 100.0);
    ex_mpv_close(m);                                                                          /* joins the thread, frees the handle */

    /* ---- a file that fails to play ---- */
    m = ex_mpv_open("/media/broken.bad", 1, err, sizeof err);
    CHECK(m != NULL);
    if (m) { wait_state(m, EX_MPV_FAILED, 1000); CHECK(ex_mpv_state(m) == EX_MPV_FAILED && ex_mpv_error(m)[0]); ex_mpv_close(m); }

    /* ---- audio only: no picture is drawn ---- */
    m = ex_mpv_open("/media/song.m4a", 0, err, sizeof err);
    CHECK(m != NULL);
    if (m) {
        wait_state(m, EX_MPV_PAUSED, 1000);
        ex_mpv_play(m);
        SDL_Delay(150);
        CHECK(ex_mpv_state(m) == EX_MPV_PLAYING && ex_mpv_position(m) > 0.05);
        ex_mpv_stats(m, &frames, &slow);
        CHECK(frames == 0 && ex_mpv_frame(m, &w, &h, &serial) == NULL);
        ex_mpv_close(m);
    }
    ex_mpv_unload();
    CHECK(!ex_mpv_loaded());
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("mpv_loader: ok\n");
    return 0;
}
