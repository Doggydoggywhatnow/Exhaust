/* The canvas view (sub-slice 6a): the widget that draws and edits a JSON Canvas (include/exhaust_canvas.h) held in a note's document.
 *
 * The widget draws only what is on screen: cards, groups and edges are tested against the view, a card's text is laid out only for the cards that
 * are big enough to read (below the zoom of settings `[canvas] zoom_hide_threshold` a card is an outline), the lines and boxes of a frame go into
 * one ExGeom, and a card's text wraps only as far as the card is tall. The text of the notes that file cards show comes from a cache the
 * application fills on a worker (nothing here reads a file).
 *
 * Editing. Every change the user makes (moving, resizing, adding, deleting, a card's text, an edge's label, a color) is written into the document
 * as one edit when the gesture ends, so it goes to the recovery journal and the autosave like typing in a note, and Undo and Redo of the document
 * undo and redo it. The canvas is read again whenever the document changes by other means (undo, an outside change loaded, a restored version).
 * A canvas that is not valid JSON is shown as a message and never written.
 *
 * Mouse: a press on empty space and a drag pans (with Ctrl held, or with Shift, a box selects; with the setting `ctrl_drag = menu` Ctrl-drag
 * instead draws a box and asks what to create there); the wheel pans (or zooms, with the setting `wheel`; Ctrl turns one into the other); a press
 * on a card selects it and a drag moves it (with the cards in a group when a group moves); a press on the border of the selected card resizes it;
 * a press on one of the dots at the sides of the selected card and a drag draws an edge to another card (to empty space: to a new text card);
 * a double click on empty space makes a text card, on a text card edits it, on a file card opens the file, on a link card opens the address, on an
 * edge edits its label. Right click: a menu.
 * Keyboard (the widget takes the focus with a click or Tab): arrows pan, or move the selected cards by a step of the grid; + - zoom; 0 fits all;
 * Home fits the selection; Enter edits or opens the selected card; Delete removes the selection; Ctrl+A selects everything; Ctrl+C, X and V copy,
 * cut and paste cards (as JSON Canvas text, so they go between canvases); Ctrl+Z, Ctrl+Y undo and redo; Escape clears the selection. */
#ifndef EXHAUST_CANVASVIEW_H
#define EXHAUST_CANVASVIEW_H
#include "exhaust_canvas.h"
#include "exhaust_panels.h"
#include "exhaust_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The text of the notes that file cards show (about 1500 bytes of each, read on a worker). One for the window, shared by every canvas. */
typedef struct ExCanvasCache ExCanvasCache;
ExCanvasCache *ex_canvascache_create(void);
void ex_canvascache_free(ExCanvasCache *c);
/* The worker's answer for a file; text NULL: it could not be read. */
void ex_canvascache_set(ExCanvasCache *c, const char *path, const char *text);
/* The file changed (or went): the next frame asks again. An empty path forgets everything. */
void ex_canvascache_invalidate(ExCanvasCache *c, const char *path);
unsigned long ex_canvascache_version(const ExCanvasCache *c);

typedef struct ExCanvasHandlers {
    void *user;
    ExPanels *panels;                                   /* for the menus and the questions; may be NULL (then there are none) */
    ExSettings *settings;                               /* the `[canvas]` settings, read every frame; may be NULL (the defaults) */
    ExCanvasCache *cache;
    /* the application reads the text of the note on a worker and calls ex_canvascache_set */
    void (*want_text)(void *user, const char *path);
    /* a double click on a file card, or Enter on it; subpath is "" or "#heading" or "#^block" */
    void (*open_file)(void *user, const char *path, const char *subpath, int new_tab);
    void (*open_url)(void *user, const char *url);
    /* does a note or file of this name or path exist? Fills path (the path in the vault) and returns 1 */
    int (*resolve)(void *user, const char *text, char *path, size_t cap);
    /* a text card becomes a note (Convert to file): creates it with the text and returns 1 with its path */
    int (*create_note)(void *user, const char *text, char *path, size_t cap);
    void (*message)(void *user, const char *text);      /* a line for the status bar */
} ExCanvasHandlers;

/* The widget. doc is the document of the canvas file (owned by the caller, and it must outlive the widget); path is its name. The canvas is
 * read from the document now and whenever the document changes. */
ExWidget *ex_widget_canvas(ExUi *ui, ExDoc *doc, const char *path, const ExCanvasHandlers *h);
/* Draw, called from the UI. */
void ex_canvasview_draw(ExUi *ui, ExWidget *w);
void ex_canvasview_wheel(ExWidget *w, float dy, float x, float y);
void ex_canvasview_context(ExWidget *w, float x, float y);

/* Adding things (the commands): at the middle of the view, or at a window position when x >= 0. They start editing a new text card. */
int ex_canvasview_add_text(ExWidget *w, float x, float y);
int ex_canvasview_add_group(ExWidget *w, float x, float y);
/* Asks which note or file to add, then adds it. */
void ex_canvasview_add_file_prompt(ExWidget *w, float x, float y);
int ex_canvasview_add_file(ExWidget *w, const char *path, float x, float y);
void ex_canvasview_fit(ExWidget *w, int selection_only);
void ex_canvasview_jump_to_group_prompt(ExWidget *w);
/* Convert the selected text card to a file card (the handler creates the note). Returns 0, or -1 when no text card is selected or it could not be done. */
int ex_canvasview_convert_selected(ExWidget *w);
int ex_canvasview_has_selection(const ExWidget *w);
/* The view as `x y zoom` (the centre in canvas units), for the session; set returns 0 when the text is good. */
void ex_canvasview_get_view(const ExWidget *w, char *out, size_t cap);
int ex_canvasview_set_view(ExWidget *w, const char *text);
/* Writes pending changes into the document now (the application calls it before it flushes notes). */
void ex_canvasview_commit(ExWidget *w);
/* For the tests and the screen reader: what the view is showing. */
typedef struct ExCanvasViewStats {
    size_t cards, edges, cards_drawn, edges_drawn, texts_laid_out, triangles, geom_grows, selected_cards, selected_edges;
    float zoom, cx, cy;
    int content_hidden;          /* the zoom is below the threshold: cards are outlines */
    int editing;                 /* a card's text is being edited */
    int broken;                  /* the file is not a canvas: a message is shown */
    int mode;                    /* 0 none, 1 pan, 2 move, 3 resize, 4 edge, 5 box */
    unsigned long reads;         /* times the canvas was read from the document */
    unsigned long commits;       /* edits written into the document */
} ExCanvasViewStats;
void ex_canvasview_stats(const ExWidget *w, ExCanvasViewStats *out);
const ExCanvas *ex_canvasview_model(const ExWidget *w);
/* A card's box in window pixels (0 if it is not a card). */
int ex_canvasview_card_rect(const ExWidget *w, size_t index, float *x, float *y, float *wd, float *ht);
/* A short description of the canvas and what is selected, for a screen reader. malloc'ed. */
char *ex_canvasview_summary(const ExWidget *w);

#ifdef __cplusplus
}
#endif
#endif
