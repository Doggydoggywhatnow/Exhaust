/* Audio (sub-slice 6c-ii): decoders and the player.
 *
 * Decoders. WAV (16-bit PCM built in; 8, 12, 20, 24, 32-bit and float; A-law, mu-law, IMA and Microsoft ADPCM) and FLAC (4 to 31 bits) are decoded by
 * lib-Peep (hand-written assembly, no libc, no malloc, no locks), MP3 by dr_mp3 and Ogg Vorbis by stb_vorbis. Every decoder hands out interleaved stereo
 * 16-bit samples at the file's own rate (mono is duplicated, more than two channels use the first two); `fill` allocates nothing and takes no lock, so it can
 * run in an audio callback. A decoder reads a file that is in memory (the decoder owns the bytes) and is opened on a worker, not on the UI thread: opening
 * an MP3 counts its frames.
 *
 * The player. One output stream (SDL3), opened lazily on the worker with `prepare`. The audio callback pulls from the attached decoder: it reads three
 * atomics (the state, a seek request and the volume lives in the stream) and writes one (the position), and decodes into a buffer on its own stack, so it
 * takes no lock of ours, allocates nothing and never touches the disk. Attaching or detaching a decoder locks the stream for an instant (SDL does not run
 * its callback meanwhile). Nothing here writes a file. */
#ifndef EXHAUST_AUDIO_H
#define EXHAUST_AUDIO_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExAudioFormat { EX_AUDIO_UNKNOWN = 0, EX_AUDIO_WAV, EX_AUDIO_FLAC, EX_AUDIO_MP3, EX_AUDIO_OGG } ExAudioFormat;
const char *ex_audio_format_name(ExAudioFormat f);
/* The format from the first bytes (MP3: a frame header or an ID3 tag). */
ExAudioFormat ex_audio_sniff(const void *data, size_t len);

typedef struct ExAudioDecoder ExAudioDecoder;
#define EX_AUDIO_MAX_FILE ((size_t)256 << 20)

/* Opens a decoder over `bytes` (malloc'ed; the decoder owns them and frees them on close, also when the open fails). NULL with the reason in err. */
ExAudioDecoder *ex_audio_decoder_open(uint8_t *bytes, size_t len, char *err, size_t errcap);
ExAudioFormat ex_audio_decoder_format(const ExAudioDecoder *d);
unsigned ex_audio_decoder_rate(const ExAudioDecoder *d);
unsigned ex_audio_decoder_channels(const ExAudioDecoder *d);       /* of the file */
unsigned ex_audio_decoder_bits(const ExAudioDecoder *d);           /* per sample of the file (0: not known) */
uint64_t ex_audio_decoder_total(const ExAudioDecoder *d);          /* frames, 0 when not known */
uint64_t ex_audio_decoder_position(const ExAudioDecoder *d);       /* frames read so far */
/* Up to `frames` stereo frames (4 bytes each) into out; 0 at the end. A short count is not the end. No allocation, no lock. */
size_t ex_audio_decoder_fill(ExAudioDecoder *d, int16_t *out, size_t frames);
void ex_audio_decoder_seek(ExAudioDecoder *d, uint64_t frame);     /* clamps to the end */
void ex_audio_decoder_close(ExAudioDecoder *d);

typedef struct ExPlayer ExPlayer;
typedef enum ExPlayerState { EX_PLAYER_IDLE = 0, EX_PLAYER_PLAYING, EX_PLAYER_PAUSED, EX_PLAYER_ENDED } ExPlayerState;

ExPlayer *ex_player_create(void);
void ex_player_free(ExPlayer *p);
/* Opens the output device (this can take a while: call it on a worker thread). 0, or -1 with the reason in err. */
int ex_player_prepare(ExPlayer *p, char *err, size_t errcap);
/* Takes the decoder (the previous one is closed) and starts it paused at the beginning. 0, or -1 when the device is not open. */
int ex_player_attach(ExPlayer *p, ExAudioDecoder *d);
void ex_player_detach(ExPlayer *p);
void ex_player_play(ExPlayer *p);
void ex_player_pause(ExPlayer *p);
void ex_player_toggle(ExPlayer *p);
void ex_player_seek(ExPlayer *p, double seconds);
void ex_player_set_volume(ExPlayer *p, float volume);              /* 0 to 1 */
float ex_player_volume(const ExPlayer *p);
ExPlayerState ex_player_state(const ExPlayer *p);
double ex_player_position(const ExPlayer *p);                      /* seconds */
double ex_player_duration(const ExPlayer *p);                      /* seconds, 0 when not known */
const char *ex_player_path(const ExPlayer *p);
const char *ex_player_info(const ExPlayer *p);                     /* "FLAC, 44100 Hz, 2 channels, 16 bit" for the file attached */
void ex_player_set_path(ExPlayer *p, const char *path);            /* what is attached, for the display */
unsigned long ex_player_serial(const ExPlayer *p);                 /* grows each time a decoder is attached */
int ex_player_has_device(const ExPlayer *p);
/* For the tests: counts of the audio callback's calls and of the frames it decoded. */
void ex_player_stats(const ExPlayer *p, unsigned long *callbacks, uint64_t *frames);

#ifdef __cplusplus
}
#endif
#endif
