/* audio (6c-ii): every sample file decodes to the expected rate, length and samples (exact for the lossless and PCM ones, close for the lossy and companded
 * ones), seeking lands on the right frame, damaged and hostile input is an error and never a crash; the player, on SDL's dummy output driver, plays,
 * pauses, seeks, ends, changes volume and switches files, and its audio callback keeps being called without a stall.
 * Usage: test_audio <folder with the sample files> */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_audio.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static const char *DIR;
#define FRAMES 6400
#define RATE 16000

static uint8_t *load(const char *name, size_t *len) {
    char path[600];
    uint8_t *d;
    snprintf(path, sizeof path, "%s/%s", DIR, name);
    d = (uint8_t *)ex_platform_read_file(path, len);
    if (!d) fprintf(stderr, "cannot read %s\n", path);
    return d;
}

/* all the frames of a file, as stereo int16 (malloc'ed); *n is how many */
static int16_t *decode_all(const char *name, size_t *n, ExAudioFormat want_fmt, unsigned want_ch, uint64_t *total_out, unsigned *rate_out) {
    size_t len = 0, cap = FRAMES * 2 + 8192, got;
    uint8_t *bytes = load(name, &len);
    ExAudioDecoder *d;
    int16_t *out;
    char err[200] = "";
    *n = 0;
    if (!bytes) { failures++; return NULL; }
    d = ex_audio_decoder_open(bytes, len, err, sizeof err);
    if (!d) { fprintf(stderr, "%s: %s\n", name, err); failures++; return NULL; }
    CHECK(ex_audio_decoder_format(d) == want_fmt);
    CHECK(ex_audio_decoder_rate(d) == RATE);
    CHECK(ex_audio_decoder_channels(d) == want_ch);
    if (total_out) *total_out = ex_audio_decoder_total(d);
    if (rate_out) *rate_out = ex_audio_decoder_rate(d);
    out = malloc(cap * 4);
    while ((got = ex_audio_decoder_fill(d, out + *n * 2, 700)) > 0 && *n + got < cap) *n += got;
    CHECK(ex_audio_decoder_position(d) == *n);
    ex_audio_decoder_close(d);
    return out;
}

static double rms(const int16_t *a, size_t n) { double s = 0; size_t i; for (i = 0; i < n * 2; i++) s += (double)a[i] * a[i]; return sqrt(s / (double)(n * 2)); }

static double mean_diff(const int16_t *a, const int16_t *b, size_t n) { double s = 0; size_t i; for (i = 0; i < n * 2; i++) s += fabs((double)a[i] - b[i]); return s / (double)(n * 2); }

int main(int argc, char **argv) {
    size_t rawlen = 0, n, len;
    int16_t *raw, *got;
    uint64_t total = 0;
    unsigned rate = 0;
    uint8_t *bytes;
    int i;
    char err[200];
    if (argc < 2) return 2;
    DIR = argv[1];
    raw = (int16_t *)load("tone.raw", &rawlen);
    if (!raw || rawlen != FRAMES * 4) { fprintf(stderr, "tone.raw is wrong\n"); return 1; }

    /* ---- the exact ones ---- */
    got = decode_all("tone16.wav", &n, EX_AUDIO_WAV, 2, &total, &rate);
    CHECK(got && n == FRAMES && total == FRAMES && memcmp(got, raw, rawlen) == 0);
    free(got);
    got = decode_all("tone.flac", &n, EX_AUDIO_FLAC, 2, &total, &rate);
    CHECK(got && n == FRAMES && total == FRAMES && memcmp(got, raw, rawlen) == 0);
    free(got);
    got = decode_all("tone24.wav", &n, EX_AUDIO_WAV, 2, &total, &rate);          /* 24 bits made from 16: the low byte is zero, so truncating to 16 bits gives them back */
    CHECK(got && n == FRAMES && memcmp(got, raw, rawlen) == 0);
    free(got);

    /* ---- the close ones ---- */
    got = decode_all("tone_alaw.wav", &n, EX_AUDIO_WAV, 2, &total, &rate);
    CHECK(got && n == FRAMES && mean_diff(got, raw, FRAMES) < 400.0);            /* A-law has about 8 bits of precision */
    free(got);
    got = decode_all("tone_ima.wav", &n, EX_AUDIO_WAV, 2, &total, &rate);
    CHECK(got && n >= FRAMES - 600 && n <= FRAMES + 600);
    if (got && n >= FRAMES) CHECK(rms(got, FRAMES) > rms(raw, FRAMES) * 0.8 && rms(got, FRAMES) < rms(raw, FRAMES) * 1.2);
    free(got);
    got = decode_all("tone.mp3", &n, EX_AUDIO_MP3, 2, &total, &rate);
    CHECK(got && n > FRAMES - 3000 && n < FRAMES + 3000 && total == n);          /* the encoder's delay and padding change the length a little */
    if (got && n > 3200) CHECK(rms(got + 2000 * 2, 2000) > rms(raw, FRAMES) * 0.7 && rms(got + 2000 * 2, 2000) < rms(raw, FRAMES) * 1.3);
    free(got);
    got = decode_all("tone.ogg", &n, EX_AUDIO_OGG, 2, &total, &rate);
    CHECK(got && n > FRAMES - 1500 && n < FRAMES + 1500);
    if (got && n > FRAMES) CHECK(rms(got, FRAMES) > rms(raw, FRAMES) * 0.8 && rms(got, FRAMES) < rms(raw, FRAMES) * 1.2);
    free(got);

    /* ---- mono is made stereo (both sides the same) ---- */
    got = decode_all("mono.wav", &n, EX_AUDIO_WAV, 1, &total, &rate);
    CHECK(got && n == FRAMES);
    if (got) { size_t k, same = 0; for (k = 0; k < n; k++) if (got[k * 2] == got[k * 2 + 1] && got[k * 2] == raw[k * 2]) same++; CHECK(same == n); }
    free(got);

    /* ---- seeking lands on the frame (the exact formats) ---- */
    {
        static const char *names[] = { "tone16.wav", "tone.flac" };
        int k;
        for (k = 0; k < 2; k++) {
            ExAudioDecoder *d;
            int16_t buf[200 * 2];
            bytes = load(names[k], &len);
            d = bytes ? ex_audio_decoder_open(bytes, len, err, sizeof err) : NULL;
            CHECK(d != NULL);
            if (!d) continue;
            ex_audio_decoder_seek(d, 3200);
            CHECK(ex_audio_decoder_position(d) == 3200);
            CHECK(ex_audio_decoder_fill(d, buf, 200) == 200 && memcmp(buf, raw + 3200 * 2, sizeof buf) == 0);
            ex_audio_decoder_seek(d, 999999);                                      /* past the end: the end */
            CHECK(ex_audio_decoder_fill(d, buf, 200) == 0);
            ex_audio_decoder_seek(d, 0);
            CHECK(ex_audio_decoder_fill(d, buf, 200) == 200 && memcmp(buf, raw, sizeof buf) == 0);
            ex_audio_decoder_close(d);
        }
    }

    /* ---- not audio, and damaged audio: an error, never a crash ---- */
    CHECK(ex_audio_sniff("hello, this is text", 19) == EX_AUDIO_UNKNOWN);
    {
        uint8_t *junk = malloc(20);
        memcpy(junk, "hello, this is text", 20);
        CHECK(ex_audio_decoder_open(junk, 20, err, sizeof err) == NULL && err[0]);
        CHECK(ex_audio_decoder_open(NULL, 0, err, sizeof err) == NULL);
    }
    {
        static const char *names[] = { "tone16.wav", "tone.flac", "tone24.wav", "tone.mp3", "tone.ogg", "tone_alaw.wav", "tone_ima.wav", "mono.wav" };
        size_t k;
        for (k = 0; k < sizeof names / sizeof *names; k++) {
            size_t fl = 0, cut;
            uint8_t *f = load(names[k], &fl);
            if (!f) { failures++; continue; }
            for (cut = 0; cut < fl; cut += fl / 30 + 1) {                                   /* cut short at many places */
                uint8_t *c = malloc(cut ? cut : 1);
                ExAudioDecoder *d;
                memcpy(c, f, cut);
                d = ex_audio_decoder_open(c, cut, err, sizeof err);
                if (d) { int16_t buf[500 * 2]; int rounds = 0; while (ex_audio_decoder_fill(d, buf, 500) > 0 && rounds++ < 100) {} ex_audio_decoder_close(d); }
            }
            for (i = 0; i < 120; i++) {                                                      /* and with bytes changed */
                uint8_t *c = malloc(fl);
                ExAudioDecoder *d;
                memcpy(c, f, fl);
                c[(size_t)(i * 7919) % fl] ^= (uint8_t)(1 + i % 255);
                c[(size_t)(i * 104729 + 13) % fl] = (uint8_t)(i * 31);
                d = ex_audio_decoder_open(c, fl, err, sizeof err);
                if (d) { int16_t buf[500 * 2]; int rounds = 0; while (ex_audio_decoder_fill(d, buf, 500) > 0 && rounds++ < 200) {} ex_audio_decoder_seek(d, 100); ex_audio_decoder_fill(d, buf, 500); ex_audio_decoder_close(d); }
            }
            free(f);
        }
    }

    /* ---- the player, on SDL's dummy driver ---- */
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    {
        ExPlayer *p = ex_player_create();
        ExAudioDecoder *d;
        unsigned long cb0, cb1;
        uint64_t fr;
        double pos1, pos2;
        CHECK(p != NULL && ex_player_state(p) == EX_PLAYER_IDLE && !ex_player_has_device(p));
        bytes = load("tone16.wav", &len);
        d = ex_audio_decoder_open(bytes, len, err, sizeof err);
        CHECK(d && ex_player_attach(p, d) != 0);                                        /* no device yet: refused, the decoder is closed */
        CHECK(ex_player_prepare(p, err, sizeof err) == 0 && ex_player_has_device(p));
        if (!ex_player_has_device(p)) { fprintf(stderr, "no audio device: %s\n", err); free(raw); return 1; }
        bytes = load("tone16.wav", &len);
        d = ex_audio_decoder_open(bytes, len, err, sizeof err);
        CHECK(d && ex_player_attach(p, d) == 0);
        CHECK(ex_player_state(p) == EX_PLAYER_PAUSED && ex_player_position(p) == 0.0 && ex_player_duration(p) > 0.39 && ex_player_duration(p) < 0.41);
        SDL_Delay(150);
        ex_player_stats(p, &cb0, &fr);
        CHECK(cb0 > 0 && fr == 0);                                                      /* the device runs, and a paused player decodes nothing */
        ex_player_play(p);
        SDL_Delay(120);
        pos1 = ex_player_position(p);
        CHECK(pos1 > 0.0 && ex_player_state(p) == EX_PLAYER_PLAYING);
        ex_player_pause(p);
        SDL_Delay(150);
        pos2 = ex_player_position(p);
        SDL_Delay(200);
        CHECK(ex_player_state(p) == EX_PLAYER_PAUSED && ex_player_position(p) == pos2);  /* paused: the position stands still */
        ex_player_seek(p, 0.3);
        SDL_Delay(120);
        CHECK(ex_player_position(p) >= 0.29 && ex_player_position(p) <= 0.32);
        ex_player_play(p);
        SDL_Delay(700);                                                                  /* 0.1 s are left: it ends */
        CHECK(ex_player_state(p) == EX_PLAYER_ENDED);
        ex_player_play(p);                                                               /* play after the end starts again */
        SDL_Delay(100);
        CHECK(ex_player_position(p) > 0.0 && ex_player_position(p) < 0.35);
        ex_player_stats(p, &cb1, &fr);
        CHECK(cb1 > cb0 && fr > 0);
        ex_player_set_volume(p, 0.25f);
        CHECK(ex_player_volume(p) == 0.25f);
        ex_player_set_volume(p, 7.0f);
        CHECK(ex_player_volume(p) == 1.0f);
        {   /* another file replaces it: a different rate, paused at the start */
            ExAudioDecoder *d2;
            unsigned long serial = ex_player_serial(p);
            bytes = load("tone.flac", &len);
            d2 = ex_audio_decoder_open(bytes, len, err, sizeof err);
            CHECK(d2 && ex_player_attach(p, d2) == 0 && ex_player_serial(p) == serial + 1);
            CHECK(ex_player_state(p) == EX_PLAYER_PAUSED && ex_player_position(p) == 0.0);
        }
        ex_player_detach(p);
        CHECK(ex_player_state(p) == EX_PLAYER_IDLE && ex_player_duration(p) == 0.0);
        ex_player_free(p);
    }
    free(raw);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("audio: ok\n");
    return 0;
}
