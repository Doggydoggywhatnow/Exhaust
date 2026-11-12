#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_platform.h"
#include "exhaust_util.h"
#include "exhaust_vault.h"

struct ExVault {
    char *path, *id, *name;
    ExVaultOptions opt;
    char **excluded_copy;
    ExNode *root;
    ExNode **rows; size_t nrows; int *depths; int rows_valid;
};

static char *dup_str(const char *s) {
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    if (p) memcpy(p, s, n + 1);
    return p;
}

static char *join_rel(const char *a, const char *b) {
    size_t na = strlen(a), nb = strlen(b);
    char *p = malloc(na + nb + 2);
    if (!p) return NULL;
    memcpy(p, a, na);
    if (na) p[na++] = '/';
    memcpy(p + na, b, nb + 1);
    return p;
}

static int ieq_n(const char *a, const char *b, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    return 1;
}

/* ---- file kinds ---- */

ExFileKind ex_file_kind(const char *name) {
    static const struct { const char *ext; ExFileKind kind; } T[] = {
        {"md", EX_KIND_MARKDOWN}, {"base", EX_KIND_BASE}, {"canvas", EX_KIND_CANVAS},
        {"avif", EX_KIND_IMAGE}, {"bmp", EX_KIND_IMAGE}, {"gif", EX_KIND_IMAGE}, {"jpeg", EX_KIND_IMAGE}, {"jpg", EX_KIND_IMAGE},
        {"png", EX_KIND_IMAGE}, {"svg", EX_KIND_IMAGE}, {"webp", EX_KIND_IMAGE},
        {"flac", EX_KIND_AUDIO}, {"m4a", EX_KIND_AUDIO}, {"mp3", EX_KIND_AUDIO}, {"ogg", EX_KIND_AUDIO}, {"wav", EX_KIND_AUDIO},
        {"webm", EX_KIND_AUDIO}, {"3gp", EX_KIND_AUDIO},
        {"mkv", EX_KIND_VIDEO}, {"mov", EX_KIND_VIDEO}, {"mp4", EX_KIND_VIDEO}, {"ogv", EX_KIND_VIDEO},
        {"pdf", EX_KIND_PDF}};
    const char *dot = strrchr(name, '.');
    size_t i;
    if (!dot || dot == name) return EX_KIND_OTHER;
    for (i = 0; i < sizeof T / sizeof T[0]; i++)
        if (strlen(T[i].ext) == strlen(dot + 1) && ieq_n(dot + 1, T[i].ext, strlen(T[i].ext))) return T[i].kind;
    return EX_KIND_OTHER;
}

/* ---- names and globs ---- */

int ex_name_valid(const char *name) {
    static const char *const RESERVED[] = {"CON", "PRN", "AUX", "NUL"};
    size_t n = strlen(name), i, base;
    if (n == 0 || n > 255 || !strcmp(name, ".") || !strcmp(name, "..")) return 0;
    if (name[n - 1] == '.' || name[n - 1] == ' ' || name[0] == ' ') return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20 || c == 0x7F || strchr("/\\#^[]|:*?\"<>", c)) return 0;
    }
    if (strstr(name, "%%")) return 0;
    /* reserved Windows device names, with or without an extension */
    for (base = 0; name[base] && name[base] != '.'; base++) {}
    for (i = 0; i < sizeof RESERVED / sizeof RESERVED[0]; i++)
        if (base == 3 && ieq_n(name, RESERVED[i], 3)) return 0;
    if (base == 4 && (ieq_n(name, "COM", 3) || ieq_n(name, "LPT", 3)) && name[3] >= '1' && name[3] <= '9') return 0;
    return 1;
}

static int valid_rel(const char *rel) {
    const char *p = rel;
    if (rel[0] == '/' || strchr(rel, '\\') || strchr(rel, ':')) return 0;
    if (!*rel) return 1;
    while (*p) {
        const char *e = strchr(p, '/');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n == 0 || (n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.')) return 0;
        for (size_t i = 0; i < n; i++) if ((unsigned char)p[i] < 0x20) return 0;
        if (!e) break;
        p = e + 1;
    }
    return 1;
}

/* *, **, ? and literal characters; ignores case. */
static int glob_rec(const char *p, const char *t) {
    while (*p) {
        if (p[0] == '*' && p[1] == '*') {
            while (*p == '*') p++;
            if (!*p) return 1;
            for (; *t || 1; t++) { if (glob_rec(p, t)) return 1; if (!*t) break; }
            return 0;
        }
        if (*p == '*') {
            p++;
            if (!*p) return strchr(t, '/') == NULL;
            for (; ; t++) { if (glob_rec(p, t)) return 1; if (!*t || *t == '/') break; }
            return 0;
        }
        if (!*t) return 0;
        if (*p == '?') { if (*t == '/') return 0; }
        else if (tolower((unsigned char)*p) != tolower((unsigned char)*t)) return 0;
        p++; t++;
    }
    return *t == 0;
}

int ex_glob_match(const char *pattern, const char *text) { return glob_rec(pattern, text); }

static int matches_pattern(const char *pat, const char *rel) {
    size_t pl = strlen(pat);
    char *copy;
    int dir_only = pl > 0 && pat[pl - 1] == '/', ok = 0;
    const char *part;
    if (pl == 0) return 0;
    copy = dup_str(pat);
    if (!copy) return 0;
    if (dir_only) copy[pl - 1] = 0;
    if (copy[0] == '/') memmove(copy, copy + 1, strlen(copy)); /* anchored to the vault */
    if (strchr(copy, '/') != NULL) {
        /* a pattern with a slash matches the whole path, or the start of it (a folder and what is in it) */
        char *prefix = dup_str(rel);
        char *slash;
        if (!prefix) { free(copy); return 0; }
        ok = glob_rec(copy, prefix);
        while (!ok && (slash = strrchr(prefix, '/')) != NULL) { *slash = 0; ok = glob_rec(copy, prefix); }
        free(prefix);
    } else {
        /* no slash: the pattern names a file or folder anywhere (a folder takes its contents along) */
        for (part = rel; *part && !ok;) {
            const char *e = strchr(part, '/');
            size_t n = e ? (size_t)(e - part) : strlen(part);
            char *seg = malloc(n + 1);
            if (!seg) break;
            memcpy(seg, part, n);
            seg[n] = 0;
            ok = glob_rec(copy, seg);
            free(seg);
            if (!e) break;
            part = e + 1;
        }
    }
    free(copy);
    return ok;
}

int ex_patterns_match(const char *const *patterns, size_t n, const char *rel) {
    size_t i;
    for (i = 0; i < n; i++) if (matches_pattern(patterns[i], rel)) return 1;
    return 0;
}

int ex_vault_is_excluded(const ExVault *v, const char *rel) {
    size_t i;
    for (i = 0; i < v->opt.nexcluded; i++) if (matches_pattern(v->opt.excluded[i], rel)) return 1;
    return 0;
}

/* ---- tree ---- */

static void free_node(ExNode *n) {
    size_t i;
    if (!n) return;
    for (i = 0; i < n->nkids; i++) free_node(n->kids[i]);
    free(n->kids); free(n->name); free(n->rel);
    free(n);
}

typedef struct Names { char **v; int n, cap; } Names;

static SDL_EnumerationResult collect(void *ud, const char *dirname, const char *fname) {
    Names *nm = ud;
    (void)dirname;
    if (nm->n == nm->cap) {
        int nc = nm->cap ? nm->cap * 2 : 16;
        char **nv = realloc(nm->v, (size_t)nc * sizeof *nv);
        if (!nv) return SDL_ENUM_FAILURE;
        nm->v = nv;
        nm->cap = nc;
    }
    nm->v[nm->n] = dup_str(fname);
    return nm->v[nm->n] ? (nm->n++, SDL_ENUM_CONTINUE) : SDL_ENUM_FAILURE;
}

static int by_key_asc; /* sorting state for qsort (the tree is rescanned on the UI thread only) */
static ExSortBy by_key;

static int cmp_names_ci(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
        if (x != y) return x < y ? -1 : 1;
    }
    if (*a != *b) return *a ? 1 : -1;
    return 0;
}

static int cmp_nodes(const void *pa, const void *pb) {
    const ExNode *a = *(ExNode *const *)pa, *b = *(ExNode *const *)pb;
    int c = 0;
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;  /* folders first, whatever the order */
    if (by_key == EX_SORT_MODIFIED) c = a->mtime < b->mtime ? -1 : a->mtime > b->mtime;
    else if (by_key == EX_SORT_CREATED) c = a->ctime < b->ctime ? -1 : a->ctime > b->ctime;
    if (c != 0) return by_key_asc ? c : -c;
    c = cmp_names_ci(a->name, b->name);
    if (c == 0) c = strcmp(a->name, b->name);
    return (by_key == EX_SORT_NAME && !by_key_asc) ? -c : c; /* ties in the time orders are by name, ascending */
}

static void sort_children(ExNode *n, ExSortBy by, int desc) {
    size_t i;
    by_key = by;
    by_key_asc = !desc;
    if (n->nkids > 1) qsort(n->kids, n->nkids, sizeof *n->kids, cmp_nodes);
    for (i = 0; i < n->nkids; i++) sort_children(n->kids[i], by, desc);
}

#define MAX_DEPTH 40

/* chain: canonical paths of the folders above, to spot symbolic link loops */
static ExNode *scan(ExVault *v, const char *abs, const char *rel, const char *name, int depth, char **chain) {
    ExNode *n = calloc(1, sizeof *n);
    Names nm = {0};
    int i;
    ExPathInfo info;
    if (!n) return NULL;
    n->name = dup_str(name);
    n->rel = dup_str(rel);
    n->is_dir = 1;
    if (ex_platform_path_info(abs, &info) == 0) { n->size = info.size; n->mtime = info.mtime; n->ctime = info.ctime; }
    if (!n->name || !n->rel) { free_node(n); return NULL; }
    if (depth >= MAX_DEPTH) { n->link_loop = 1; return n; }
    if (!SDL_EnumerateDirectory(abs, collect, &nm)) { for (i = 0; i < nm.n; i++) free(nm.v[i]); free(nm.v); return n; }
    for (i = 0; i < nm.n; i++) {
        const char *fn = nm.v[i];
        char *child_abs, *child_rel;
        ExPathInfo ci;
        ExNode *c = NULL;
        if (fn[0] == '.') { free(nm.v[i]); continue; } /* .exhaust, .trash, .git ... are not shown */
        child_abs = join_rel(abs, fn);
        child_rel = rel[0] ? join_rel(rel, fn) : dup_str(fn);
        if (child_abs && child_rel && ex_platform_path_info(child_abs, &ci) == 0) { /* a broken link is skipped */
            if (ci.is_dir) {
                char *real = ex_platform_realpath(child_abs);
                int loop = 0, d;
                for (d = 0; real && chain[d]; d++) if (strcmp(chain[d], real) == 0) loop = 1;
                if (loop || !real) {
                    c = calloc(1, sizeof *c);
                    if (c) { c->name = dup_str(fn); c->rel = dup_str(child_rel); c->is_dir = 1; c->link_loop = 1; c->mtime = ci.mtime; c->ctime = ci.ctime; }
                } else {
                    chain[depth + 1] = real;
                    chain[depth + 2] = NULL;
                    c = scan(v, child_abs, child_rel, fn, depth + 1, chain);
                    chain[depth + 1] = NULL;
                }
                if (loop || !c || !real) free(real); else free(real);
            } else if (ci.is_file && (v->opt.show_all_files || ex_file_kind(fn) != EX_KIND_OTHER)) {
                c = calloc(1, sizeof *c);
                if (c) { c->name = dup_str(fn); c->rel = dup_str(child_rel); c->kind = ex_file_kind(fn); c->size = ci.size; c->mtime = ci.mtime; c->ctime = ci.ctime; }
            }
        }
        if (c) {
            ExNode **nk = realloc(n->kids, (n->nkids + 1) * sizeof *nk);
            if (nk) { n->kids = nk; c->parent = n; c->excluded = ex_vault_is_excluded(v, c->rel); n->kids[n->nkids++] = c; }
            else free_node(c);
        }
        free(child_abs); free(child_rel); free(nm.v[i]);
    }
    free(nm.v);
    return n;
}

/* Copies the expanded state of the old tree onto the new one. */
static void carry_expanded(const ExNode *old, ExNode *fresh) {
    size_t i, j;
    if (!old || !fresh) return;
    fresh->expanded = old->expanded;
    for (i = 0; i < fresh->nkids; i++)
        for (j = 0; j < old->nkids; j++)
            if (strcmp(fresh->kids[i]->name, old->kids[j]->name) == 0) { carry_expanded(old->kids[j], fresh->kids[i]); break; }
}

int ex_vault_rescan(ExVault *v) {
    char *chain[MAX_DEPTH + 4] = {0};
    ExNode *fresh;
    chain[0] = v->path;
    fresh = scan(v, v->path, "", v->name, 0, chain);
    if (!fresh) return -1;
    sort_children(fresh, v->opt.sort_by, v->opt.sort_descending);
    carry_expanded(v->root, fresh);
    if (!v->root) fresh->expanded = 1;
    free_node(v->root);
    v->root = fresh;
    v->rows_valid = 0;
    return 0;
}

ExVault *ex_vault_open(const char *folder) {
    ExVault *v = calloc(1, sizeof *v);
    ExPathInfo info;
    uint8_t digest[32];
    char hex[65], *slash;
    if (!v) return NULL;
    v->path = ex_platform_realpath(folder);
    if (!v->path || ex_platform_path_info(v->path, &info) != 0 || !info.is_dir) { free(v->path); free(v); return NULL; }
    ex_sha256(v->path, strlen(v->path), digest);
    ex_hex(digest, 16, hex);
    v->id = dup_str(hex);
    slash = strrchr(v->path, '/');
    v->name = dup_str(slash && slash[1] ? slash + 1 : v->path);
    v->opt.trash_mode = EX_TRASH_SYSTEM;
    if (!v->id || !v->name || ex_vault_rescan(v) != 0) { ex_vault_close(v); return NULL; }
    return v;
}

static void free_excluded(ExVault *v) {
    size_t i;
    for (i = 0; i < v->opt.nexcluded; i++) free(v->excluded_copy[i]);
    free(v->excluded_copy);
    v->excluded_copy = NULL;
    v->opt.excluded = NULL;
    v->opt.nexcluded = 0;
}

void ex_vault_close(ExVault *v) {
    if (!v) return;
    free_node(v->root);
    free_excluded(v);
    free(v->rows); free(v->depths);
    free(v->path); free(v->id); free(v->name);
    free(v);
}

const char *ex_vault_path(const ExVault *v) { return v->path; }
const char *ex_vault_id(const ExVault *v) { return v->id; }
const char *ex_vault_name(const ExVault *v) { return v->name; }

void ex_vault_set_options(ExVault *v, const ExVaultOptions *o) {
    char **copy = NULL;
    size_t i;
    if (o->nexcluded) {
        copy = calloc(o->nexcluded, sizeof *copy);
        for (i = 0; copy && i < o->nexcluded; i++) copy[i] = dup_str(o->excluded[i]);
    }
    free_excluded(v);
    v->opt = *o;
    v->excluded_copy = copy;
    v->opt.excluded = (const char *const *)copy;
    if (!copy) v->opt.nexcluded = 0;
    ex_vault_rescan(v);
}

void ex_vault_get_options(const ExVault *v, ExVaultOptions *o) { *o = v->opt; }

const ExNode *ex_vault_root(const ExVault *v) { return v->root; }

const ExNode *ex_vault_find(const ExVault *v, const char *rel) {
    const ExNode *n = v->root;
    const char *p = rel;
    if (!*rel) return n;
    while (n && *p) {
        const char *e = strchr(p, '/');
        size_t len = e ? (size_t)(e - p) : strlen(p), i;
        const ExNode *next = NULL;
        for (i = 0; i < n->nkids; i++)
            if (strlen(n->kids[i]->name) == len && strncmp(n->kids[i]->name, p, len) == 0) { next = n->kids[i]; break; }
        n = next;
        if (!e) break;
        p = e + 1;
    }
    return n;
}

char *ex_vault_abs_path(const ExVault *v, const char *rel) { return rel[0] ? join_rel(v->path, rel) : dup_str(v->path); }

/* ---- rows ---- */

static void flatten(ExVault *v, ExNode *n, int depth, size_t *count, size_t cap) {
    size_t i;
    for (i = 0; i < n->nkids; i++) {
        ExNode *c = n->kids[i];
        if (*count < cap) { v->rows[*count] = c; v->depths[*count] = depth; }
        (*count)++;
        if (c->is_dir && c->expanded && !c->link_loop) flatten(v, c, depth + 1, count, cap);
    }
}

static size_t count_nodes(const ExNode *n) {
    size_t i, c = 1;
    for (i = 0; i < n->nkids; i++) c += count_nodes(n->kids[i]);
    return c;
}

static void build_rows(ExVault *v) {
    size_t cap = count_nodes(v->root), count = 0;
    free(v->rows); free(v->depths);
    v->rows = malloc(cap * sizeof *v->rows);
    v->depths = malloc(cap * sizeof *v->depths);
    if (v->rows && v->depths) flatten(v, v->root, 0, &count, cap);
    v->nrows = v->rows && v->depths ? count : 0;
    v->rows_valid = 1;
}

size_t ex_vault_row_count(ExVault *v) { if (!v->rows_valid) build_rows(v); return v->nrows; }

const ExNode *ex_vault_row(ExVault *v, size_t i, int *depth) {
    if (!v->rows_valid) build_rows(v);
    if (i >= v->nrows) return NULL;
    if (depth) *depth = v->depths[i];
    return v->rows[i];
}

void ex_vault_toggle(ExVault *v, const char *rel) {
    ExNode *n = (ExNode *)ex_vault_find(v, rel);
    if (n && n->is_dir && !n->link_loop) { n->expanded = !n->expanded; v->rows_valid = 0; }
}

static void set_all(ExNode *n, int open) {
    size_t i;
    if (n->is_dir) n->expanded = open;
    for (i = 0; i < n->nkids; i++) set_all(n->kids[i], open);
}

void ex_vault_expand_all(ExVault *v) {
    set_all(v->root, 1);
    v->rows_valid = 0;
}

void ex_vault_collapse_all(ExVault *v) {
    set_all(v->root, 0);
    v->root->expanded = 1; /* the vault's own children stay listed */
    v->rows_valid = 0;
}

long ex_vault_reveal(ExVault *v, const char *rel) {
    ExNode *n = (ExNode *)ex_vault_find(v, rel), *p;
    size_t i, rows;
    if (!n) return -1;
    for (p = n->parent; p; p = p->parent) p->expanded = 1;
    v->rows_valid = 0;
    rows = ex_vault_row_count(v);
    for (i = 0; i < rows; i++) if (v->rows[i] == n) return (long)i;
    return -1;
}

/* ---- actions ---- */

static char *parent_of(const char *rel) {
    const char *slash = strrchr(rel, '/');
    char *p;
    if (!slash) return dup_str("");
    p = malloc((size_t)(slash - rel) + 1);
    if (p) { memcpy(p, rel, (size_t)(slash - rel)); p[slash - rel] = 0; }
    return p;
}

static const char *base_of(const char *rel) { const char *s = strrchr(rel, '/'); return s ? s + 1 : rel; }

static int is_dir_rel(const ExVault *v, const char *rel) {
    char *abs = ex_vault_abs_path(v, rel);
    ExPathInfo info;
    int ok = abs && ex_platform_path_info(abs, &info) == 0 && info.is_dir;
    free(abs);
    return ok;
}

static ExVaultResult finish(ExVault *v, const char *rel, char **out) {
    if (out) *out = dup_str(rel);
    ex_vault_rescan(v);
    return EX_VAULT_OK;
}

ExVaultResult ex_vault_create_note(ExVault *v, const char *parent_rel, const char *name, char **out_rel) {
    char fname[300], *rel, *abs;
    FILE *f;
    if (!valid_rel(parent_rel) || !ex_name_valid(name)) return EX_VAULT_INVALID;
    if (!is_dir_rel(v, parent_rel)) return EX_VAULT_NOT_FOUND;
    snprintf(fname, sizeof fname, "%s%s", name, strrchr(name, '.') && strrchr(name, '.') != name ? "" : ".md");
    if (!ex_name_valid(fname)) return EX_VAULT_INVALID;
    rel = parent_rel[0] ? join_rel(parent_rel, fname) : dup_str(fname);
    abs = rel ? ex_vault_abs_path(v, rel) : NULL;
    if (!abs) { free(rel); return EX_VAULT_IO; }
    f = fopen(abs, "wbx"); /* "x": fails if the file exists, so nothing is ever replaced */
    if (!f) { ExPathInfo i; ExVaultResult r = ex_platform_path_info(abs, &i) == 0 ? EX_VAULT_EXISTS : EX_VAULT_IO; free(abs); free(rel); return r; }
    fclose(f);
    free(abs);
    { ExVaultResult r = finish(v, rel, out_rel); free(rel); return r; }
}

ExVaultResult ex_vault_create_folder(ExVault *v, const char *parent_rel, const char *name, char **out_rel) {
    char *rel, *abs;
    ExPathInfo info;
    if (!valid_rel(parent_rel) || !ex_name_valid(name)) return EX_VAULT_INVALID;
    if (!is_dir_rel(v, parent_rel)) return EX_VAULT_NOT_FOUND;
    rel = parent_rel[0] ? join_rel(parent_rel, name) : dup_str(name);
    abs = rel ? ex_vault_abs_path(v, rel) : NULL;
    if (!abs) { free(rel); return EX_VAULT_IO; }
    if (ex_platform_path_info(abs, &info) == 0) { free(abs); free(rel); return EX_VAULT_EXISTS; }
    if (!SDL_CreateDirectory(abs)) { free(abs); free(rel); return EX_VAULT_IO; }
    free(abs);
    { ExVaultResult r = finish(v, rel, out_rel); free(rel); return r; }
}

static ExVaultResult do_rename(ExVault *v, const char *rel, const char *new_parent, const char *new_name, char **out) {
    char *from = ex_vault_abs_path(v, rel), *new_rel, *to;
    ExVaultResult res = EX_VAULT_IO;
    ExPathInfo info;
    new_rel = new_parent[0] ? join_rel(new_parent, new_name) : dup_str(new_name);
    to = new_rel ? ex_vault_abs_path(v, new_rel) : NULL;
    if (!from || !to) goto done;
    if (strcmp(rel, new_rel) == 0) { res = EX_VAULT_OK; goto done; }
    if (ex_platform_path_info(to, &info) == 0) {
        /* the same file under another capitalization (case-insensitive folders): go through a temporary name */
        char *real_from = ex_platform_realpath(from), *real_to = ex_platform_realpath(to);
        int same = real_from && real_to && strcmp(real_from, real_to) == 0 && strcmp(from, to) != 0 && cmp_names_ci(rel, new_rel) == 0;
        free(real_from); free(real_to);
        if (same) {
            size_t n = strlen(from) + 32;
            char *tmp = malloc(n);
            if (tmp) {
                snprintf(tmp, n, "%s.exhaust-rename-%d", from, (int)(SDL_GetTicks() & 0xffff));
                if (ex_platform_rename_noreplace(from, tmp) == 0) {
                    if (ex_platform_rename_noreplace(tmp, to) == 0) res = EX_VAULT_OK;
                    else ex_platform_rename_noreplace(tmp, from); /* put it back */
                }
                free(tmp);
            }
        } else res = EX_VAULT_EXISTS;
        goto done;
    }
    if (ex_platform_rename_noreplace(from, to) == 0) res = EX_VAULT_OK;
done:
    if (res == EX_VAULT_OK) res = finish(v, new_rel, out);
    free(from); free(to); free(new_rel);
    return res;
}

ExVaultResult ex_vault_rename(ExVault *v, const char *rel, const char *new_name, char **out_rel) {
    char *parent;
    ExVaultResult r;
    if (!valid_rel(rel) || !rel[0] || !ex_name_valid(new_name)) return EX_VAULT_INVALID;
    if (!ex_vault_find(v, rel)) return EX_VAULT_NOT_FOUND;
    parent = parent_of(rel);
    if (!parent) return EX_VAULT_IO;
    r = do_rename(v, rel, parent, new_name, out_rel);
    free(parent);
    return r;
}

ExVaultResult ex_vault_move(ExVault *v, const char *rel, const char *new_parent_rel, char **out_rel) {
    size_t n;
    if (!valid_rel(rel) || !rel[0] || !valid_rel(new_parent_rel)) return EX_VAULT_INVALID;
    if (!ex_vault_find(v, rel)) return EX_VAULT_NOT_FOUND;
    if (!is_dir_rel(v, new_parent_rel)) return EX_VAULT_NOT_FOUND;
    n = strlen(rel);
    /* a folder cannot go into itself or something inside it */
    if (strncmp(new_parent_rel, rel, n) == 0 && (new_parent_rel[n] == 0 || new_parent_rel[n] == '/')) return EX_VAULT_INVALID;
    return do_rename(v, rel, new_parent_rel, base_of(rel), out_rel);
}

static ExVaultResult mkparents(const ExVault *v, const char *rel_dir) {
    char *abs = ex_vault_abs_path(v, rel_dir);
    int ok = abs && SDL_CreateDirectory(abs);
    free(abs);
    return ok ? EX_VAULT_OK : EX_VAULT_IO;
}

ExVaultResult ex_vault_delete(ExVault *v, const char *rel) {
    char *abs;
    ExVaultResult res = EX_VAULT_IO;
    if (!valid_rel(rel) || !rel[0]) return EX_VAULT_INVALID;
    if (!ex_vault_find(v, rel)) return EX_VAULT_NOT_FOUND;
    abs = ex_vault_abs_path(v, rel);
    if (!abs) return EX_VAULT_IO;
    switch (v->opt.trash_mode) {
    case EX_TRASH_PERMANENT:
        if (ex_platform_remove_tree(abs) == 0) res = EX_VAULT_OK;
        break;
    case EX_TRASH_VAULT: {
        /* into .trash, keeping the path; a name already there gets a number */
        char *dir = parent_of(rel), *trash_dir = NULL, *target_rel = NULL, *target_abs = NULL;
        const char *base = base_of(rel);
        int k;
        if (!dir) break;
        trash_dir = dir[0] ? join_rel(".trash", dir) : dup_str(".trash");
        if (!trash_dir || mkparents(v, trash_dir) != EX_VAULT_OK) { free(dir); free(trash_dir); break; }
        for (k = 0; k < 10000 && res != EX_VAULT_OK; k++) {
            char name[300];
            const char *dot = strrchr(base, '.');
            if (k == 0) snprintf(name, sizeof name, "%s", base);
            else if (dot && dot != base) snprintf(name, sizeof name, "%.*s %d%s", (int)(dot - base), base, k + 1, dot);
            else snprintf(name, sizeof name, "%s %d", base, k + 1);
            free(target_rel); free(target_abs);
            target_rel = join_rel(trash_dir, name);
            target_abs = target_rel ? ex_vault_abs_path(v, target_rel) : NULL;
            if (target_abs && ex_platform_rename_noreplace(abs, target_abs) == 0) res = EX_VAULT_OK;
        }
        free(dir); free(trash_dir); free(target_rel); free(target_abs);
        break;
    }
    default:
        if (ex_platform_trash_system(abs) == 0) res = EX_VAULT_OK;
        break;
    }
    free(abs);
    if (res == EX_VAULT_OK) ex_vault_rescan(v);
    return res;
}

int ex_vault_open_default(const ExVault *v, const char *rel) {
    char *abs = valid_rel(rel) ? ex_vault_abs_path(v, rel) : NULL;
    int rc = abs ? ex_platform_open_default(abs) : -1;
    free(abs);
    return rc;
}

int ex_vault_show_in_file_manager(const ExVault *v, const char *rel) {
    char *abs = valid_rel(rel) ? ex_vault_abs_path(v, rel) : NULL;
    int rc = abs ? ex_platform_show_in_file_manager(abs) : -1;
    free(abs);
    return rc;
}

int ex_vault_copy_path(const ExVault *v, const char *rel, int absolute) {
    char *text = absolute ? ex_vault_abs_path(v, rel) : dup_str(rel);
    int rc = text ? ex_clipboard_set_text(text) : -1;
    free(text);
    return rc;
}
