/* A deterministic stand-in for libFuzzer where it is not available (Windows, macOS, gcc): feeds a fuzz target the seed files and
 * then many random mutations of them. Usage: mutate_driver <iterations> <seed-file>... */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint64_t g_state = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { g_state ^= g_state << 13; g_state ^= g_state >> 7; g_state ^= g_state << 17; return (uint32_t)(g_state >> 16); }

int main(int argc, char **argv) {
    const char *save = getenv("MUTATE_SAVE");
    long iters = argc > 1 ? atol(argv[1]) : 1000, i;
    int nseeds = argc - 2, s;
    uint8_t **seed = calloc((size_t)(nseeds > 0 ? nseeds : 1), sizeof *seed);
    size_t *len = calloc((size_t)(nseeds > 0 ? nseeds : 1), sizeof *len);
    static const char *const BITS[] = {"---\n", "\n---\n", ": ", "[", "]", "{", "}", "- ", "#", "\"", "'", "[[", "]]", "\n", "\r\n", "&a ", "*a", "!!str ", "? ", "|\n", ">\n", "%YAML 1.2\n", "...\n", "\t"};
    for (s = 0; s < nseeds; s++) {
        FILE *f = fopen(argv[s + 2], "rb");
        if (!f) { fprintf(stderr, "cannot read %s\n", argv[s + 2]); return 2; }
        seed[s] = malloc(1 << 16);
        len[s] = fread(seed[s], 1, 1 << 16, f);
        fclose(f);
        LLVMFuzzerTestOneInput(seed[s], len[s]);
    }
    if (nseeds < 1) return 2;
    for (i = 0; i < iters; i++) {
        size_t n, k, edits = 1 + rnd() % 6;
        uint8_t *buf;
        s = (int)(rnd() % (uint32_t)nseeds);
        n = len[s];
        buf = malloc(n + 64 * edits + 1);
        memcpy(buf, seed[s], n);
        for (k = 0; k < edits; k++) {
            size_t at = n ? rnd() % n : 0;
            switch (rnd() % 4) {
            case 0: if (n) buf[at] = (uint8_t)rnd(); break;                                           /* a random byte */
            case 1: if (n) { size_t cut = 1 + rnd() % 8; if (at + cut > n) cut = n - at; memmove(buf + at, buf + at + cut, n - at - cut); n -= cut; } break;
            case 2: { const char *b = BITS[rnd() % (sizeof BITS / sizeof BITS[0])]; size_t bl = strlen(b); memmove(buf + at + bl, buf + at, n - at); memcpy(buf + at, b, bl); n += bl; break; }
            default: if (n > 1) { uint8_t chunk[16]; size_t from = rnd() % n, cnt = 1 + rnd() % 16; if (from + cnt > n) cnt = n - from; memcpy(chunk, buf + from, cnt); memmove(buf + at + cnt, buf + at, n - at); memcpy(buf + at, chunk, cnt); n += cnt; } break;
            }
        }
        if (save) { FILE *f = fopen(save, "wb"); if (f) { fwrite(buf, 1, n, f); fclose(f); } }   /* the input that was running if the target aborts */
        LLVMFuzzerTestOneInput(buf, n);
        free(buf);
    }
    for (s = 0; s < nseeds; s++) free(seed[s]);
    free(seed); free(len);
    printf("ok: %ld inputs\n", iters);
    return 0;
}
