#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SFNT_NAMES_H

#include "exhaust_fontdb.h"
#include "exhaust_platform.h"

#define CACHE_MAGIC "EXHAUST-FONTS 1"
#define MAX_FACES_PER_FILE 64

struct ExFontDb {
    ExFontEntry *e;
    int n, cap;
};

/* ---- helpers ---- */

static char *dup_str(const char *s) {
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    if (p) memcpy(p, s, n + 1);
    return p;
}

static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

static int has_font_ext(const char *path) {
    const char *dot = strrchr(path, '.');
    return dot && (ieq(dot, ".ttf") || ieq(dot, ".otf") || ieq(dot, ".ttc") || ieq(dot, ".otc"));
}

static uint64_t fnv(uint64_t h, const void *data, size_t n) {
    const unsigned char *p = data;
    size_t i;
    for (i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

static ExFontEntry *add_entry(ExFontDb *db) {
    if (db->n == db->cap) {
        int nc = db->cap ? db->cap * 2 : 64;
        ExFontEntry *ne = realloc(db->e, (size_t)nc * sizeof *ne);
        if (!ne) return NULL;
        db->e = ne;
        db->cap = nc;
    }
    memset(&db->e[db->n], 0, sizeof db->e[0]);
    return &db->e[db->n++];
}

void ex_fontdb_free(ExFontDb *db) {
    int i;
    if (!db) return;
    for (i = 0; i < db->n; i++) { free(db->e[i].path); free(db->e[i].family); free(db->e[i].style); }
    free(db->e);
    free(db);
}

int ex_fontdb_count(const ExFontDb *db) { return db ? db->n : 0; }
const ExFontEntry *ex_fontdb_entry(const ExFontDb *db, int i) { return db && i >= 0 && i < db->n ? &db->e[i] : NULL; }

/* ---- scanning ---- */

typedef struct Walk {
    uint64_t sig;
    int folders;
    char **files;
    int nfiles, capfiles;
} Walk;

static int on_entry(const char *path, int is_dir, int64_t mtime, void *user) {
    Walk *w = user;
    if (is_dir) {
        w->sig = fnv(w->sig, path, strlen(path));
        w->sig = fnv(w->sig, &mtime, sizeof mtime);
        w->folders++;
    } else if (has_font_ext(path)) {
        if (w->nfiles == w->capfiles) {
            int nc = w->capfiles ? w->capfiles * 2 : 128;
            char **nf = realloc(w->files, (size_t)nc * sizeof *nf);
            if (!nf) return 1;
            w->files = nf;
            w->capfiles = nc;
        }
        w->files[w->nfiles] = dup_str(path);
        if (w->files[w->nfiles]) w->nfiles++;
    }
    return 0;
}

static void fill_coverage(FT_Face face, uint8_t *cov) {
    FT_UInt gi;
    FT_ULong c = FT_Get_First_Char(face, &gi);
    while (gi != 0) {
        if (c < EX_COVERAGE_LIMIT) {
            size_t page = c / EX_COVERAGE_PAGE;
            cov[page >> 3] |= (uint8_t)(1u << (page & 7));
        } else break;
        c = FT_Get_Next_Char(face, c, &gi);
    }
}

static int clean_field(const char *s) {
    for (; *s; s++)
        if (*s == '\t' || *s == '\n' || *s == '\r') return 0;
    return 1;
}

static void scan_file(FT_Library ft, ExFontDb *db, const char *path, int *opened) {
    FT_Face face;
    long nfaces, i;
    if (FT_New_Face(ft, path, -1, &face)) return;
    (*opened)++;
    nfaces = face->num_faces;
    FT_Done_Face(face);
    if (nfaces > MAX_FACES_PER_FILE) nfaces = MAX_FACES_PER_FILE;
    for (i = 0; i < nfaces; i++) {
        ExFontEntry *e;
        if (FT_New_Face(ft, path, i, &face)) continue;
        if (face->family_name && clean_field(face->family_name) && clean_field(path) &&
            (FT_IS_SCALABLE(face) || FT_HAS_COLOR(face)) && !FT_Select_Charmap(face, FT_ENCODING_UNICODE) &&
            (e = add_entry(db)) != NULL) {
            e->path = dup_str(path);
            e->index = (int)i;
            e->family = dup_str(face->family_name);
            e->style = dup_str(face->style_name && clean_field(face->style_name) ? face->style_name : "");
            e->flags = (face->style_flags & FT_STYLE_FLAG_BOLD ? EX_FONT_FLAG_BOLD : 0) |
                       (face->style_flags & FT_STYLE_FLAG_ITALIC ? EX_FONT_FLAG_ITALIC : 0) |
                       (FT_IS_FIXED_WIDTH(face) ? EX_FONT_FLAG_MONO : 0) |
                       (FT_HAS_COLOR(face) ? EX_FONT_FLAG_COLOR : 0);
            fill_coverage(face, e->coverage);
            if (!e->path || !e->family || !e->style) { /* out of memory: drop the entry */
                free(e->path); free(e->family); free(e->style);
                db->n--;
            }
        }
        FT_Done_Face(face);
    }
}

/* ---- cache file ---- */

static char *cache_path(const char *cache_dir) {
    char *dir = NULL, *path;
    size_t n;
    if (cache_dir && !*cache_dir) return NULL; /* caching off */
    dir = cache_dir ? dup_str(cache_dir) : ex_platform_cache_dir();
    if (!dir) return NULL;
    n = strlen(dir);
    path = malloc(n + 16);
    if (path) { memcpy(path, dir, n); memcpy(path + n, "/fonts.cache", 13); }
    free(dir);
    return path;
}

static const char HEX[] = "0123456789abcdef";

static int load_cache(const char *path, uint64_t sig, ExFontDb *db) {
    size_t len = 0;
    char *data = ex_platform_read_file(path, &len), *p, *line;
    char want[64];
    int ok = 0;
    if (!data) return 0;
    snprintf(want, sizeof want, CACHE_MAGIC "\nsig %016llx\n", (unsigned long long)sig);
    if (strncmp(data, want, strlen(want)) != 0) { free(data); return 0; }
    p = data + strlen(want);
    for (line = p; *line;) {
        char *end = strchr(line, '\n'), *f[6];
        int k;
        ExFontEntry *e;
        if (!end) break;
        *end = 0;
        f[0] = line;
        for (k = 1; k < 6; k++) {
            char *t = strchr(f[k - 1], '\t');
            if (!t) break;
            *t = 0;
            f[k] = t + 1;
        }
        if (k < 6 || strlen(f[5]) != EX_COVERAGE_BYTES * 2) { ok = 0; goto done; }
        if (!(e = add_entry(db))) { ok = 0; goto done; }
        e->path = dup_str(f[0]);
        e->index = atoi(f[1]);
        e->flags = (unsigned)atoi(f[2]);
        e->family = dup_str(f[3]);
        e->style = dup_str(f[4]);
        for (k = 0; k < EX_COVERAGE_BYTES; k++) {
            const char *a = strchr(HEX, f[5][2 * k]), *b = strchr(HEX, f[5][2 * k + 1]);
            if (!a || !b) { ok = 0; goto done; }
            e->coverage[k] = (uint8_t)(((a - HEX) << 4) | (b - HEX));
        }
        line = end + 1;
        ok = 1;
    }
    ok = 1; /* an empty list is a valid cache */
done:
    free(data);
    return ok;
}

static void save_cache(const char *path, uint64_t sig, const ExFontDb *db) {
    size_t cap = 64 + (size_t)db->n * (EX_COVERAGE_BYTES * 2 + 512), used = 0;
    char *buf = malloc(cap), *slash;
    int i;
    char *dir;
    if (!buf) return;
    used += (size_t)snprintf(buf, cap, CACHE_MAGIC "\nsig %016llx\n", (unsigned long long)sig);
    for (i = 0; i < db->n; i++) {
        const ExFontEntry *e = &db->e[i];
        int k;
        size_t need = strlen(e->path) + strlen(e->family) + strlen(e->style) + EX_COVERAGE_BYTES * 2 + 64;
        if (used + need > cap) {
            char *nb;
            cap = (used + need) * 2;
            nb = realloc(buf, cap);
            if (!nb) { free(buf); return; }
            buf = nb;
        }
        used += (size_t)sprintf(buf + used, "%s\t%d\t%u\t%s\t%s\t", e->path, e->index, e->flags, e->family, e->style);
        for (k = 0; k < EX_COVERAGE_BYTES; k++) {
            buf[used++] = HEX[e->coverage[k] >> 4];
            buf[used++] = HEX[e->coverage[k] & 15];
        }
        buf[used++] = '\n';
    }
    dir = dup_str(path);
    if (dir) {
        slash = strrchr(dir, '/');
        if (slash) { *slash = 0; ex_platform_make_dirs(dir); }
        free(dir);
    }
    ex_platform_write_file_atomic(path, buf, used); /* a failed cache write only costs a rescan */
    free(buf);
}

/* ---- public scan ---- */

ExFontDb *ex_fontdb_scan(const char *const *dirs, int ndirs, const char *cache_dir, ExFontScanStats *stats) {
    ExFontDb *db = calloc(1, sizeof *db);
    Walk w = {0};
    char **owned = NULL;
    int nowned = 0, i, opened = 0;
    char *cpath;
    ExFontScanStats st = {0};

    if (!db) return NULL;
    if (!dirs) {
        nowned = ex_platform_font_dirs(&owned);
        dirs = (const char *const *)owned;
        ndirs = nowned;
    }
    w.sig = 1469598103934665603ull;
    for (i = 0; i < ndirs; i++) (void)ex_platform_walk(dirs[i], on_entry, &w); /* a missing folder is fine */
    st.folders_visited = w.folders;

    cpath = cache_path(cache_dir);
    if (cpath && load_cache(cpath, w.sig, db)) {
        st.cache_hit = 1;
    } else {
        FT_Library ft;
        /* a failed or partial cache load may have left entries behind */
        int j;
        for (j = 0; j < db->n; j++) { free(db->e[j].path); free(db->e[j].family); free(db->e[j].style); }
        db->n = 0;
        if (FT_Init_FreeType(&ft) == 0) {
            for (i = 0; i < w.nfiles; i++) scan_file(ft, db, w.files[i], &opened);
            FT_Done_FreeType(ft);
        }
        if (cpath) save_cache(cpath, w.sig, db);
    }
    st.files_opened = opened;
    st.faces = db->n;
    if (stats) *stats = st;

    for (i = 0; i < w.nfiles; i++) free(w.files[i]);
    free(w.files);
    free(cpath);
    ex_platform_free_list(owned, nowned);
    return db;
}

/* ---- queries ---- */

int ex_fontdb_find_family(const ExFontDb *db, const char *family, unsigned want) {
    int i, best = -1, best_score = -1;
    if (!db || !family) return -1;
    for (i = 0; i < db->n; i++) {
        const ExFontEntry *e = &db->e[i];
        int score;
        if (!ieq(e->family, family)) continue;
        score = 2 - (int)(((e->flags ^ want) & EX_FONT_FLAG_BOLD) != 0) - (int)(((e->flags ^ want) & EX_FONT_FLAG_ITALIC) != 0);
        if (score > best_score) { best = i; best_score = score; }
    }
    return best;
}

typedef struct Cand { int index, score; } Cand;

static int cmp_cand(const void *a, const void *b) {
    const Cand *x = a, *y = b;
    if (x->score != y->score) return y->score - x->score;
    return x->index - y->index;
}

int ex_fontdb_find_codepoint(const ExFontDb *db, uint32_t cp, unsigned want, int need_color, int nth) {
    size_t page;
    Cand *c;
    int i, n = 0, result = -1;
    if (!db || cp >= EX_COVERAGE_LIMIT || nth < 0) return -1;
    page = cp / EX_COVERAGE_PAGE;
    c = malloc((size_t)(db->n ? db->n : 1) * sizeof *c);
    if (!c) return -1;
    for (i = 0; i < db->n; i++) {
        const ExFontEntry *e = &db->e[i];
        if (!(e->coverage[page >> 3] & (1u << (page & 7)))) continue;
        if (need_color && !(e->flags & EX_FONT_FLAG_COLOR)) continue;
        c[n].index = i;
        c[n].score = 4 - 2 * (int)(((e->flags ^ want) & EX_FONT_FLAG_BOLD) != 0) -
                     2 * (int)(((e->flags ^ want) & EX_FONT_FLAG_ITALIC) != 0) +
                     (e->flags & EX_FONT_FLAG_COLOR ? (need_color ? 1 : -1) : 0);
        n++;
    }
    qsort(c, (size_t)n, sizeof *c, cmp_cand);
    if (nth < n) result = c[nth].index;
    free(c);
    return result;
}
