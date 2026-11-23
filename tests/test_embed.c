/* embeds (6c-v): the notes a note embeds are shown in place (a whole note, a heading, a block; not inside a code fence; not again inside the embedded text;
 * not for files that are not notes), and the reading view draws embedded media: a picture at the size written (`|640x480`, `|100`), cards for audio,
 * video, PDFs and canvases whose click hands "path#fragment" to the link handler, a placeholder for an external address (nothing is fetched), and plain text
 * for a name that does not resolve. Also the parsing of `name|size#fragment`. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_embed.h"
#include "exhaust_platform.h"
#include "reading_view.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define V "embed_vault"
#define DB "embed_db"

static void write_file(const char *rel, const char *content) {
    char path[512], *slash;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", V, rel);
    slash = strrchr(path, '/');
    if (slash) { *slash = 0; ex_platform_make_dirs(path); *slash = '/'; }
    f = fopen(path, "wb");
    if (f) { fwrite(content, 1, strlen(content), f); fclose(f); }
}

static char link_target[400]; static int link_clicks;
static void on_link(ExRSpanKind kind, const char *target, void *u) { (void)u; if (kind == EX_RSK_EMBED) { link_clicks++; snprintf(link_target, sizeof link_target, "%s", target); } }
static int resolve(void *u, const char *name, char *path, size_t cap) {
    static const char *const files[] = { "photo.png", "Media/song.flac", "clip.mp4", "doc.pdf", "board.canvas" };
    size_t i;
    (void)u;
    for (i = 0; i < sizeof files / sizeof *files; i++) {
        const char *slash = strrchr(files[i], '/');
        if (!strcmp(files[i], name) || !strcmp(slash ? slash + 1 : files[i], name)) { snprintf(path, cap, "%s", files[i]); return 1; }
    }
    return 0;
}
static int audio_state(void *u, const char *p) { (void)u; (void)p; return 0; }

int main(void) {
    ExIndex *w;
    char err[200], *root, *out;
    size_t olen = 0;
    char name[200], size[40], frag[200];
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");

    /* ---- the parts of an embed ---- */
    ex_embed_split("photo.png|640x480", name, sizeof name, size, sizeof size, frag, sizeof frag);
    CHECK(!strcmp(name, "photo.png") && !strcmp(size, "640x480") && !frag[0]);
    ex_embed_split("doc.pdf#page=3&height=400", name, sizeof name, size, sizeof size, frag, sizeof frag);
    CHECK(!strcmp(name, "doc.pdf") && !size[0] && !strcmp(frag, "page=3&height=400"));
    ex_embed_split("a b.png|100", name, sizeof name, NULL, 0, NULL, 0);
    CHECK(!strcmp(name, "a b.png"));
    CHECK(ex_embed_media_kind("x.PNG") == 1 && ex_embed_media_kind("x.mp3") == 2 && ex_embed_media_kind("x.mkv") == 3 && ex_embed_media_kind("x.pdf") == 4 && ex_embed_media_kind("x.canvas") == 5 && ex_embed_media_kind("x.md") == 0 && ex_embed_media_kind("Note") == 0);

    /* ---- notes embedded in notes ---- */
    ex_platform_remove_tree(V); ex_platform_remove_tree(DB);
    write_file("Source.md", "# Source\n\nFirst paragraph of the source.\n\n## Part\nThe part text. ^blk1\n\nTail line.\n");
    write_file("Loop.md", "# Loop\n![[Loop]]\nafter\n");
    write_file("Host.md", "Intro\n\n![[Source]]\n\nMiddle\n\n![[Source#Part]]\n\n```\n![[Source]]\n```\n\n![[photo.png]]\n![[Nothing here]]\n![[Loop]]\n");
    ex_platform_make_dirs(DB);
    root = ex_platform_realpath(V);
    w = ex_index_open(DB "/index.db", 1, err, sizeof err);
    CHECK(w != NULL);
    if (!w || ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL) != 0) { fprintf(stderr, "cannot index\n"); return 1; }
    {
        size_t tl = 0;
        char *text = (char *)ex_platform_read_file(V "/Host.md", &tl);
        out = ex_embed_expand(w, root, "Host.md", text, tl, &olen);
        CHECK(out != NULL);
        if (out && getenv("EMBED_DEBUG")) fprintf(stderr, "---\n%s\n---\n", out);
        if (out) {
            CHECK(strstr(out, "> *Source*") != NULL && strstr(out, "> First paragraph of the source.") != NULL);          /* the whole note, quoted under its name */
            CHECK(strstr(out, "> *Source > Part*") != NULL && strstr(out, "> The part text.") != NULL);                    /* a heading */
            CHECK(strstr(out, "```\n![[Source]]\n```") != NULL);                                                            /* in a fence: left alone */
            CHECK(strstr(out, "![[photo.png]]") != NULL);                                                                  /* not a note: left for the reading view */
            CHECK(strstr(out, "![[Nothing here]]") != NULL);                                                               /* does not resolve: left alone */
            CHECK(strstr(out, "> *Loop*") != NULL && strstr(out, "> ![[Loop]]") != NULL);                                  /* a note that embeds itself: shown once, its own embed stays as written */
            CHECK(strstr(out, "Intro\n") == out && strstr(out, "Middle") != NULL);
            free(out);
        }
        free(text);
        out = ex_embed_expand(w, root, "Host.md", "no embeds here\n", 15, &olen);
        CHECK(out == NULL);
    }
    ex_index_close(w);
    free(root);
    ex_platform_remove_tree(V); ex_platform_remove_tree(DB);

    /* ---- the reading view draws media ---- */
    {
        ExRenderer *r = ex_renderer_create("embeds", 900, 700);
        ExTheme theme;
        ExReadingView *v;
        ExReadingMedia m;
        ExImageCache *cache = ex_imagecache_create(0);
        float h0, h1, h2;
        const char *text = "Intro\n\n![[photo.png|300x200]]\n\n![[song.flac]]\n\n![[clip.mp4]]\n\n![[doc.pdf#page=3&height=400]]\n\n![[board.canvas]]\n\n![[missing.png]]\n\n![](https://example.org/x.png)\n\nEnd\n";
        if (!r) { fprintf(stderr, "renderer: %s\n", ex_render_error()); return 1; }
        CHECK(ex_theme_init_builtin(&theme, EX_THEME_DARK) == 0);
        v = ex_readingview_create(text, strlen(text));
        memset(&m, 0, sizeof m);
        m.resolve = resolve; m.audio_state = audio_state; m.images = cache;
        ex_readingview_set_viewport(v, 0, 0, 800, 650);
        ex_readingview_draw(v, r, &theme, 16.0f);
        h0 = ex_readingview_content_height(v);                                                 /* without media: the embeds are one line each */
        ex_readingview_set_media(v, &m);
        ex_readingview_set_link_handler(v, on_link, NULL);
        ex_readingview_draw(v, r, &theme, 16.0f);
        h1 = ex_readingview_content_height(v);
        CHECK(h1 > h0 + 200.0f);                                                               /* a 300 by 200 picture and four cards need room */
        {   /* the picture: asked for once (the cache has no loader here, so it stays loading) and drawn as a box of the written size */
            ExImageCacheStats cs;
            ex_imagecache_stats(cache, &cs);
            CHECK(cs.entries == 1 && cs.requests == 1);
        }
        /* a click on a card hands the path and the fragment to the handler: find them by sweeping down the column */
        {
            float y;
            char seen[6][400];
            int n = 0, k;
            memset(seen, 0, sizeof seen);
            for (y = 20.0f; y < 640.0f; y += 6.0f) {
                link_clicks = 0;
                ex_readingview_mouse(v, EX_MOUSE_DOWN, 400.0f, y);
                ex_readingview_mouse(v, EX_MOUSE_UP, 400.0f, y);
                if (link_clicks) { int dup = 0; for (k = 0; k < n; k++) if (!strcmp(seen[k], link_target)) dup = 1; if (!dup && n < 6) snprintf(seen[n++], 400, "%s", link_target); }
            }
            CHECK(n == 4);
            {
                int have_audio = 0, have_video = 0, have_pdf = 0, have_canvas = 0;
                for (k = 0; k < n; k++) {
                    if (!strcmp(seen[k], "Media/song.flac")) have_audio = 1;
                    if (!strcmp(seen[k], "clip.mp4")) have_video = 1;
                    if (!strcmp(seen[k], "doc.pdf#page=3&height=400")) have_pdf = 1;
                    if (!strcmp(seen[k], "board.canvas")) have_canvas = 1;
                }
                CHECK(have_audio && have_video && have_pdf && have_canvas);
            }
        }
        {   /* an answer for the picture arrives: the layout takes its real shape (here a 1 by 1 picture drawn at the size written) */
            ExImage *im = calloc(1, sizeof *im);
            im->w = im->h = im->src_w = im->src_h = 1;
            im->rgba = calloc(1, 4);
            ex_imagecache_result(cache, "photo.png", 8, im, NULL);                              /* the bucket asked for may differ: the entry's key is what get made */
            free(im);
            ex_readingview_draw(v, r, &theme, 16.0f);
            h2 = ex_readingview_content_height(v);
            CHECK(h2 > 0.0f);
        }
        ex_readingview_free(v);
        ex_imagecache_free(cache);
        ex_renderer_destroy(r);
    }
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("embeds: ok\n");
    return 0;
}
