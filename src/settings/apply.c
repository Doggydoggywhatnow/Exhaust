/* Turns settings into the options of other modules. */
#include <stdlib.h>
#include <string.h>

#include "exhaust_settings.h"
#include "exhaust_vault.h"

/* The comma separated list value split into trimmed items; the strings live in one malloc'ed block. */
static size_t split_list(const char *value, char **block, const char ***items) {
    size_t n = 0, cap = 0;
    char *copy = malloc(strlen(value) + 1), *p, *save;
    const char **list = NULL;
    *block = NULL; *items = NULL;
    if (!copy) return 0;
    strcpy(copy, value);
    for (p = copy; p; p = save) {
        char *end;
        save = strchr(p, ',');
        if (save) *save++ = 0;
        while (*p == ' ' || *p == '\t') p++;
        end = p + strlen(p);
        while (end > p && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
        if (!*p) continue;
        if (n == cap) {
            const char **nl = realloc(list, (cap ? cap * 2 : 8) * sizeof *nl);
            if (!nl) break;
            list = nl;
            cap = cap ? cap * 2 : 8;
        }
        list[n++] = p;
    }
    *block = copy;
    *items = list;
    return n;
}

void ex_settings_apply_vault(const ExSettings *s, ExVault *v) {
    ExVaultOptions o;
    const char *t = ex_settings_get(s, "files", "trash_mode"), *sort = ex_settings_get(s, "files", "sort_by");
    char *block;
    const char **items;
    size_t n = split_list(ex_settings_get(s, "files", "excluded"), &block, &items);
    memset(&o, 0, sizeof o);
    o.show_all_files = ex_settings_get_bool(s, "files", "show_all_file_types");
    o.trash_mode = !strcmp(t, "vault_trash") ? EX_TRASH_VAULT : !strcmp(t, "permanent") ? EX_TRASH_PERMANENT : EX_TRASH_SYSTEM;
    o.sort_by = !strcmp(sort, "modified") ? EX_SORT_MODIFIED : !strcmp(sort, "created") ? EX_SORT_CREATED : EX_SORT_NAME;
    o.sort_descending = ex_settings_get_bool(s, "files", "sort_descending");
    o.excluded = items;
    o.nexcluded = n;
    ex_vault_set_options(v, &o);
    free(items);
    free(block);
}
