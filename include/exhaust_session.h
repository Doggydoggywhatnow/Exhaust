/* Session state: what was open, saved as it changes and restored on launch. The workspace
 * (exhaust_workspace.h) is serialized to `session.ini` in the vault's folder under the OS
 * local-data folder (docs/formats.md, "Journal and session state"). The file is written on a
 * worker thread (atomic replace), a short moment after the last change, so the UI never waits
 * for the disk and a burst of changes becomes one write. It is user data: a damaged file is
 * moved aside, never deleted, and never rebuilt from scratch. */
#ifndef EXHAUST_SESSION_H
#define EXHAUST_SESSION_H

#include <stdint.h>

#include "exhaust_vault.h"
#include "exhaust_workspace.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExSession ExSession;

/* data_dir overrides the vault's folder (tests); NULL uses the OS local-data folder keyed by the vault id. */
ExSession *ex_session_create(const ExVault *vault, const char *data_dir, int debounce_ms);
/* Saves what is pending, waits for the writer and frees. */
void ex_session_destroy(ExSession *s, ExWorkspace *ws);
/* The path of session.ini (owned by the session). */
const char *ex_session_path(const ExSession *s);
/* Reads the file into the workspace. Returns 1 if the workspace now holds a saved session, 0 if there
 * was none (the workspace is untouched), -1 if the file could not be read or parsed (it is kept as
 * session.ini.damaged and the workspace is untouched). */
int ex_session_load(ExSession *s, ExWorkspace *ws);
/* Call about ten times a second with a millisecond clock: when the workspace changed and `debounce_ms`
 * passed since the last change, queues a write. Cheap when nothing changed. */
void ex_session_tick(ExSession *s, ExWorkspace *ws, int64_t now_ms);
/* Writes at once and waits (used when leaving). Returns 0 on success. */
int ex_session_flush(ExSession *s, ExWorkspace *ws);
/* Number of files written so far (for tests). */
int ex_session_writes(const ExSession *s);

#ifdef __cplusplus
}
#endif
#endif
