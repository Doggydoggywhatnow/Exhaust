/* The slash menu's model (sub-slice 3d): the list of commands registered with the editor flag, filtered and ranked by fuzzy
 * matching of what was typed after the "/". The list is the command registry's, not a separate one: a command that is not
 * bound or whose feature is off stays in the list but is shown disabled and cannot run. */
#ifndef EXHAUST_SLASH_H
#define EXHAUST_SLASH_H

#include <stddef.h>

#include "exhaust_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExSlashMenu ExSlashMenu;

typedef struct ExSlashItem {
    const char *id, *name;
    int available;       /* can run now */
    int score;
} ExSlashItem;

ExSlashMenu *ex_slash_create(ExCommands *commands);     /* commands are not owned */
void ex_slash_free(ExSlashMenu *m);
/* What was typed after the slash. Everything matches an empty query, in table order; otherwise only the matches, best first
 * (ties in table order). The selection returns to the first item. */
void ex_slash_set_query(ExSlashMenu *m, const char *query);
const char *ex_slash_query(const ExSlashMenu *m);
size_t ex_slash_count(const ExSlashMenu *m);
const ExSlashItem *ex_slash_item(const ExSlashMenu *m, size_t i);
size_t ex_slash_selected(const ExSlashMenu *m);
void ex_slash_move(ExSlashMenu *m, int delta);          /* wraps around */
void ex_slash_select(ExSlashMenu *m, size_t i);
/* Runs the selected command; 0 if it ran, else the result of ex_commands_run (-2 for a command that is not bound, -3 for one that is not available now), or -1 if the list is empty. */
int ex_slash_run(ExSlashMenu *m);

#ifdef __cplusplus
}
#endif
#endif
