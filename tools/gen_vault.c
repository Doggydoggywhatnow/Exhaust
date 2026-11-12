/* Writes a deterministic test vault: the same seed always gives byte-identical files.
 * Usage: gen_vault OUT_DIR [--seed N] [--notes N] [--links N]
 * Defaults: seed 1, 5,000 notes, 20,000 wikilinks. The notes have properties (tags, aliases,
 * dates), headings, inline tags and wikilinks of the forms [[Note]], [[Note|alias]] and
 * [[Note#Heading]]; there are canvases and PNG attachments (shown with Markdown image syntax, so
 * they are not counted as wikilinks). Nothing in the output depends on the clock or the machine. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_platform.h"

static uint64_t g_state;

static uint64_t next(void) {                       /* splitmix64: small, fast, the same everywhere */
    uint64_t z = (g_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
static unsigned below(unsigned n) { return (unsigned)(next() % n); }

static const char *const WORDS[] = {
    "harbor", "quartz", "lantern", "meadow", "copper", "ember", "willow", "granite", "orbit", "velvet", "thistle", "canyon", "mosaic", "falcon",
    "tundra", "saffron", "cobalt", "juniper", "marble", "nimbus", "obsidian", "pebble", "quill", "raven", "sundial", "tangle", "umber", "violet",
    "wander", "yonder", "zephyr", "anchor", "beacon", "cinder", "driftwood", "echo", "fable", "glacier", "hollow", "ivory", "jasper", "kindle",
    "ledger", "meridian", "nectar", "opal", "prairie", "quarry", "ripple", "sparrow", "timber", "unfold", "valley", "winter", "xenon", "yarrow",
    "zenith", "atlas", "bramble", "compass", "dune", "estuary", "fjord", "grove"};
#define NWORDS (sizeof WORDS / sizeof WORDS[0])
static const char *const TAGS[] = {"project", "idea", "reading", "daily", "meeting", "research", "draft", "archive", "todo", "reference", "people", "health"};
#define NTAGS (sizeof TAGS / sizeof TAGS[0])
static const char *const HEAD[] = {"Summary", "Details", "Questions", "Next steps", "Background", "Notes", "Decisions", "Open items"};
#define NHEAD (sizeof HEAD / sizeof HEAD[0])

/* The 1x1 transparent PNG. */
static const unsigned char PNG[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xFF, 0xFF, 0x3F, 0x00, 0x05, 0xFE, 0x02, 0xFE,
    0xA7, 0x35, 0x81, 0x84, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

static int g_notes = 5000, g_links = 20000;
static unsigned g_attach = 24;

#define FOLDERS 40
static unsigned folder_of(int i) { return (unsigned)i % FOLDERS; }

static void note_name(int i, char *out, size_t cap) {
    snprintf(out, cap, "%c%s %s %05d", (char)(WORDS[i % NWORDS][0] - 32), WORDS[i % NWORDS] + 1, WORDS[(i * 7 + 3) % NWORDS], i);
}
static void note_path(int i, char *out, size_t cap) {
    char name[96];
    note_name(i, name, sizeof name);
    snprintf(out, cap, "Area %02u/%s.md", folder_of(i), name);
}

static void write_file(const char *root, const char *rel, const void *data, size_t len) {
    char path[1024];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", root, rel);
    f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "gen_vault: cannot write %s\n", path); exit(1); }
    fwrite(data, 1, len, f);
    fclose(f);
}

static void sentence(char *out, size_t cap) {
    unsigned n = 8 + below(7), i;
    size_t used = 0;
    out[0] = 0;
    for (i = 0; i < n && used + 20 < cap; i++) {
        const char *w = WORDS[below((unsigned)NWORDS)];
        used += (size_t)snprintf(out + used, cap - used, "%s%s", i ? " " : "", w);
    }
    if (used + 3 < cap) { out[used] = '.'; out[used + 1] = 0; }
    if (out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 32);
}

int main(int argc, char **argv) {
    const char *out = NULL;
    uint64_t seed = 1;
    int i, a, links_left, canvases = 6;
    char *buf;
    size_t cap = 16384;
    int per, extra, total_links = 0, tag_uses = 0;
    for (a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--seed") && a + 1 < argc) seed = strtoull(argv[++a], NULL, 10);
        else if (!strcmp(argv[a], "--notes") && a + 1 < argc) g_notes = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--links") && a + 1 < argc) g_links = atoi(argv[++a]);
        else if (argv[a][0] != '-' && !out) out = argv[a];
        else { fprintf(stderr, "usage: gen_vault OUT_DIR [--seed N] [--notes N] [--links N]\n"); return 2; }
    }
    if (!out || g_notes < 1 || g_links < 0) { fprintf(stderr, "usage: gen_vault OUT_DIR [--seed N] [--notes N] [--links N]\n"); return 2; }
    g_state = seed * 0x2545F4914F6CDD1Dull + 12345;
    buf = malloc(cap);
    if (!buf) return 1;
    ex_platform_make_dirs(out);
    {
        char dir[1024];
        for (i = 0; i < FOLDERS && i < g_notes; i++) { snprintf(dir, sizeof dir, "%s/Area %02d", out, i); ex_platform_make_dirs(dir); }
        snprintf(dir, sizeof dir, "%s/Attachments", out);
        ex_platform_make_dirs(dir);
        snprintf(dir, sizeof dir, "%s/Canvases", out);
        ex_platform_make_dirs(dir);
    }
    per = g_links / g_notes;
    extra = g_links % g_notes;
    links_left = g_links;
    for (i = 0; i < g_notes; i++) {
        char name[96], path[160], s[512];
        size_t n = 0;
        int links = per + (i < extra), sections = 1 + (int)below(3), sec, l, linked = 0;
        note_name(i, name, sizeof name);
        note_path(i, path, sizeof path);
        n += (size_t)snprintf(buf + n, cap - n, "---\ntitle: %s\ntags: [%s, %s]\naliases: [%s %d]\ncreated: 20%02d-%02d-%02d\nstatus: %s\n---\n", name,
                              TAGS[below((unsigned)NTAGS)], TAGS[below((unsigned)NTAGS)], WORDS[below((unsigned)NWORDS)], i % 1000, 15 + i % 10, 1 + i % 12, 1 + i % 28,
                              below(2) ? "active" : "done");
        n += (size_t)snprintf(buf + n, cap - n, "\n# %s\n", name);
        for (sec = 0; sec < sections; sec++) {
            int para = 1 + (int)below(2), p;
            n += (size_t)snprintf(buf + n, cap - n, "\n## %s\n", HEAD[(unsigned)(i + sec * 3) % NHEAD]);
            for (p = 0; p < para; p++) {
                unsigned sents = 2 + below(3), k;
                n += (size_t)snprintf(buf + n, cap - n, "\n");
                for (k = 0; k < sents; k++) {
                    sentence(s, sizeof s);
                    n += (size_t)snprintf(buf + n, cap - n, "%s%s", k ? " " : "", s);
                    if (linked < links && below(2)) {      /* a link in the middle of a sentence */
                        int t = (int)below((unsigned)g_notes);
                        char tn[96];
                        note_name(t, tn, sizeof tn);
                        switch (below(3)) {
                        case 0: n += (size_t)snprintf(buf + n, cap - n, " See [[%s]]", tn); break;
                        case 1: n += (size_t)snprintf(buf + n, cap - n, " See [[%s|%s]]", tn, WORDS[below((unsigned)NWORDS)]); break;
                        default: n += (size_t)snprintf(buf + n, cap - n, " See [[%s#%s]]", tn, HEAD[below((unsigned)NHEAD)]); break;
                        }
                        linked++;
                    }
                }
                if (below(4) == 0) { n += (size_t)snprintf(buf + n, cap - n, " #%s", TAGS[below((unsigned)NTAGS)]); tag_uses++; }
                n += (size_t)snprintf(buf + n, cap - n, "\n");
            }
        }
        if (below(30) == 0) n += (size_t)snprintf(buf + n, cap - n, "\n![picture](Attachments/image-%02u.png)\n", below(g_attach));
        n += (size_t)snprintf(buf + n, cap - n, "\n## Related\n\n");
        for (l = linked; l < links; l++) {                    /* the rest of this note's links go in a list */
            int t = (int)below((unsigned)g_notes);
            char tn[96];
            note_name(t, tn, sizeof tn);
            n += (size_t)snprintf(buf + n, cap - n, "- [[%s]]\n", tn);
        }
        total_links += links;
        links_left -= links;
        write_file(out, path, buf, n);
    }
    /* attachments */
    for (a = 0; a < (int)g_attach; a++) {
        char rel[64];
        snprintf(rel, sizeof rel, "Attachments/image-%02d.png", a);
        write_file(out, rel, PNG, sizeof PNG);
    }
    /* canvases: file cards for some notes, a text card and edges */
    for (a = 0; a < canvases; a++) {
        size_t n = 0;
        int nodes = 6 + a, k;
        char rel[64];
        n += (size_t)snprintf(buf + n, cap - n, "{\n  \"nodes\": [\n");
        for (k = 0; k < nodes; k++) {
            char p[160];
            note_path(a * 100 + k, p, sizeof p);
            if (a * 100 + k >= g_notes) snprintf(p, sizeof p, "Area 00/%s.md", "missing");
            if (k == 0) n += (size_t)snprintf(buf + n, cap - n, "    {\"id\": \"n%d\", \"type\": \"text\", \"text\": \"Canvas %d\", \"x\": 0, \"y\": 0, \"width\": 250, \"height\": 60}", k, a);
            else n += (size_t)snprintf(buf + n, cap - n, "%s    {\"id\": \"n%d\", \"type\": \"file\", \"file\": \"%s\", \"x\": %d, \"y\": %d, \"width\": 400, \"height\": 300}", ",\n", k, p, (k % 3) * 450, (k / 3) * 350 + 100);
        }
        n += (size_t)snprintf(buf + n, cap - n, "\n  ],\n  \"edges\": [\n");
        for (k = 1; k < nodes; k++)
            n += (size_t)snprintf(buf + n, cap - n, "%s    {\"id\": \"e%d\", \"fromNode\": \"n%d\", \"toNode\": \"n%d\"}", k > 1 ? ",\n" : "", k, k - 1, k);
        n += (size_t)snprintf(buf + n, cap - n, "\n  ]\n}\n");
        snprintf(rel, sizeof rel, "Canvases/Canvas %d.canvas", a);
        write_file(out, rel, buf, n);
    }
    printf("gen_vault: seed %llu, %d notes, %d wikilinks, %d tag uses, %d canvases, %u attachments\n", (unsigned long long)seed, g_notes, total_links, tag_uses, canvases, g_attach);
    (void)links_left;
    free(buf);
    return 0;
}
