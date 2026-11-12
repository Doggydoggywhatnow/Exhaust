/* System font database. Scans the OS font folders once and keeps, per face, the
 * family, style and which Unicode blocks it covers, so a font for a missing
 * glyph can be chosen without opening any font file. The result is cached in
 * the OS cache folder, keyed by the modification times of the font folders.
 * Fonts are opened only when a glyph needs them. */
#ifndef EXHAUST_FONTDB_H
#define EXHAUST_FONTDB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EX_FONT_FLAG_BOLD = 1,
    EX_FONT_FLAG_ITALIC = 2,
    EX_FONT_FLAG_MONO = 4,
    EX_FONT_FLAG_COLOR = 8
};

/* Coverage is kept for code points below EX_COVERAGE_LIMIT in pages of
 * EX_COVERAGE_PAGE code points. */
#define EX_COVERAGE_PAGE 128
#define EX_COVERAGE_LIMIT 0x30000
#define EX_COVERAGE_BYTES (EX_COVERAGE_LIMIT / EX_COVERAGE_PAGE / 8)

typedef struct ExFontEntry {
    char *path;
    int index; /* face index within the file (collections) */
    char *family;
    char *style;
    unsigned flags;
    uint8_t coverage[EX_COVERAGE_BYTES];
} ExFontEntry;

typedef struct ExFontScanStats {
    int cache_hit;       /* the list came from the cache file */
    int folders_visited; /* folders statted to compute the signature */
    int files_opened;    /* font files opened by this scan (0 on a cache hit) */
    int faces;           /* faces in the database */
} ExFontScanStats;

typedef struct ExFontDb ExFontDb;

/* Scans the given folders (or the OS font folders if dirs is NULL). cache_dir is
 * where the cache file goes (NULL: the OS cache folder; "" : no caching). */
ExFontDb *ex_fontdb_scan(const char *const *dirs, int ndirs, const char *cache_dir, ExFontScanStats *stats);
void ex_fontdb_free(ExFontDb *db);

int ex_fontdb_count(const ExFontDb *db);
const ExFontEntry *ex_fontdb_entry(const ExFontDb *db, int i);
/* Index of the face with this family name (case-insensitive) that best matches
 * the wanted style flags, or -1. */
int ex_fontdb_find_family(const ExFontDb *db, const char *family, unsigned want_flags);
/* Index of the nth best face (nth = 0 is the best) whose coverage includes cp,
 * or -1. Coverage is kept per page of 128 code points, so a face may still lack
 * the glyph: callers verify and try the next. Bold and italic in want_flags are
 * matched when possible; with need_color only color faces qualify. */
int ex_fontdb_find_codepoint(const ExFontDb *db, uint32_t cp, unsigned want_flags, int need_color, int nth);

#ifdef __cplusplus
}
#endif
#endif
