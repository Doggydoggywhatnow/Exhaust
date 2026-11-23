/* libmpv at run time (sub-slice 6c-iii): video, and the audio formats the built-in decoders do not take (`.m4a`, `.webm`, `.3gp`).
 *
 * libmpv is LGPL and is never linked: this module opens it with SDL_LoadObject (dlopen, LoadLibrary) the first time it is needed, from the add-on folder
 * `addons` next to the program, then from the system. Only the headers (`third_party/mpv`, ISC) are vendored. When the library is not there every call
 * says so and the interface offers "Open externally" (the file in the system's own program); nothing else fails.
 *
 * A player is one libmpv handle with one thread of its own (`pump`): the thread reads the events and the properties a few times a second into atomics
 * and, for video, draws each new frame with libmpv's software renderer at the size the widget asked for into a buffer; the interface thread takes the
 * latest finished frame with ex_mpv_frame (a swap of two pointers under a short lock) and uploads it as a texture. The interface thread never calls into
 * libmpv. Privacy: only a local file path is given to libmpv; scripts, the user's mpv configuration, youtube-dl and network playlists are switched off. */
#ifndef EXHAUST_MPV_H
#define EXHAUST_MPV_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opens the library. `path` is an explicit file to load (tests; NULL: the environment variable EXHAUST_MPV_LIB, then the add-on folder, then the system's
 * names). 1 when it is loaded (also when it already was), 0 with the reason in err. Safe to call from any thread; the result is kept. */
int ex_mpv_load(const char *path, char *err, size_t errcap);
int ex_mpv_loaded(void);
unsigned long ex_mpv_api_version(void);         /* the library's client API version, 0 when not loaded */
void ex_mpv_unload(void);                       /* the tests */

typedef struct ExMpv ExMpv;
typedef enum ExMpvState { EX_MPV_LOADING = 0, EX_MPV_PLAYING, EX_MPV_PAUSED, EX_MPV_ENDED, EX_MPV_FAILED } ExMpvState;

/* Starts a player on a file (an absolute path), paused. video 0: no picture (audio only). Opening takes a moment: call it on a worker. NULL with the
 * reason in err (library missing, or it refused the file; a file that fails later is state FAILED with ex_mpv_error). */
ExMpv *ex_mpv_open(const char *abs_path, int video, char *err, size_t errcap);
void ex_mpv_close(ExMpv *m);                    /* stops the thread, then frees everything; waits for libmpv */
void ex_mpv_close_async(ExMpv *m);              /* the same on a thread of its own, for the interface thread (closing can take a moment) */
void ex_mpv_play(ExMpv *m);
void ex_mpv_pause(ExMpv *m);
void ex_mpv_toggle(ExMpv *m);
void ex_mpv_seek(ExMpv *m, double seconds);     /* absolute */
void ex_mpv_set_volume(ExMpv *m, float volume); /* 0 to 1 */
ExMpvState ex_mpv_state(const ExMpv *m);
double ex_mpv_position(const ExMpv *m);
double ex_mpv_duration(const ExMpv *m);         /* 0 when not known */
const char *ex_mpv_error(const ExMpv *m);
void ex_mpv_video_size(const ExMpv *m, int *w, int *h);   /* of the file, 0 0 until known or when there is none */

/* The size the picture is drawn at (the widget's), in pixels; at most 4096 on a side. */
void ex_mpv_set_target_size(ExMpv *m, int w, int h);
/* The latest finished frame, RGBA, w by h with 4*w bytes a row, or NULL when there is none; *serial grows with each new frame. Valid until the next call. */
const uint8_t *ex_mpv_frame(ExMpv *m, int *w, int *h, unsigned long *serial);
/* The number of frames the thread has drawn and the longest time one took, for the tests and the frame budget. */
void ex_mpv_stats(const ExMpv *m, unsigned long *frames, double *slowest_ms);

#ifdef __cplusplus
}
#endif
#endif
