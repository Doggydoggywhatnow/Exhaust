/* The status bar (sub-slice 7f): the line at the bottom of the window. A message on the left; on the right the items that apply (the editing mode,
 * the save state, a notice that files are checked by polling, the word count). See "Shell" in docs/formats.md. The bar holds no state of the
 * notes: the application tells it what to show. */
#ifndef EXHAUST_STATUSBAR_H
#define EXHAUST_STATUSBAR_H

#include "exhaust_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExStatusBar ExStatusBar;
typedef enum ExSaveState { EX_SAVE_NONE, EX_SAVE_SAVED, EX_SAVE_EDITING, EX_SAVE_FAILED } ExSaveState;   /* NONE: no note, nothing shown */

ExStatusBar *ex_statusbar_create(ExUi *ui);
void ex_statusbar_free(ExStatusBar *s);
ExWidget *ex_statusbar_widget(ExStatusBar *s);
void ex_statusbar_message(ExStatusBar *s, const char *text);
/* The editing mode of the active note ("live", "source" or "reading"; NULL or "" for none) and whether the setting show_mode_in_status_bar is on. */
void ex_statusbar_set_mode(ExStatusBar *s, const char *mode, int setting_on);
void ex_statusbar_set_save(ExStatusBar *s, ExSaveState state);
void ex_statusbar_set_polling(ExStatusBar *s, int polling);
/* The word count item is the application's label (its text is set elsewhere); this shows or hides it. */
ExWidget *ex_statusbar_words(ExStatusBar *s);
ExWidget *ex_statusbar_message_label(ExStatusBar *s);              /* the message's label, for code that keeps its own pointer */
/* The text of an item as shown now ("" when it is hidden): "message", "mode", "save", "polling", "words". For tests. */
const char *ex_statusbar_text(const ExStatusBar *s, const char *item);
int ex_statusbar_item_visible(const ExStatusBar *s, const char *item);

#ifdef __cplusplus
}
#endif
#endif
