#ifndef EXHAUST_FINDBAR_H
#define EXHAUST_FINDBAR_H

#include "exhaust_find.h"
#include "exhaust_ui.h"

/* Find and replace in the note being edited (sub-slice 3d): a bar with a find field, switches for regular expressions,
 * case and whole words, previous and next, a count, and (when opened for replacing) a replacement field with Replace
 * and Replace all. Every match is highlighted in the editor; Enter goes to the next match, Shift+Enter to the previous,
 * Escape closes the bar and returns to the editor. */
typedef struct ExFindBar ExFindBar;

ExFindBar *ex_findbar_create(ExUi *ui);
void ex_findbar_free(ExFindBar *fb);
ExWidget *ex_findbar_widget(ExFindBar *fb);
/* Shows the bar for an editor widget (the note being edited), with the selected text as the pattern if it is one line;
 * `replace` also shows the replacement row. Focus goes to the find field. */
void ex_findbar_open(ExFindBar *fb, ExWidget *editor, int replace);
void ex_findbar_close(ExFindBar *fb);
int ex_findbar_is_open(const ExFindBar *fb);
/* The editor the bar works on (the active one); NULL if none. Call it when the active tab changes. */
void ex_findbar_set_target(ExFindBar *fb, ExWidget *editor);
/* The editor widget is about to be freed (its tab closed or its content replaced): the bar forgets it without touching it. */
void ex_findbar_target_closed(ExFindBar *fb, ExWidget *editor);
/* Call after every input: recompiles the pattern when it or an option changed, recounts, moves to the first match
 * after a change of the pattern, and updates the highlight and the count. */
void ex_findbar_update(ExFindBar *fb);

void ex_findbar_next(ExFindBar *fb, int dir);      /* dir > 0 next, < 0 previous; wraps around */
int ex_findbar_replace(ExFindBar *fb);             /* replaces the selected match (if it is one), then goes to the next; 1 if replaced */
size_t ex_findbar_replace_all(ExFindBar *fb);      /* one undo step; returns the number of replacements */

typedef enum ExFindBarOption { EX_FINDBAR_REGEX, EX_FINDBAR_CASE, EX_FINDBAR_WORD } ExFindBarOption;
void ex_findbar_set_option(ExFindBar *fb, ExFindBarOption o, int on);
void ex_findbar_set_pattern(ExFindBar *fb, const char *pattern);
void ex_findbar_set_replacement(ExFindBar *fb, const char *text);
size_t ex_findbar_match_count(const ExFindBar *fb);   /* up to 10,000 */
const char *ex_findbar_status(const ExFindBar *fb);   /* "3 of 12", "No results", or the pattern's error */

#endif
