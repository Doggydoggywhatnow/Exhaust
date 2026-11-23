/* Audio decoders and the player: see include/exhaust_audio.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "dr_mp3.h"

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

#include "peep.h"
#include "exhaust_audio.h"

#define CHUNK 1024                       /* frames decoded at a time, on the callback's stack */

struct ExAudioDecoder {
    ExAudioFormat fmt;
    uint8_t *bytes; size_t len;
    unsigned rate, channels, bits;
    uint64_t total, pos;
    peep p; void *work; int peep_open_ok;
    drmp3 mp3; int mp3_open;
    stb_vorbis *vb;
    int16_t scratch[CHUNK * 2];          /* a mono or odd-channel MP3 read, made stereo */
};

static void seterr(char *err, size_t cap, const char *msg) { if (err && cap) snprintf(err, cap, "%s", msg); }

const char *ex_audio_format_name(ExAudioFormat f) {
    static const char *n[] = { "unknown", "WAV", "FLAC", "MP3", "Ogg Vorbis" };
    return (unsigned)f < sizeof n / sizeof *n ? n[f] : n[0];
}

ExAudioFormat ex_audio_sniff(const void *data, size_t len) {
    const uint8_t *p = data;
    if (len >= 12 && !memcmp(p, "RIFF", 4) && !memcmp(p + 8, "WAVE", 4)) return EX_AUDIO_WAV;
    if (len >= 4 && !memcmp(p, "fLaC", 4)) return EX_AUDIO_FLAC;
    if (len >= 4 && !memcmp(p, "OggS", 4)) return EX_AUDIO_OGG;
    if (len >= 3 && !memcmp(p, "ID3", 3)) return EX_AUDIO_MP3;
    if (len >= 4 && p[0] == 0xFF && (p[1] & 0xE0) == 0xE0 && (p[1] & 0x18) != 0x08 && (p[1] & 0x06) != 0) return EX_AUDIO_MP3;
    return EX_AUDIO_UNKNOWN;
}

static const peep_codec *const PEEP_CODECS[] = { &peep_codec_flac, &peep_codec_wavx, &peep_codec_wavc, NULL };

static void decoder_free(ExAudioDecoder *d) {
    if (!d) return;
    if (d->peep_open_ok) peep_close(&d->p);
    if (d->mp3_open) drmp3_uninit(&d->mp3);
    if (d->vb) stb_vorbis_close(d->vb);
    free(d->work);
    free(d->bytes);
    free(d);
}
void ex_audio_decoder_close(ExAudioDecoder *d) { decoder_free(d); }

ExAudioDecoder *ex_audio_decoder_open(uint8_t *bytes, size_t len, char *err, size_t errcap) {
    ExAudioDecoder *d;
    ExAudioFormat f;
    if (!bytes) { seterr(err, errcap, "no data"); return NULL; }
    if (len > EX_AUDIO_MAX_FILE) { free(bytes); seterr(err, errcap, "this audio file is too big (over 256 MB)"); return NULL; }
    d = calloc(1, sizeof *d);
    if (!d) { free(bytes); seterr(err, errcap, "out of memory"); return NULL; }
    d->bytes = bytes; d->len = len;
    f = ex_audio_sniff(bytes, len);
    d->fmt = f;
    if (f == EX_AUDIO_WAV || f == EX_AUDIO_FLAC) {
        size_t work_size = 64 * 1024;
        d->work = malloc(work_size);
        if (!d->work) { decoder_free(d); seterr(err, errcap, "out of memory"); return NULL; }
        memset(&d->p, 0, sizeof d->p);
        if (!peep_open(&d->p, PEEP_CODECS, bytes, len, d->work, work_size) && d->p.work_need > work_size) {      /* a codec asked for a bigger workspace */
            size_t need = d->p.work_need;
            free(d->work);
            if (need > ((size_t)64 << 20)) { d->work = NULL; decoder_free(d); seterr(err, errcap, "this audio file is not supported"); return NULL; }
            d->work = malloc(need);
            if (!d->work) { decoder_free(d); seterr(err, errcap, "out of memory"); return NULL; }
            work_size = need;
            memset(&d->p, 0, sizeof d->p);
            peep_open(&d->p, PEEP_CODECS, bytes, len, d->work, work_size);
        }
        if (d->p.kind == 0 || d->p.rate == 0) { decoder_free(d); seterr(err, errcap, "this audio file is damaged or in a format that is not supported"); return NULL; }
        d->peep_open_ok = 1;
        d->rate = d->p.rate; d->channels = d->p.channels; d->bits = d->p.bits ? d->p.bits : 16; d->total = d->p.total;
        return d;
    }
    if (f == EX_AUDIO_MP3) {
        drmp3_uint64 frames = 0;
        if (!drmp3_init_memory(&d->mp3, bytes, len, NULL)) { decoder_free(d); seterr(err, errcap, "this MP3 file is damaged"); return NULL; }
        d->mp3_open = 1;
        d->rate = d->mp3.sampleRate; d->channels = d->mp3.channels; d->bits = 16;
        if (d->rate == 0 || d->channels == 0) { decoder_free(d); seterr(err, errcap, "this MP3 file is damaged"); return NULL; }
        frames = drmp3_get_pcm_frame_count(&d->mp3);                              /* a scan of the whole file: this is why a decoder is opened on a worker */
        d->total = frames;
        drmp3_seek_to_pcm_frame(&d->mp3, 0);
        return d;
    }
    if (f == EX_AUDIO_OGG) {
        int verr = 0;
        stb_vorbis_info info;
        d->vb = stb_vorbis_open_memory(bytes, (int)(len > 0x7fffffff ? 0x7fffffff : len), &verr, NULL);
        if (!d->vb) { decoder_free(d); seterr(err, errcap, "this Ogg file is damaged or is not Vorbis audio"); return NULL; }
        info = stb_vorbis_get_info(d->vb);
        d->rate = info.sample_rate; d->channels = (unsigned)info.channels; d->bits = 16;
        d->total = stb_vorbis_stream_length_in_samples(d->vb);
        if (d->rate == 0 || d->channels == 0) { decoder_free(d); seterr(err, errcap, "this Ogg file is damaged"); return NULL; }
        return d;
    }
    decoder_free(d);
    seterr(err, errcap, "this is not an audio file Exhaust can play");
    return NULL;
}

ExAudioFormat ex_audio_decoder_format(const ExAudioDecoder *d) { return d ? d->fmt : EX_AUDIO_UNKNOWN; }
unsigned ex_audio_decoder_rate(const ExAudioDecoder *d) { return d ? d->rate : 0; }
unsigned ex_audio_decoder_channels(const ExAudioDecoder *d) { return d ? d->channels : 0; }
unsigned ex_audio_decoder_bits(const ExAudioDecoder *d) { return d ? d->bits : 0; }
uint64_t ex_audio_decoder_total(const ExAudioDecoder *d) { return d ? d->total : 0; }
uint64_t ex_audio_decoder_position(const ExAudioDecoder *d) { return d ? d->pos : 0; }

size_t ex_audio_decoder_fill(ExAudioDecoder *d, int16_t *out, size_t frames) {
    size_t got = 0;
    if (!d || !out || !frames) return 0;
    if (d->peep_open_ok) got = peep_fill(&d->p, out, frames);
    else if (d->mp3_open) {
        size_t want = frames > CHUNK ? CHUNK : frames, i;
        if (d->mp3.channels == 2) got = (size_t)drmp3_read_pcm_frames_s16(&d->mp3, want, out);
        else {
            int16_t tmp[CHUNK * 8];
            unsigned ch = d->mp3.channels > 8 ? 8 : d->mp3.channels;
            if (want > CHUNK) want = CHUNK;
            got = (size_t)drmp3_read_pcm_frames_s16(&d->mp3, want, tmp);
            for (i = 0; i < got; i++) { out[i * 2] = tmp[i * ch]; out[i * 2 + 1] = ch > 1 ? tmp[i * ch + 1] : tmp[i * ch]; }
        }
    } else if (d->vb) {
        size_t want = frames > CHUNK ? CHUNK : frames;
        int n = stb_vorbis_get_samples_short_interleaved(d->vb, 2, out, (int)(want * 2));
        got = n > 0 ? (size_t)n : 0;
    }
    d->pos += got;
    return got;
}

void ex_audio_decoder_seek(ExAudioDecoder *d, uint64_t frame) {
    if (!d) return;
    if (d->total && frame > d->total) frame = d->total;
    if (d->peep_open_ok) peep_seek(&d->p, frame);
    else if (d->mp3_open) drmp3_seek_to_pcm_frame(&d->mp3, frame);
    else if (d->vb) stb_vorbis_seek(d->vb, (unsigned)frame);
    d->pos = frame;
}

/* ---- the player ---- */

struct ExPlayer {
    SDL_AudioStream *stream;
    ExAudioDecoder *dec;                    /* changed only with the stream locked */
    SDL_AtomicInt state;                    /* ExPlayerState */
    SDL_AtomicInt seek_to;                  /* a frame, or -1 */
    SDL_AtomicInt pos_lo, pos_hi;           /* the position in frames, as two 32-bit halves (written by the callback) */
    SDL_AtomicInt callbacks;
    SDL_AtomicInt frames_lo;
    unsigned rate; uint64_t total;
    float volume;
    char path[600];
    char info[96];
    unsigned long serial;
    int init_audio;
};

static void store_pos(ExPlayer *p, uint64_t v) { SDL_SetAtomicInt(&p->pos_lo, (int)(uint32_t)(v & 0xffffffffu)); SDL_SetAtomicInt(&p->pos_hi, (int)(uint32_t)(v >> 32)); }
static uint64_t load_pos(const ExPlayer *p) {
    int hi1, lo, hi2;
    do { hi1 = SDL_GetAtomicInt((SDL_AtomicInt *)&p->pos_hi); lo = SDL_GetAtomicInt((SDL_AtomicInt *)&p->pos_lo); hi2 = SDL_GetAtomicInt((SDL_AtomicInt *)&p->pos_hi); } while (hi1 != hi2);
    return ((uint64_t)(uint32_t)hi1 << 32) | (uint32_t)lo;
}

/* The audio thread. Allocates nothing, takes no lock of ours and reads no file. */
static void SDLCALL audio_cb(void *user, SDL_AudioStream *stream, int additional, int total) {
    ExPlayer *p = user;
    int16_t buf[CHUNK * 2];
    (void)total;
    SDL_AddAtomicInt(&p->callbacks, 1);
    while (additional > 0) {
        int chunk_frames = additional / 4 > CHUNK ? CHUNK : additional / 4;
        size_t got = 0;
        int st = SDL_GetAtomicInt(&p->state);
        if (chunk_frames < 1) chunk_frames = 1;
        if (p->dec) {
            int sk = SDL_GetAtomicInt(&p->seek_to);
            if (sk >= 0) { SDL_SetAtomicInt(&p->seek_to, -1); ex_audio_decoder_seek(p->dec, (uint64_t)sk); store_pos(p, ex_audio_decoder_position(p->dec)); }
        }
        if (st == EX_PLAYER_PLAYING && p->dec) {
            got = ex_audio_decoder_fill(p->dec, buf, (size_t)chunk_frames);
            if (got == 0) SDL_SetAtomicInt(&p->state, EX_PLAYER_ENDED);
            store_pos(p, ex_audio_decoder_position(p->dec));
            SDL_AddAtomicInt(&p->frames_lo, (int)got);
        }
        if (got < (size_t)chunk_frames) memset(buf + got * 2, 0, ((size_t)chunk_frames - got) * 4);          /* paused, ended or a short read: silence keeps the device fed */
        SDL_PutAudioStreamData(stream, buf, chunk_frames * 4);
        additional -= chunk_frames * 4;
    }
}

ExPlayer *ex_player_create(void) {
    ExPlayer *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    SDL_SetAtomicInt(&p->state, EX_PLAYER_IDLE); SDL_SetAtomicInt(&p->seek_to, -1);
    p->volume = 1.0f;
    return p;
}

void ex_player_free(ExPlayer *p) {
    if (!p) return;
    if (p->stream) SDL_DestroyAudioStream(p->stream);            /* waits for the callback to finish */
    ex_audio_decoder_close(p->dec);
    if (p->init_audio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    free(p);
}

int ex_player_prepare(ExPlayer *p, char *err, size_t errcap) {
    SDL_AudioSpec spec;
    if (!p) return -1;
    if (p->stream) return 0;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { seterr(err, errcap, SDL_GetError()); return -1; }
    p->init_audio = 1;
    spec.format = SDL_AUDIO_S16; spec.channels = 2; spec.freq = 44100;
    p->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_cb, p);
    if (!p->stream) { seterr(err, errcap, *SDL_GetError() ? SDL_GetError() : "there is no audio device"); return -1; }
    SDL_SetAudioStreamGain(p->stream, p->volume);
    SDL_ResumeAudioStreamDevice(p->stream);                      /* the device runs; the callback gives silence until something plays */
    return 0;
}

int ex_player_has_device(const ExPlayer *p) { return p && p->stream != NULL; }

int ex_player_attach(ExPlayer *p, ExAudioDecoder *d) {
    SDL_AudioSpec src;
    ExAudioDecoder *old;
    if (!p || !d) { ex_audio_decoder_close(d); return -1; }
    if (!p->stream) { ex_audio_decoder_close(d); return -1; }
    src.format = SDL_AUDIO_S16; src.channels = 2; src.freq = (int)ex_audio_decoder_rate(d);
    SDL_LockAudioStream(p->stream);
    old = p->dec;
    SDL_SetAtomicInt(&p->state, EX_PLAYER_PAUSED);
    SDL_SetAtomicInt(&p->seek_to, -1);
    SDL_SetAudioStreamFormat(p->stream, &src, NULL);
    SDL_ClearAudioStream(p->stream);
    p->dec = d; p->rate = ex_audio_decoder_rate(d); p->total = ex_audio_decoder_total(d);
    snprintf(p->info, sizeof p->info, "%s, %u Hz, %u channel%s%s", ex_audio_format_name(ex_audio_decoder_format(d)), p->rate, ex_audio_decoder_channels(d), ex_audio_decoder_channels(d) == 1 ? "" : "s", ex_audio_decoder_bits(d) ? ", " : "");
    if (ex_audio_decoder_bits(d)) { size_t n = strlen(p->info); snprintf(p->info + n, sizeof p->info - n, "%u bit", ex_audio_decoder_bits(d)); }
    store_pos(p, 0);
    p->serial++;
    SDL_UnlockAudioStream(p->stream);
    ex_audio_decoder_close(old);
    return 0;
}

void ex_player_detach(ExPlayer *p) {
    ExAudioDecoder *old;
    if (!p) return;
    if (p->stream) SDL_LockAudioStream(p->stream);
    old = p->dec; p->dec = NULL; p->total = 0; p->path[0] = 0;
    SDL_SetAtomicInt(&p->state, EX_PLAYER_IDLE);
    store_pos(p, 0);
    if (p->stream) SDL_UnlockAudioStream(p->stream);
    ex_audio_decoder_close(old);
}

void ex_player_play(ExPlayer *p) {
    if (!p || !p->dec) return;
    if (SDL_GetAtomicInt(&p->state) == EX_PLAYER_ENDED) { SDL_SetAtomicInt(&p->seek_to, 0); store_pos(p, 0); }
    SDL_SetAtomicInt(&p->state, EX_PLAYER_PLAYING);
}
void ex_player_pause(ExPlayer *p) { if (p && p->dec && SDL_GetAtomicInt(&p->state) == EX_PLAYER_PLAYING) SDL_SetAtomicInt(&p->state, EX_PLAYER_PAUSED); }
void ex_player_toggle(ExPlayer *p) { if (!p) return; if (SDL_GetAtomicInt(&p->state) == EX_PLAYER_PLAYING) ex_player_pause(p); else ex_player_play(p); }

void ex_player_seek(ExPlayer *p, double seconds) {
    uint64_t f;
    if (!p || !p->dec || p->rate == 0) return;
    if (seconds < 0) seconds = 0;
    f = (uint64_t)(seconds * (double)p->rate);
    if (p->total && f > p->total) f = p->total;
    if (f > 0x7ffffffeu) f = 0x7ffffffeu;
    store_pos(p, f);
    SDL_SetAtomicInt(&p->seek_to, (int)f);
    if (SDL_GetAtomicInt(&p->state) == EX_PLAYER_ENDED) SDL_SetAtomicInt(&p->state, EX_PLAYER_PAUSED);
}

void ex_player_set_volume(ExPlayer *p, float v) {
    if (!p) return;
    p->volume = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    if (p->stream) SDL_SetAudioStreamGain(p->stream, p->volume);
}
float ex_player_volume(const ExPlayer *p) { return p ? p->volume : 0.0f; }
ExPlayerState ex_player_state(const ExPlayer *p) { return p ? (ExPlayerState)SDL_GetAtomicInt((SDL_AtomicInt *)&p->state) : EX_PLAYER_IDLE; }
double ex_player_position(const ExPlayer *p) { return p && p->rate ? (double)load_pos(p) / (double)p->rate : 0.0; }
double ex_player_duration(const ExPlayer *p) { return p && p->rate ? (double)p->total / (double)p->rate : 0.0; }
const char *ex_player_path(const ExPlayer *p) { return p ? p->path : ""; }
const char *ex_player_info(const ExPlayer *p) { return p ? p->info : ""; }
void ex_player_set_path(ExPlayer *p, const char *path) { if (p) snprintf(p->path, sizeof p->path, "%s", path ? path : ""); }
unsigned long ex_player_serial(const ExPlayer *p) { return p ? p->serial : 0; }
void ex_player_stats(const ExPlayer *p, unsigned long *callbacks, uint64_t *frames) { if (callbacks) *callbacks = p ? (unsigned long)SDL_GetAtomicInt((SDL_AtomicInt *)&p->callbacks) : 0; if (frames) *frames = p ? (uint64_t)(uint32_t)SDL_GetAtomicInt((SDL_AtomicInt *)&p->frames_lo) : 0; }
