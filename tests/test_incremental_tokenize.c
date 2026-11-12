/* incremental_tokenize: after one-character edits in a note of about 1 MB, updating the tokens takes
 * at least 10 times less time than tokenizing the whole note again. A relative check, so a slow
 * machine does not fail it. It also checks the editor's own path (document edit, token update).
 * Usage: test_incremental_tokenize <sample-note.md> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_editor.h"
#include "exhaust_markdown.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static double seconds(void) { return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency(); }

int main(int argc, char **argv) {
    size_t slen = 0, reps, n = 0, cap, i;
    char *sample, *text;
    ExMd *m = ex_md_create();
    double t0, full, inc, ed_inc, ed_full;
    const int edits = 100, fulls = 8;
    if (argc < 2) { fprintf(stderr, "usage: test_incremental_tokenize SAMPLE\n"); return 2; }
    sample = ex_platform_read_file(argv[1], &slen);
    CHECK(sample != NULL);
    if (!sample) return 1;
    reps = (1000 * 1000) / (slen + 3) + 1;                      /* about 1 MB */
    cap = reps * (slen + 3) + 1024;
    text = malloc(cap);
    for (i = 0; i < reps; i++) { memcpy(text + n, sample, slen); n += slen; memcpy(text + n, "\n\n\n", 3); n += 3; }
    CHECK(n >= 1000 * 1000);

    /* the tokenizer alone */
    t0 = seconds();
    for (i = 0; i < (size_t)fulls; i++) ex_md_tokenize(m, text, n);
    full = (seconds() - t0) / fulls;
    t0 = seconds();
    for (i = 0; i < (size_t)edits; i++) {
        size_t off = n / 3 + i * 53;
        memmove(text + off + 1, text + off, n - off);
        text[off] = (char)('a' + i % 26);
        n++;
        ex_md_update(m, text, n, off, 0, 1);
    }
    inc = (seconds() - t0) / edits;
    fprintf(stderr, "tokenizer: %.2f ms for the whole note, %.3f ms per one-character edit (%.0fx)\n", full * 1000, inc * 1000, full / inc);
    CHECK(inc * 10 <= full);

    /* a fresh tokenization agrees with the last incremental result */
    {
        ExMd *fresh = ex_md_create();
        ex_md_tokenize(fresh, text, n);
        CHECK(ex_md_span_count(fresh) == ex_md_span_count(m) && ex_md_block_count(fresh) == ex_md_block_count(m));
        ex_md_free(fresh);
    }

    /* the editor: type into the document, then ask for the tokens */
    {
        ExDoc *doc = ex_doc_create(text, n);
        ExEditor *ed = ex_editor_create(doc);
        ex_editor_set_mode(ed, EX_EDITOR_LIVE);
        CHECK(ex_editor_tokens(ed) != NULL);                    /* the first call tokenizes everything */
        t0 = seconds();
        for (i = 0; i < (size_t)edits; i++) {
            ex_doc_set_caret(doc, n / 2 + i * 31);
            ex_editor_text_input(ed, "x", 1);
            CHECK(ex_editor_tokens(ed) != NULL);
        }
        ed_inc = (seconds() - t0) / edits;
        t0 = seconds();
        for (i = 0; i < (size_t)fulls; i++) { ex_editor_set_mode(ed, EX_EDITOR_SOURCE); CHECK(ex_editor_tokens(ed) != NULL); }   /* a mode change starts the tokens over */
        ed_full = (seconds() - t0) / fulls;
        fprintf(stderr, "editor: %.2f ms to tokenize the note again, %.3f ms per typed character (%.0fx)\n", ed_full * 1000, ed_inc * 1000, ed_full / ed_inc);
        CHECK(ed_inc * 10 <= ed_full);
        ex_editor_free(ed);
        ex_doc_free(doc);
    }
    ex_md_free(m);
    free(text);
    free(sample);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("incremental_tokenize: ok");
    return 0;
}
