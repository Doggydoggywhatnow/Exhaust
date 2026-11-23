/* libFuzzer target for the audio decoders (6c-ii): any bytes in. Opening, reading and seeking (WAV, FLAC, MP3, Ogg Vorbis) must not crash or read past the
 * data. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_audio.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    uint8_t *copy = malloc(size ? size : 1);
    ExAudioDecoder *d;
    int16_t buf[256 * 2];
    int rounds = 0;
    char err[100];
    if (!copy) return 0;
    memcpy(copy, data, size);
    (void)ex_audio_sniff(data, size);
    d = ex_audio_decoder_open(copy, size, err, sizeof err);
    if (!d) return 0;
    while (ex_audio_decoder_fill(d, buf, 256) > 0 && rounds++ < 64) {}
    ex_audio_decoder_seek(d, size % 977);
    (void)ex_audio_decoder_fill(d, buf, 256);
    ex_audio_decoder_seek(d, 0);
    (void)ex_audio_decoder_fill(d, buf, 256);
    ex_audio_decoder_close(d);
    return 0;
}
