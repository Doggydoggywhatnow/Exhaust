/* Dropping a file into a note: see include/exhaust_attach.h and docs/formats.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_attach.h"
#include "exhaust_platform.h"
#include "exhaust_vault.h"

static void fail(ExAttachResult *o, const char *what, const char *path) {
    snprintf(o->error, sizeof o->error, "%s: %.200s", what, path ? path : "");
}

/* A folder setting that stays inside the vault and is not Exhaust's own: relative, no "..", no backslash, no colon, not .exhaust or .trash. */
static int safe_rel_folder(const char *p) {
    const char *s = p, *e;
    if (!p || !p[0] || p[0] == '/' || strchr(p, '\\') || strchr(p, ':')) return 0;
    while (*s) {
        size_t n;
        e = strchr(s, '/');
        n = e ? (size_t)(e - s) : strlen(s);
        if (n == 0) { if (!e) break; s = e + 1; continue; }
        if ((n == 2 && !strncmp(s, "..", 2)) || (n == 1 && s[0] == '.')) return 0;
        if ((n == 8 && !strncmp(s, ".exhaust", 8)) || (n == 6 && !strncmp(s, ".trash", 6))) return 0;
        if (!e) break;
        s = e + 1;
    }
    return 1;
}

static void trim_slashes(char *s) {
    size_t n = strlen(s), a = 0;
    while (n && s[n - 1] == '/') s[--n] = 0;
    while (s[a] == '/') a++;
    if (a) memmove(s, s + a, strlen(s + a) + 1);
}

void ex_attach_folder(const ExAttachConfig *c, char *out, size_t cap) {
    char dir[1500] = "", tmp[1700];
    out[0] = 0;
    if (!c) return;
    switch (c->location) {
    case EX_ATTACH_FOLDER:
        if (safe_rel_folder(c->folder)) snprintf(out, cap, "%s", c->folder);
        break;
    case EX_ATTACH_SAME_FOLDER: case EX_ATTACH_SUBFOLDER: {
        const char *sl = c->note_rel ? strrchr(c->note_rel, '/') : NULL;
        if (sl) snprintf(dir, sizeof dir, "%.*s", (int)(sl - c->note_rel), c->note_rel);
        if (c->location == EX_ATTACH_SAME_FOLDER) { snprintf(out, cap, "%s", dir); break; }
        if (!safe_rel_folder(c->subfolder)) { snprintf(out, cap, "%s", dir); break; }       /* no valid subfolder: the note's own folder */
        snprintf(tmp, sizeof tmp, "%s%s%s", dir, dir[0] ? "/" : "", c->subfolder);
        snprintf(out, cap, "%s", tmp);
        break;
    }
    default: break;
    }
    if (!safe_rel_folder(out) && out[0]) out[0] = 0;
    trim_slashes(out);
}

void ex_attach_link(const char *rel, char *out, size_t cap) {
    const char *slash = strrchr(rel, '/'), *name = slash ? slash + 1 : rel;
    ExFileKind k = ex_file_kind(name);
    int embed = k == EX_KIND_IMAGE || k == EX_KIND_AUDIO || k == EX_KIND_VIDEO || k == EX_KIND_PDF;
    snprintf(out, cap, "%s[[%s]]", embed ? "!" : "", slash ? rel : name);                 /* in the top folder the name is enough; elsewhere the path, which cannot match another file */
}

/* The dropped file's name as a name a note could have. */
static void clean_name(const char *name, char *stem, size_t scap, char *ext, size_t ecap) {
    char t[400];
    const char *dot;
    size_t i, w = 0;
    for (i = 0; name[i] && w + 1 < sizeof t; i++) {
        char ch = name[i];
        if (ch == '#' || ch == '^' || ch == '[' || ch == ']' || ch == '|' || ch == ':' || ch == '/' || ch == '\\' || (unsigned char)ch < 0x20) ch = '_';
        t[w++] = ch;
    }
    t[w] = 0;
    for (i = 0; i + 1 < w; i++) if (t[i] == '%' && t[i + 1] == '%') { t[i] = '_'; t[i + 1] = '_'; i++; }       /* "%%" starts a comment in a note's name */
    while (w && (t[w - 1] == ' ' || t[w - 1] == '.')) t[--w] = 0;
    { size_t lead = 0; while (t[lead] == ' ') lead++; if (lead) { memmove(t, t + lead, w - lead + 1); w -= lead; } }      /* a name cannot start with a space either */
    dot = strrchr(t, '.');
    if (dot && dot != t && strlen(dot) <= 16) { snprintf(ext, ecap, "%s", dot); snprintf(stem, scap, "%.*s", (int)(dot - t), t); }
    else { ext[0] = 0; snprintf(stem, scap, "%s", t); }
    if (strlen(stem) > 100) { size_t k = 100; while (k > 0 && ((unsigned char)stem[k] & 0xC0) == 0x80) k--; stem[k] = 0; }
    if (!stem[0]) snprintf(stem, scap, "Attachment");
    {   /* a reserved Windows name, or anything else a note's name cannot be: a mark after it */
        char probe[500];
        snprintf(probe, sizeof probe, "%s", stem);
        if (!ex_name_valid(probe)) { snprintf(probe, sizeof probe, "%s_", stem); snprintf(stem, scap, "%s", probe); }
    }
}

/* The copy itself: `in` is read from its start, the result is a new file stem+ext in the chosen folder, never replacing one. */
static int attach_stream(const ExAttachConfig *c, SDL_IOStream *in, const char *stem, const char *ext, ExAttachResult *out) {
    char folder[1600], dirabs[3300];
    int k;
    ex_attach_folder(c, folder, sizeof folder);
    snprintf(dirabs, sizeof dirabs, "%s%s%s", c->vault_root, folder[0] ? "/" : "", folder);
    if (ex_platform_make_dirs(dirabs) != 0) { fail(out, "The folder could not be made", dirabs); return -1; }
    for (k = 1; k < 10000; k++) {
        char rel[2200], abs[3800], tmp[3900];
        ExPathInfo ti;
        SDL_IOStream *o;
        char *buf;
        int ok = 1;
        if (k == 1) snprintf(rel, sizeof rel, "%s%s%s%s", folder, folder[0] ? "/" : "", stem, ext);
        else snprintf(rel, sizeof rel, "%s%s%s %d%s", folder, folder[0] ? "/" : "", stem, k, ext);
        snprintf(abs, sizeof abs, "%s/%s", c->vault_root, rel);
        if (ex_platform_path_info(abs, &ti) == 0) continue;                                  /* taken */
        snprintf(tmp, sizeof tmp, "%s.attach~", abs);
        o = SDL_IOFromFile(tmp, "wb");
        if (!o) { fail(out, "The copy could not be written", abs); return -1; }
        buf = malloc(1 << 16);
        if (!buf) { SDL_CloseIO(o); ex_platform_remove_tree(tmp); fail(out, "Out of memory", ""); return -1; }
        SDL_SeekIO(in, 0, SDL_IO_SEEK_SET);
        for (;;) {
            size_t n = SDL_ReadIO(in, buf, 1 << 16);
            if (n == 0) { if (SDL_GetIOStatus(in) != SDL_IO_STATUS_EOF) ok = 0; break; }
            if (SDL_WriteIO(o, buf, n) != n) { ok = 0; break; }
        }
        free(buf);
        if (!SDL_CloseIO(o)) ok = 0;
        if (!ok) { ex_platform_remove_tree(tmp); fail(out, "The copy could not be written", abs); return -1; }
        if (ex_platform_rename_noreplace(tmp, abs) != 0) { ex_platform_remove_tree(tmp); continue; }     /* it appeared meanwhile: the next number */
        snprintf(out->rel, sizeof out->rel, "%s", rel);
        ex_attach_link(rel, out->link, sizeof out->link);
        return 0;
    }
    fail(out, "No free name for the copy", stem);
    return -1;
}

int ex_attach_copy(const ExAttachConfig *c, const char *source, ExAttachResult *out) {
    char stem[400], ext[40];
    const char *slash;
    ExPathInfo si;
    SDL_IOStream *in;
    int rc;
    memset(out, 0, sizeof *out);
    if (!c || !c->vault_root || !source || !source[0]) { fail(out, "Nothing to copy", ""); return -1; }
    if (ex_platform_path_info(source, &si) != 0) { fail(out, "The file cannot be read", source); return -1; }
    if (si.is_dir || !si.is_file) { fail(out, "A folder cannot be dropped into a note", source); return -1; }
    slash = strrchr(source, '/');
#ifdef _WIN32
    { const char *bs = strrchr(source, '\\'); if (bs && (!slash || bs > slash)) slash = bs; }
#endif
    clean_name(slash ? slash + 1 : source, stem, sizeof stem, ext, sizeof ext);
    in = SDL_IOFromFile(source, "rb");
    if (!in) { fail(out, "The file cannot be read", source); return -1; }
    rc = attach_stream(c, in, stem, ext, out);
    SDL_CloseIO(in);
    return rc;
}

int ex_attach_save_bytes(const ExAttachConfig *c, const char *stem, const char *ext, const void *data, size_t len, ExAttachResult *out) {
    SDL_IOStream *in;
    char st[400], ex[40];
    int rc;
    memset(out, 0, sizeof *out);
    if (!c || !c->vault_root || !data || len == 0) { fail(out, "Nothing to save", ""); return -1; }
    {   /* the name and the extension come from the caller (a picture's name from the clipboard, a type from a format): a "/" or ".." in either would write outside
         * the folder, even outside the vault. They go through the same cleaning as the name of a dropped file. */
        char joined[500], st2[400], ex2[40];
        snprintf(joined, sizeof joined, "%s%s", stem && stem[0] ? stem : "Attachment", ext ? ext : "");
        clean_name(joined, st2, sizeof st2, ex2, sizeof ex2);
        snprintf(st, sizeof st, "%s", st2);
        snprintf(ex, sizeof ex, "%s", ex2);
    }
    in = SDL_IOFromConstMem(data, len);
    if (!in) { fail(out, "Out of memory", ""); return -1; }
    rc = attach_stream(c, in, st, ex, out);
    SDL_CloseIO(in);
    return rc;
}
