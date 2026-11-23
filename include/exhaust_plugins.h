/* Plugins (sub-slice 8b): Lua 5.4 in a sandbox, one state for each plugin, all on one thread of their own; the manifest, the user's approval of the
 * permissions, the API of docs/formats.md (Plugin manifest, The runtime, Lua API). The interface thread never runs plugin code: it posts jobs and, once a
 * frame, calls ex_plugins_update, which carries out the calls the plugins make into the application (the editor, notices, panes, commands). */
#ifndef EXHAUST_PLUGINS_H
#define EXHAUST_PLUGINS_H

#include <stddef.h>
#include "exhaust_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EX_PLUGIN_MAX_PERMS 9

typedef struct ExPluginInfo {
    char id[64], name[120], version[32], author[120], description[400], entry[120];
    int api;
    char permissions[EX_PLUGIN_MAX_PERMS][24];
    size_t nperm;
    int valid;                 /* the manifest is usable (error says why not) */
    int enabled;               /* the user turned it on (plugins.ini) */
    int approved;              /* the approval matches the manifest's permissions and version */
    int running;
    char status[240];          /* one line for the Plugins page: "Off", "Running", "Needs approval: ...", "Stopped: ..." */
    char error[200];
} ExPluginInfo;

/* What the application gives the runtime. Every callback runs on the interface thread, from ex_plugins_update. */
typedef struct ExPluginHost {
    void *user;
    /* the open note: malloc'ed text, the selection and the cursor as byte offsets; -1 when no note is open */
    int (*editor_get)(void *user, char **text, size_t *sel0, size_t *sel1, size_t *cursor);
    int (*editor_replace)(void *user, const char *text, size_t len, int whole);   /* the selection, or the whole text; 0 on success */
    void (*notice)(void *user, const char *text);
    int (*open_pane)(void *user, const char *title, const char *markdown);        /* a tab with read-only text; 0 on success */
    int (*rename)(void *user, const char *from, const char *to);                  /* vault.rename; 0 on success */
    void (*changed)(void *user);                                                  /* the list of plugins or a state changed */
} ExPluginHost;

typedef struct ExPlugins ExPlugins;

/* commands may be NULL (a test without a registry). wake is called, from the plugin thread, when a message waits for ex_plugins_update. */
ExPlugins *ex_plugins_create(const char *vault_root, ExCommands *commands, const ExPluginHost *host, void (*wake)(void *), void *wake_user);
void ex_plugins_free(ExPlugins *p);     /* stops the thread (waits up to two seconds for a plugin that is stuck in C code) */

void ex_plugins_update(ExPlugins *p);   /* the interface thread, once a frame: the plugins' calls into the application */

/* The master switch ([plugins] allow). Off stops every plugin. */
void ex_plugins_set_allowed(ExPlugins *p, int allowed);
/* Reads the plugin folders again and starts and stops what must be started and stopped. */
void ex_plugins_rescan(ExPlugins *p);
/* Turns a plugin on, approving the permissions its manifest lists now (the caller has asked the user), or off. */
void ex_plugins_enable(ExPlugins *p, const char *id, int on);
/* The state of every plugin found (copied; at most `max`); returns how many there are. */
size_t ex_plugins_snapshot(ExPlugins *p, ExPluginInfo *out, size_t max);
/* Events for the plugins: "vault-open", "file-open", "file-save", "file-rename", "editor-change" (at most one in 500 ms). a and b may be NULL. */
void ex_plugins_emit(ExPlugins *p, const char *name, const char *a, const char *b);
/* True while jobs are waiting or running (for tests). */
int ex_plugins_busy(ExPlugins *p);

/* The manifest and the permission names, without a runtime (tests and the Plugins page). */
int ex_plugin_permission_valid(const char *name);
int ex_plugin_read_manifest(const char *folder, const char *folder_name, ExPluginInfo *out);   /* 1 valid; 0 with out->error */

#ifdef __cplusplus
}
#endif
#endif
