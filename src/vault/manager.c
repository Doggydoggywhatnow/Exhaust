#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_ini.h"
#include "exhaust_platform.h"
#include "exhaust_vault.h"
#include "exhaust_vaultmgr.h"

struct ExVaultManager {
    ExRegistry *reg;
    char *reg_path;          /* NULL: the standard file */
    char *data_root;
    unsigned long revision;
    size_t *order;           /* indexes into the registry, most recently opened first */
    size_t norder;
    int order_for;           /* revision the order was computed for */
};

static char *dup_str(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }

ExVaultManager *ex_vaultmgr_create(const char *registry_path, const char *data_root) {
    ExVaultManager *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->reg_path = registry_path ? dup_str(registry_path) : NULL;
    if (data_root) m->data_root = dup_str(data_root);
    else {
        char *r = ex_platform_local_data_dir();
        m->data_root = r;
    }
    m->reg = ex_registry_load(m->reg_path);
    m->order_for = -1;
    if (!m->reg) { ex_vaultmgr_free(m); return NULL; }
    return m;
}

void ex_vaultmgr_free(ExVaultManager *m) {
    if (!m) return;
    ex_registry_free(m->reg);
    free(m->reg_path); free(m->data_root); free(m->order);
    free(m);
}

unsigned long ex_vaultmgr_revision(const ExVaultManager *m) { return m->revision; }

static void changed(ExVaultManager *m) { m->revision++; }

static int save(ExVaultManager *m) { return ex_registry_save(m->reg); }

static void compute_order(ExVaultManager *m) {
    size_t n = ex_registry_count(m->reg), i, k;
    size_t *o = realloc(m->order, (n ? n : 1) * sizeof *o);
    if (!o) return;
    m->order = o;
    for (i = 0; i < n; i++) o[i] = i;
    for (i = 1; i < n; i++) {                       /* insertion sort: few vaults; stable; by last opened, newest first */
        ExRegistryEntry a, b;
        size_t cur = o[i];
        ex_registry_get(m->reg, cur, &a);
        for (k = i; k > 0; k--) {
            ex_registry_get(m->reg, o[k - 1], &b);
            if (b.opened >= a.opened) break;
            o[k] = o[k - 1];
        }
        o[k] = cur;
    }
    m->norder = n;
    m->order_for = (int)m->revision;
}

size_t ex_vaultmgr_count(const ExVaultManager *m) { return ex_registry_count(m->reg); }

int ex_vaultmgr_get(const ExVaultManager *cm, size_t i, ExVaultInfo *out) {
    ExVaultManager *m = (ExVaultManager *)cm;       /* the order cache is not part of the observable state */
    ExRegistryEntry e;
    ExPathInfo info;
    if (m->order_for != (int)m->revision || m->norder != ex_registry_count(m->reg)) compute_order(m);
    if (i >= m->norder || ex_registry_get(m->reg, m->order[i], &e) != 0) return -1;
    out->id = e.id; out->name = e.name; out->path = e.path; out->opened = e.opened;
    out->exists = ex_platform_path_info(e.path, &info) == 0 && info.is_dir;
    return 0;
}

const char *ex_vaultmgr_last(const ExVaultManager *m) { return ex_registry_last(m->reg); }

int ex_vault_folder_name_valid(const char *name) {
    static const char *const reserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                                           "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    size_t n = strlen(name), i, stem;
    char up[16];
    if (n == 0 || n > 200 || name[0] == '.' || name[0] == ' ' || name[n - 1] == ' ' || name[n - 1] == '.') return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 32 || strchr("/\\:*?\"<>|", c)) return 0;
    }
    for (stem = 0; stem < n && name[stem] != '.'; stem++) {}
    if (stem < sizeof up) {
        for (i = 0; i < stem; i++) up[i] = (char)toupper((unsigned char)name[i]);
        up[stem] = 0;
        for (i = 0; i < sizeof reserved / sizeof reserved[0]; i++) if (strcmp(up, reserved[i]) == 0) return 0;
    }
    return 1;
}

static ExVmResult add_folder(ExVaultManager *m, const char *folder, const char **out_id) {
    ExPathInfo info;
    const char *id;
    if (ex_platform_path_info(folder, &info) != 0) return EX_VM_NOT_FOUND;
    if (!info.is_dir) return EX_VM_NOT_A_FOLDER;
    id = ex_registry_add(m->reg, folder);
    if (!id) return EX_VM_IO;
    if (save(m) != 0) return EX_VM_IO;
    changed(m);
    if (out_id) *out_id = id;
    return EX_VM_OK;
}

ExVmResult ex_vaultmgr_open_folder(ExVaultManager *m, const char *folder, const char **out_id) { return add_folder(m, folder, out_id); }

ExVmResult ex_vaultmgr_create_vault(ExVaultManager *m, const char *parent_folder, const char *name, const char **out_id) {
    ExPathInfo info;
    char *path;
    ExVmResult r;
    if (!ex_vault_folder_name_valid(name)) return EX_VM_INVALID_NAME;
    if (ex_platform_path_info(parent_folder, &info) != 0) return EX_VM_NOT_FOUND;
    if (!info.is_dir) return EX_VM_NOT_A_FOLDER;
    path = malloc(strlen(parent_folder) + strlen(name) + 2);
    if (!path) return EX_VM_IO;
    sprintf(path, "%s/%s", parent_folder, name);
    if (ex_platform_path_info(path, &info) == 0) { free(path); return EX_VM_EXISTS; }     /* never an existing folder or file */
    if (ex_platform_make_dirs(path) != 0) { free(path); return EX_VM_IO; }
    r = add_folder(m, path, out_id);
    free(path);
    return r;
}

ExVmResult ex_vaultmgr_rename(ExVaultManager *m, const char *id, const char *new_name) {
    size_t i;
    ExRegistryEntry e;
    int found = 0;
    for (i = 0; i < ex_registry_count(m->reg); i++) if (ex_registry_get(m->reg, i, &e) == 0 && strcmp(e.id, id) == 0) found = 1;
    if (!found) return EX_VM_NOT_FOUND;
    if (!new_name || !new_name[0] || strlen(new_name) > 200 || strpbrk(new_name, "\r\n")) return EX_VM_INVALID_NAME;
    if (ex_registry_rename(m->reg, id, new_name) != 0 || save(m) != 0) return EX_VM_IO;
    changed(m);
    return EX_VM_OK;
}

ExVmResult ex_vaultmgr_remove(ExVaultManager *m, const char *id) {
    if (ex_registry_remove(m->reg, id) != 0) return EX_VM_NOT_FOUND;
    if (save(m) != 0) return EX_VM_IO;
    changed(m);
    return EX_VM_OK;
}

ExVmResult ex_vaultmgr_touch(ExVaultManager *m, const char *id, int64_t when) {
    if (ex_registry_touch(m->reg, id, when) != 0) return EX_VM_NOT_FOUND;
    if (save(m) != 0) return EX_VM_IO;
    changed(m);
    return EX_VM_OK;
}

static int is_inside(const char *folder, const char *parent) {
    size_t n = strlen(parent);
    return strncmp(folder, parent, n) == 0 && (folder[n] == '/' || folder[n] == 0);
}

ExVmResult ex_vaultmgr_move(ExVaultManager *m, const char *id_in, const char *new_parent, const char **out_id) {
    size_t i;
    char id_copy[64];
    const char *id = id_copy;       /* the caller's id may point into the list, which changes below */
    ExRegistryEntry e;
    char *old_path = NULL, *parent_real, *target = NULL, *old_data = NULL, *new_data = NULL;
    const char *base, *new_id;
    ExPathInfo info;
    ExVmResult r = EX_VM_OK;
    snprintf(id_copy, sizeof id_copy, "%s", id_in);
    for (i = 0; i < ex_registry_count(m->reg); i++)
        if (ex_registry_get(m->reg, i, &e) == 0 && strcmp(e.id, id) == 0) { old_path = dup_str(e.path); break; }
    if (!old_path) return EX_VM_NOT_FOUND;
    if (ex_platform_path_info(old_path, &info) != 0 || !info.is_dir) { free(old_path); return EX_VM_NOT_FOUND; }
    parent_real = ex_platform_realpath(new_parent);
    if (!parent_real || ex_platform_path_info(parent_real, &info) != 0 || !info.is_dir) { free(old_path); free(parent_real); return EX_VM_NOT_FOUND; }
    if (is_inside(parent_real, old_path)) { free(old_path); free(parent_real); return EX_VM_INTO_ITSELF; }
    base = strrchr(old_path, '/');
    base = base && base[1] ? base + 1 : old_path;
    target = malloc(strlen(parent_real) + strlen(base) + 2);
    if (!target) { free(old_path); free(parent_real); return EX_VM_IO; }
    sprintf(target, "%s/%s", parent_real, base);
    if (strcmp(target, old_path) == 0) { r = EX_VM_EXISTS; goto done; }
    if (ex_platform_path_info(target, &info) == 0) { r = EX_VM_EXISTS; goto done; }          /* never replaces anything */
    if (ex_platform_rename_noreplace(old_path, target) != 0) { r = EX_VM_IO; goto done; }
    new_id = ex_registry_move(m->reg, id, target);
    if (!new_id) {                                                                            /* put the folder back */
        ex_platform_rename_noreplace(target, old_path);
        r = EX_VM_IO;
        goto done;
    }
    if (out_id) *out_id = new_id;
    /* the journal and the session are keyed by the id, which comes from the path: they follow the vault */
    if (m->data_root && strcmp(new_id, id) != 0) {
        old_data = malloc(strlen(m->data_root) + strlen(id) + 16);
        new_data = malloc(strlen(m->data_root) + strlen(new_id) + 16);
        if (old_data && new_data) {
            sprintf(old_data, "%s/vaults/%s", m->data_root, id);
            sprintf(new_data, "%s/vaults/%s", m->data_root, new_id);
            if (ex_platform_path_info(old_data, &info) == 0) {
                if (ex_platform_path_info(new_data, &info) == 0 || ex_platform_rename_noreplace(old_data, new_data) != 0) r = EX_VM_DATA_NOT_MOVED;
            }
        } else r = EX_VM_DATA_NOT_MOVED;
    }
    if (save(m) != 0) r = EX_VM_IO;
    changed(m);
done:
    free(old_path); free(parent_real); free(target); free(old_data); free(new_data);
    return r;
}

/* ---- transferring settings ---- */

static char *vault_path_of(ExVaultManager *m, const char *id) {
    size_t i;
    ExRegistryEntry e;
    for (i = 0; i < ex_registry_count(m->reg); i++) if (ex_registry_get(m->reg, i, &e) == 0 && strcmp(e.id, id) == 0) return dup_str(e.path);
    return NULL;
}

static char *join_path(const char *a, const char *b) {
    char *p = malloc(strlen(a) + strlen(b) + 2);
    if (p) sprintf(p, "%s/%s", a, b);
    return p;
}

/* Adds every key of `src_text` that `dst_text` does not set, by patching the target's text. */
static int merge_ini(const char *src_text, size_t src_len, char **dst_text, size_t *dst_len, ExTransferResult *res) {
    ExIni *src = ex_ini_parse(src_text, src_len), *dst = ex_ini_parse(*dst_text, *dst_len);
    size_t i, k;
    if (!src || !dst) { ex_ini_free(src); ex_ini_free(dst); return -1; }
    for (i = 0; i < ex_ini_section_count(src); i++) {
        const char *sec = ex_ini_section_name(src, i);
        for (k = 0; k < ex_ini_key_count(src, sec); k++) {
            const char *key = ex_ini_key_name(src, sec, k), *val = ex_ini_get(src, sec, key);
            char *patched;
            size_t plen = 0;
            if (!val) continue;
            if (ex_ini_get(dst, sec, key)) { res->skipped++; continue; }
            patched = ex_ini_patch(*dst_text, *dst_len, sec, key, val, &plen);
            if (!patched) { ex_ini_free(src); ex_ini_free(dst); return -1; }
            free(*dst_text);
            *dst_text = patched;
            *dst_len = plen;
            ex_ini_free(dst);
            dst = ex_ini_parse(patched, plen);
            if (!dst) { ex_ini_free(src); return -1; }
            res->keys_added++;
        }
    }
    ex_ini_free(src);
    ex_ini_free(dst);
    return 0;
}

static int transfer_ini_file(const char *from_dir, const char *to_dir, const char *name, ExTransferResult *res) {
    char *src_path = join_path(from_dir, name), *dst_path = join_path(to_dir, name), *src, *dst;
    size_t sl = 0, dl = 0;
    int rc = 0;
    if (!src_path || !dst_path) { free(src_path); free(dst_path); return -1; }
    src = ex_platform_read_file(src_path, &sl);
    if (src) {
        dst = ex_platform_read_file(dst_path, &dl);
        if (!dst) {                                                          /* nothing there: a plain copy */
            ex_platform_make_dirs(to_dir);
            rc = ex_platform_write_file_atomic(dst_path, src, sl);
            if (!rc) {
                ExIni *ini = ex_ini_parse(src, sl);
                size_t i;
                if (ini) { for (i = 0; i < ex_ini_section_count(ini); i++) res->keys_added += (int)ex_ini_key_count(ini, ex_ini_section_name(ini, i)); ex_ini_free(ini); }
            }
        } else {
            rc = merge_ini(src, sl, &dst, &dl, res);
            if (!rc) {
                ex_platform_make_dirs(to_dir);
                rc = ex_platform_write_file_atomic(dst_path, dst, dl);
            }
            free(dst);
        }
        free(src);
    }
    free(src_path); free(dst_path);
    return rc;
}

typedef struct CopyCtx { const char *from, *to; ExTransferResult *res; int failed; } CopyCtx;

static int copy_visit(const char *path, int is_dir, int64_t mtime, void *user) {
    CopyCtx *c = user;
    const char *rel = path + strlen(c->from);
    char *dst, *data;
    size_t len = 0;
    ExPathInfo info;
    (void)mtime;
    if (is_dir) return 0;
    while (*rel == '/') rel++;
    dst = join_path(c->to, rel);
    if (!dst) { c->failed = 1; return 1; }
    if (ex_platform_path_info(dst, &info) == 0) { c->res->skipped++; free(dst); return 0; }    /* never replaces */
    data = ex_platform_read_file(path, &len);
    if (data) {
        char *slash = strrchr(dst, '/');
        if (slash) { *slash = 0; ex_platform_make_dirs(dst); *slash = '/'; }
        if (ex_platform_write_file_atomic(dst, data, len) == 0) c->res->files_copied++; else c->failed = 1;
        free(data);
    }
    free(dst);
    return 0;
}

ExVmResult ex_vaultmgr_transfer_settings(ExVaultManager *m, const char *from_id, const char *to_id, ExTransferResult *out) {
    char from_id_copy[64], to_id_copy[64];
    char *from = NULL, *to = NULL, *from_cfg = NULL, *to_cfg = NULL, *a = NULL, *b = NULL;
    ExTransferResult local, *res = out ? out : &local;
    ExPathInfo info;
    ExVmResult r = EX_VM_OK;
    CopyCtx ctx;
    const char *const dirs[] = {"themes", "snippets"};
    size_t d;
    memset(res, 0, sizeof *res);
    snprintf(from_id_copy, sizeof from_id_copy, "%s", from_id);
    snprintf(to_id_copy, sizeof to_id_copy, "%s", to_id);
    if (strcmp(from_id_copy, to_id_copy) == 0) return EX_VM_SAME_VAULT;
    from = vault_path_of(m, from_id_copy);
    to = vault_path_of(m, to_id_copy);
    if (!from || !to || ex_platform_path_info(from, &info) != 0 || !info.is_dir || ex_platform_path_info(to, &info) != 0 || !info.is_dir) { r = EX_VM_NOT_FOUND; goto done; }
    from_cfg = join_path(from, ".exhaust");
    to_cfg = join_path(to, ".exhaust");
    if (!from_cfg || !to_cfg) { r = EX_VM_IO; goto done; }
    if (ex_platform_path_info(from_cfg, &info) != 0) goto done;                    /* nothing to transfer */
    if (transfer_ini_file(from_cfg, to_cfg, "settings.ini", res) != 0 || transfer_ini_file(from_cfg, to_cfg, "hotkeys.ini", res) != 0) { r = EX_VM_IO; goto done; }
    for (d = 0; d < 2; d++) {
        a = join_path(from_cfg, dirs[d]);
        b = join_path(to_cfg, dirs[d]);
        if (a && b && ex_platform_path_info(a, &info) == 0 && info.is_dir) {
            ctx.from = a; ctx.to = b; ctx.res = res; ctx.failed = 0;
            ex_platform_walk(a, copy_visit, &ctx);
            if (ctx.failed) r = EX_VM_IO;
        }
        free(a); free(b); a = b = NULL;
    }
done:
    free(from); free(to); free(from_cfg); free(to_cfg);
    return r;
}

const char *ex_vm_message(ExVmResult r) {
    switch (r) {
    case EX_VM_OK: return "Done.";
    case EX_VM_INVALID_NAME: return "That name cannot be used for a folder. Avoid / \\ : * ? \" < > | and a leading dot, and do not end it with a space or a dot.";
    case EX_VM_EXISTS: return "Something with that name is already there, so nothing was changed.";
    case EX_VM_NOT_FOUND: return "That vault or folder was not found.";
    case EX_VM_NOT_A_FOLDER: return "That is not a folder.";
    case EX_VM_INTO_ITSELF: return "A vault cannot be moved into itself.";
    case EX_VM_IO: return "The operating system refused. A vault can only be moved to a folder on the same drive.";
    case EX_VM_SAME_VAULT: return "Choose a different vault.";
    case EX_VM_DATA_NOT_MOVED: return "The vault moved, but its recovery data could not follow it and was left where it was; nothing was deleted.";
    }
    return "";
}
