#include <stdlib.h>
#include <string.h>

#include "exhaust_ui.h"

ExWidget *ex_sample_window(ExUi *ui, ExDoc *doc, ExClickFn toggle_theme, void *user, ExWidget **editor_out) {
    ExWidget *root, *row, *ribbon, *toggle, *editor, *status;
    root = ex_widget_box(ui, 1, "Exhaust");
    root->role = EX_ROLE_WINDOW;
    root->width = ex_size_grow();
    root->height = ex_size_grow();
    root->bg_token = EX_COLOR_BACKGROUND;
    row = ex_widget_box(ui, 0, "workspace");
    row->width = ex_size_grow();
    row->height = ex_size_grow();
    ribbon = ex_widget_box(ui, 1, "ribbon");
    ribbon->width = ex_size_token(EX_SIZE_RIBBON_WIDTH, 1.0f);
    ribbon->height = ex_size_grow();
    ribbon->bg_token = EX_COLOR_BACKGROUND_ALT;
    ribbon->pad_x = 0.5f;
    ribbon->pad_y = 1.0f;
    toggle = ex_widget_button(ui, "Aa", toggle_theme, user);
    free(toggle->name); /* the visible text is "Aa"; screen readers get the purpose */
    toggle->name = malloc(32);
    if (toggle->name) strcpy(toggle->name, "Switch light and dark theme");
    editor = ex_widget_editor(ui, doc, "Note text");
    status = ex_widget_label(ui, "Typing, selecting, several cursors (Ctrl+Alt+Up or Down, Alt+click), undo and redo work. Ctrl+Plus and Ctrl+Minus zoom.");
    status->text_token = EX_COLOR_TEXT_MUTED;
    status->text_scale = 0.85f;
    status->width = ex_size_grow();
    status->pad_x = 1.0f;
    status->pad_y = 0.5f;
    status->bg_token = EX_COLOR_BACKGROUND_ALT;
    ex_widget_add(ribbon, toggle);
    ex_widget_add(row, ribbon);
    ex_widget_add(row, editor);
    ex_widget_add(root, row);
    ex_widget_add(root, status);
    if (editor_out) *editor_out = editor;
    return root;
}
