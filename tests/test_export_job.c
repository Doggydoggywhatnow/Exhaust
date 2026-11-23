/* the export job (sub-slice 6e-iv): the export runs on a thread of its own and reports through a queue. A job writes its file and ends with one message
 * that says how many pages; a cancelled job writes nothing and leaves an old file as it was; a file that cannot be written is reported and the old file is
 * kept; the names of the pictures a note embeds are found. The test waits by polling the queue (the interface never does). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_exportjob.h"
#include "exhaust_pdfwrite.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* waits for the end message; counts the progress messages before it */
static int wait_done(ExQueue *q, ExExportMsg *end, int *progress) {
    int i;
    *progress = 0;
    for (i = 0; i < 6000; i++) {
        ExMessage m;
        while (ex_queue_pop(q, &m)) {
            if (m.type == EX_MSG_EXPORT && m.payload) {
                const ExExportMsg *e = m.payload;
                if (e->done) { *end = *e; if (m.free_payload) m.free_payload(m.payload); return 1; }
                (*progress)++;
            }
            if (m.free_payload && m.payload) m.free_payload(m.payload);
        }
        SDL_Delay(5);
    }
    return 0;
}

static ExExportStart start_for(const char *text, size_t len, const char *out, ExQueue *q) {
    ExExportStart st;
    memset(&st, 0, sizeof st);
    st.title = "Job"; st.text = text; st.len = len; st.out_path = out; st.results = q;
    st.opt.page_size = "A4"; st.opt.margin = "default"; st.opt.downscale_percent = 100;
    return st;
}

int main(void) {
    ExQueue *q = ex_queue_create(NULL, NULL);
    const char *dir = "export_job_test";
    char out[300], big_path[300];
    size_t i, cap = 3000 * 90, n = 0, len = 0;
    char *big = malloc(cap), *data;
    ExExportJob *j;
    ExExportMsg end;
    int progress;
    ExPdfCheck chk;
    ex_platform_remove_tree(dir);
    ex_platform_make_dirs(dir);
    snprintf(out, sizeof out, "%s/a.pdf", dir);
    snprintf(big_path, sizeof big_path, "%s/big.pdf", dir);
    for (i = 0; i < 3000; i++) n += (size_t)snprintf(big + n, cap - n, "Line %zu of the note with words enough to be wrapped more than once on a page.\n\n", i);

    {   /* a job writes the file and says so */
        ExExportStart st = start_for("# Hello\n\nA small note.\n", 23, out, q);
        j = ex_exportjob_start(&st);
        CHECK(j != NULL);
        CHECK(wait_done(q, &end, &progress));
        CHECK(end.done && end.ok && end.pages == 1 && strstr(end.message, "Exported 1 page"));
        ex_exportjob_free(j);
        data = ex_platform_read_file(out, &len);
        CHECK(data && len > 500 && !memcmp(data, "%PDF-1.", 7));
        CHECK(ex_pdfw_check((unsigned char *)data, len, &chk) == 0 && chk.pages == 1 && chk.text && strstr(chk.text, "Hello") && strstr(chk.text, "A small note."));
        ex_pdfw_check_free(&chk);
        free(data);
    }
    {   /* a long note reports progress, many pages */
        ExExportStart st = start_for(big, n, big_path, q);
        j = ex_exportjob_start(&st);
        CHECK(j != NULL && wait_done(q, &end, &progress));
        CHECK(end.ok && end.pages > 20);
        ex_exportjob_free(j);
    }
    {   /* a cancelled job writes nothing and keeps the old file */
        size_t before_len = 0, after_len = 0;
        char *before = ex_platform_read_file(out, &before_len), *after;
        ExExportStart st = start_for(big, n, out, q);
        j = ex_exportjob_start(&st);
        CHECK(j != NULL);
        ex_exportjob_cancel(j);
        CHECK(wait_done(q, &end, &progress));
        CHECK(!end.ok && end.cancelled && strstr(end.message, "cancelled"));
        ex_exportjob_free(j);
        after = ex_platform_read_file(out, &after_len);
        CHECK(before && after && before_len == after_len && !memcmp(before, after, before_len));
        free(before); free(after);
    }
    {   /* a file that cannot be written (the folder is not there) is reported; nothing is created */
        char bad[300];
        ExExportStart st;
        ExPathInfo info;
        snprintf(bad, sizeof bad, "%s/no/such/folder/x.pdf", dir);
        st = start_for("text\n", 5, bad, q);
        j = ex_exportjob_start(&st);
        CHECK(j != NULL && wait_done(q, &end, &progress));
        CHECK(!end.ok && !end.cancelled && strstr(end.message, "could not be saved"));
        ex_exportjob_free(j);
        CHECK(ex_platform_path_info(bad, &info) != 0);
    }
    {   /* the pictures a note embeds */
        const char *t = "![[a.png|100]] text ![[b.mp3]] and ![[Note]] ![alt](c%20d.jpg) ![w](https://x.org/e.png) ![[a.png]] ```\n![[in-code.png]]\n```\n";
        char **names;
        size_t k = ex_exportjob_embed_names(t, strlen(t), &names, 50);
        CHECK(k >= 4);
        CHECK(k >= 1 && !strcmp(names[0], "a.png"));
        CHECK(k >= 2 && !strcmp(names[1], "b.mp3"));
        CHECK(k >= 3 && !strcmp(names[2], "c%20d.jpg"));
        { size_t z, bad = 0; for (z = 0; z < k; z++) if (!strcmp(names[z], "Note") || strstr(names[z], "x.org")) bad++; CHECK(bad == 0); }
        ex_exportjob_names_free(names, k);
    }
    free(big);
    ex_queue_destroy(q);
    ex_platform_remove_tree(dir);
    if (failures) { fprintf(stderr, "%d failures\n", failures); return 1; }
    printf("export_job ok\n");
    return 0;
}
