/* libFuzzer target for the picture decoders (6c-i): any bytes in. Sniffing, reading the size and decoding (PNG, JPEG, GIF, BMP, WebP, AVIF, SVG) must not
 * crash, read past the data or allocate without a limit; what decodes has a size inside the limits and as many pixels as it says. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "exhaust_image.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    ExImage im;
    char err[200];
    int w, h;
    (void)ex_image_sniff(data, size);
    (void)ex_image_info(data, size, &w, &h, err, sizeof err);
    if (ex_image_decode(data, size, 64, 48, &im, err, sizeof err) == 0) {
        if (im.w < 1 || im.h < 1 || im.w > 64 || im.h > 64 || !im.rgba) abort();
        ex_image_free(&im);
    }
    return 0;
}
