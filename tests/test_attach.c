/* attach (sub-slice 4d follow-up, found by hand in the 8c AppImage: "I can't drop images into a note"): the copy of a dropped file into the vault. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_attach.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define V "attach_test_vault"
#define SRC "attach_test_src"

static void put(const char *path, const void *data, size_t n) {
    char dir[600], *sl;
    snprintf(dir, sizeof dir, "%s", path);
    sl = strrchr(dir, '/'); if (sl) { *sl = 0; ex_platform_make_dirs(dir); }
    ex_platform_write_file_atomic(path, data, n);
}
static int same(const char *a, const char *b) {
    size_t na = 0, nb = 0; char *x = ex_platform_read_file(a, &na), *y = ex_platform_read_file(b, &nb);
    int r = x && y && na == nb && !memcmp(x, y, na);
    free(x); free(y);
    return r;
}
static int exists(const char *p) { ExPathInfo i; return ex_platform_path_info(p, &i) == 0; }

int main(void) {
    char root[600], abs[3000];
    ExAttachConfig c;
    ExAttachResult r;
    char folder[300];
    ex_platform_remove_tree(V); ex_platform_remove_tree(SRC);
    ex_platform_make_dirs(V);
    { char *rp = ex_platform_realpath(V); snprintf(root, sizeof root, "%s", rp ? rp : V); free(rp); }
    put(SRC "/photo.png", "\x89PNG-bytes-1", 13);
    put(SRC "/doc.txt", "text", 4);
    memset(&c, 0, sizeof c);
    c.vault_root = root; c.note_rel = "Notes/Trip/Day one.md"; c.folder = ""; c.subfolder = "";

    /* the top folder: the name is the link, a picture is embedded */
    c.location = EX_ATTACH_VAULT;
    CHECK(ex_attach_copy(&c, SRC "/photo.png", &r) == 0);
    CHECK(!strcmp(r.rel, "photo.png") && !strcmp(r.link, "![[photo.png]]"));
    snprintf(abs, sizeof abs, "%s/photo.png", root);
    CHECK(same(SRC "/photo.png", abs));
    CHECK(!exists(SRC "/photo.png.attach~") && !exists(V "/photo.png.attach~"));

    /* never replaced: the same file again, and a file that already has the name */
    CHECK(ex_attach_copy(&c, SRC "/photo.png", &r) == 0 && !strcmp(r.rel, "photo 2.png") && !strcmp(r.link, "![[photo 2.png]]"));
    CHECK(ex_attach_copy(&c, SRC "/photo.png", &r) == 0 && !strcmp(r.rel, "photo 3.png"));
    put(V "/doc.txt", "mine", 4);
    CHECK(ex_attach_copy(&c, SRC "/doc.txt", &r) == 0 && !strcmp(r.rel, "doc 2.txt") && !strcmp(r.link, "[[doc 2.txt]]"));
    { size_t n = 0; char *t = ex_platform_read_file(V "/doc.txt", &n); CHECK(t && n == 4 && !memcmp(t, "mine", 4)); free(t); }     /* the user's file is untouched */

    /* locations */
    c.location = EX_ATTACH_FOLDER; c.folder = "Attachments/Pictures";
    CHECK(ex_attach_copy(&c, SRC "/photo.png", &r) == 0 && !strcmp(r.rel, "Attachments/Pictures/photo.png") && !strcmp(r.link, "![[Attachments/Pictures/photo.png]]"));
    c.location = EX_ATTACH_SAME_FOLDER;
    CHECK(ex_attach_copy(&c, SRC "/photo.png", &r) == 0 && !strcmp(r.rel, "Notes/Trip/photo.png"));
    c.location = EX_ATTACH_SUBFOLDER; c.subfolder = "files";
    CHECK(ex_attach_copy(&c, SRC "/photo.png", &r) == 0 && !strcmp(r.rel, "Notes/Trip/files/photo.png"));
    c.note_rel = "Top.md";
    CHECK(ex_attach_copy(&c, SRC "/photo.png", &r) == 0 && !strcmp(r.rel, "files/photo.png"));

    /* settings that would leave the vault or touch Exhaust's own folders fall back */
    c.note_rel = "Notes/Trip/Day one.md";
    {   static const char *const bad[] = { "../outside", "/abs", "a/../../b", "C:/x", "a\\b", ".exhaust", ".exhaust/plugins", ".trash/x", "." };
        size_t i;
        c.location = EX_ATTACH_FOLDER;
        for (i = 0; i < sizeof bad / sizeof *bad; i++) {
            c.folder = bad[i];
            ex_attach_folder(&c, folder, sizeof folder);
            if (folder[0]) { fprintf(stderr, "unsafe folder kept: %s -> %s\\n", bad[i], folder); failures++; }
        }
        c.location = EX_ATTACH_SUBFOLDER; c.subfolder = "../up";
        ex_attach_folder(&c, folder, sizeof folder);
        CHECK(!strcmp(folder, "Notes/Trip"));                                          /* an unusable subfolder: the note's own folder */
        c.location = EX_ATTACH_FOLDER; c.folder = "/Docs/Pics/";
        ex_attach_folder(&c, folder, sizeof folder);
        CHECK(folder[0] == 0);                                                          /* absolute: not used */
        c.folder = "Docs//Pics/";
        ex_attach_folder(&c, folder, sizeof folder);
        CHECK(!strcmp(folder, "Docs//Pics") || !strcmp(folder, "Docs/Pics"));
    }
    CHECK(!exists(V "/../outside"));

    /* names a note could not have */
    c.location = EX_ATTACH_VAULT;
    put(SRC "/we#ird [name]^x.png", "x", 1);
    CHECK(ex_attach_copy(&c, SRC "/we#ird [name]^x.png", &r) == 0 && !strcmp(r.rel, "we_ird _name__x.png"));
#ifndef _WIN32
    put(SRC "/a|b:c.png", "x", 1);                                                  /* a file name Windows cannot have (CI found the test failing there: ISSUES 84) */
    CHECK(ex_attach_copy(&c, SRC "/a|b:c.png", &r) == 0 && !strcmp(r.rel, "a_b_c.png"));
#endif
    put(SRC "/CON.png", "x", 1);
    CHECK(ex_attach_copy(&c, SRC "/CON.png", &r) == 0 && !strcmp(r.rel, "CON_.png"));
    put(SRC "/noext", "x", 1);
    CHECK(ex_attach_copy(&c, SRC "/noext", &r) == 0 && !strcmp(r.rel, "noext") && !strcmp(r.link, "[[noext]]"));
    put(SRC "/clip.PDF", "%PDF", 4);
    CHECK(ex_attach_copy(&c, SRC "/clip.PDF", &r) == 0 && !strcmp(r.link, "![[clip.PDF]]"));
    put(SRC "/song.mp3", "x", 1);
    CHECK(ex_attach_copy(&c, SRC "/song.mp3", &r) == 0 && !strcmp(r.link, "![[song.mp3]]"));

    /* a big file is copied in blocks, byte for byte */
    {   size_t n = 5 * 1024 * 1024 + 123, i;
        unsigned char *big = malloc(n);
        for (i = 0; i < n; i++) big[i] = (unsigned char)(i * 31 + (i >> 8));
        put(SRC "/big.bin", big, n);
        free(big);
        CHECK(ex_attach_copy(&c, SRC "/big.bin", &r) == 0);
        snprintf(abs, sizeof abs, "%s/%s", root, r.rel);
        CHECK(same(SRC "/big.bin", abs));
    }

    /* bytes (a picture from the clipboard) take the same place, the same rules and the name that was asked for */
    c.location = EX_ATTACH_FOLDER; c.folder = "Pasted";
    CHECK(ex_attach_save_bytes(&c, "Pasted image 20260101000000", ".png", "\x89PNGxyz", 7, &r) == 0 && !strcmp(r.rel, "Pasted/Pasted image 20260101000000.png") && !strcmp(r.link, "![[Pasted/Pasted image 20260101000000.png]]"));
    snprintf(abs, sizeof abs, "%s/%s", root, r.rel);
    { size_t n = 0; char *t = ex_platform_read_file(abs, &n); CHECK(t && n == 7 && !memcmp(t, "\x89PNGxyz", 7)); free(t); }
    CHECK(ex_attach_save_bytes(&c, "Pasted image 20260101000000", ".png", "other!!", 7, &r) == 0 && !strcmp(r.rel, "Pasted/Pasted image 20260101000000 2.png"));      /* the same second: never replaced */
    CHECK(ex_attach_save_bytes(&c, "x", ".png", "", 0, &r) == -1 && r.error[0]);
    CHECK(ex_attach_save_bytes(&c, "x", ".png", NULL, 5, &r) == -1);
    c.location = EX_ATTACH_VAULT;

    /* refusals write nothing */
    CHECK(ex_attach_copy(&c, SRC "/missing.png", &r) == -1 && r.error[0]);
    CHECK(ex_attach_copy(&c, SRC, &r) == -1 && r.error[0]);                           /* a folder */
    CHECK(ex_attach_copy(&c, "", &r) == -1);
    CHECK(!exists(V "/missing.png"));

    ex_platform_remove_tree(V); ex_platform_remove_tree(SRC);
    if (failures) { fprintf(stderr, "attach: %d failures\n", failures); return 1; }
    printf("attach ok\n");
    return 0;
}
