#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust.h"
#include "exhaust_fontdb.h"
#include "exhaust_input.h"
#include "exhaust_platform.h"
#include "exhaust_render.h"
#include "exhaust_theme.h"
#include "exhaust_ui.h"
#include "exhaust_worker.h"

/* The system font scan runs on a worker thread and hands its result to the UI
 * thread as a message; the queue wakes the UI loop with an SDL event. */
enum { MSG_FONTS_READY = 1 };
static Uint32 g_wake_event;

static void wake_ui(void *unused) {
    SDL_Event e;
    (void)unused;
    SDL_zero(e);
    e.type = g_wake_event;
    SDL_PushEvent(&e);
}

static void worker_job(ExWorker *self, const ExMessage *job, void *unused) {
    (void)unused;
    if (job->type == MSG_FONTS_READY) /* the job's name; the result carries the same type */
        ex_worker_send(self, MSG_FONTS_READY, ex_fontdb_scan(NULL, 0, NULL, NULL), 0, NULL);
}

typedef struct App {
    ExRenderer *r;
    ExUi *ui;
    ExTheme theme;
    ExWidget *root, *editor;
    ExDoc *doc;
    int blink_on;
    float zoom;
} App;

static void relayout_and_draw(App *a) {
    int w, h;
    ex_renderer_size(a->r, &w, &h);
    ex_ui_layout(a->ui, a->root, (float)w, (float)h);
    ex_renderer_clear(a->r, ex_theme_color(&a->theme, EX_COLOR_BACKGROUND));
    ex_ui_draw(a->ui, a->root, a->blink_on);
    ex_renderer_present(a->r);
}

static void on_toggle_theme(ExWidget *w, void *user) {
    App *a = user;
    ExTheme next;
    (void)w;
    if (ex_theme_init_builtin(&next, a->theme.is_dark ? EX_THEME_LIGHT : EX_THEME_DARK) == 0) {
        ex_theme_free(&a->theme);
        a->theme = next;
        ex_ui_set_theme(a->ui, &a->theme);
    }
}

int main(int argc, char **argv) {
    App app;
    SDL_Event e;
    ExQueue *results;
    ExWorker *worker;
    ExMessage job;
    int running = 1, clicks = 0;
    Uint64 last_click = 0;
    const char *intro = "Welcome to Exhaust.\n\nType here. English, \xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A (Arabic), "
                        "\xD7\xA2\xD7\x91\xD7\xA8\xD7\x99\xD7\xAA (Hebrew), \xE4\xBD\xA0\xE5\xA5\xBD (Chinese), \xF0\x9F\x98\x80.\n";

    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        printf("Exhaust %s\n", exhaust_version());
        return 0;
    }
    memset(&app, 0, sizeof app);
    app.zoom = 1.0f;
    app.blink_on = 1;
    app.r = ex_renderer_create("Exhaust", 960, 600);
    if (!app.r) {
        fprintf(stderr, "exhaust: cannot open a window: %s\n", ex_render_error());
        return 1;
    }
    if (ex_theme_init_builtin(&app.theme, EX_THEME_DARK) != 0) return 1;
    ex_renderer_set_display_scale(app.r, ex_renderer_detect_display_scale(app.r));
    app.ui = ex_ui_create(app.r, &app.theme, 16.0f);
    app.doc = ex_doc_create(intro, strlen(intro));
    app.root = ex_sample_window(app.ui, app.doc, on_toggle_theme, &app, &app.editor);
    ex_ui_set_focus(app.ui, app.editor);

    g_wake_event = SDL_RegisterEvents(1);
    results = ex_queue_create(wake_ui, NULL);
    worker = ex_worker_start("font scan", 1, worker_job, NULL, results);
    memset(&job, 0, sizeof job);
    job.type = MSG_FONTS_READY;
    if (worker) ex_worker_post(worker, &job);

    ex_text_input_start(ex_renderer_window(app.r));
    relayout_and_draw(&app);
    while (running) {
        ExTextEvent te;
        ExKey key;
        int mods;
        int timed_out = !SDL_WaitEventTimeout(&e, 530); /* sleeps until something happens or the caret blinks */
        if (timed_out) {
            /* nothing happened: blink the caret */
        } else if (e.type == g_wake_event) {
            ExMessage m;
            while (ex_queue_pop(results, &m))
                if (m.type == MSG_FONTS_READY) ex_renderer_set_font_db(app.r, (ExFontDb *)m.payload);
        } else if (ex_text_event_decode(&e, &te)) {
            if (te.kind == EX_TEXT_COMMIT) ex_ui_text_input(app.ui, te.text, strlen(te.text));
            else if (app.editor->editor) ex_editor_set_composition(app.editor->editor, te.text);
        } else if (ex_key_from_sdl(&e, &key, &mods)) {
            ex_ui_key(app.ui, key, mods);
        } else {
            switch (e.type) {
            case SDL_EVENT_QUIT: running = 0; break;
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                ex_renderer_set_display_scale(app.r, ex_renderer_detect_display_scale(app.r));
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            case SDL_EVENT_MOUSE_MOTION: {
                float x = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.x : e.button.x;
                float y = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.y : e.button.y;
                float density = SDL_GetWindowPixelDensity(ex_renderer_window(app.r));
                x *= density; y *= density; /* window coordinates to output pixels */
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    Uint64 now = SDL_GetTicks();
                    clicks = now - last_click < 400 ? clicks + 1 : 1;
                    last_click = now;
                    ex_ui_mouse(app.ui, app.root, EX_MOUSE_DOWN, x, y, ex_mods_from_sdl(), clicks);
                } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                    ex_ui_mouse(app.ui, app.root, EX_MOUSE_UP, x, y, ex_mods_from_sdl(), clicks);
                } else if (e.motion.state & SDL_BUTTON_LMASK) {
                    ex_ui_mouse(app.ui, app.root, EX_MOUSE_DRAG, x, y, ex_mods_from_sdl(), clicks);
                }
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL:
                if (app.editor->editor) ex_editor_scroll(app.editor->editor, -e.wheel.y * 3.0f * (float)ex_px(app.r, 24));
                break;
            case SDL_EVENT_KEY_DOWN:
                if (ex_mods_from_sdl() & EX_MOD_PRIMARY) {
                    if (e.key.key == SDLK_EQUALS || e.key.key == SDLK_PLUS) app.zoom *= 1.1f;
                    else if (e.key.key == SDLK_MINUS) app.zoom /= 1.1f;
                    else if (e.key.key == SDLK_0) app.zoom = 1.0f;
                    ex_renderer_set_zoom(app.r, app.zoom);
                }
                break;
            default: break;
            }
        }
        app.blink_on = timed_out ? !app.blink_on : 1;
        if (app.editor->editor) {
            float cx, cy, cw, ch;
            relayout_and_draw(&app);
            ex_editor_caret_rect(app.editor->editor, app.r, &cx, &cy, &cw, &ch);
            ex_text_input_set_area(ex_renderer_window(app.r), (int)cx, (int)cy, 1, (int)ch);
        }
    }
    ex_worker_stop(worker);
    {
        ExMessage m;
        while (ex_queue_pop(results, &m))
            if (m.type == MSG_FONTS_READY) ex_fontdb_free((ExFontDb *)m.payload);
    }
    ex_queue_destroy(results);
    ex_widget_free(app.root);
    ex_doc_free(app.doc);
    ex_ui_free(app.ui);
    ex_theme_free(&app.theme);
    ex_renderer_destroy(app.r);
    return 0;
}
