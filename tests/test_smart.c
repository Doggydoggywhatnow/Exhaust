/* smart_typing: the typing helpers of docs/formats.md ("Typing helpers: smart lists and auto-pairing"): Enter in lists and quotes,
 * pairing of brackets, quotes and Markdown marks, typing over a closer and Backspace between a pair; then the same through the editor
 * (one key at a time, as typed), including what must NOT happen: plain fields, code blocks, several cursors, pasted text, a setting off,
 * and that the `[[` link list still opens and accepts over the closing brackets. Samples mark the caret with \001 and a selection with
 * \002 (start) and \003 (end). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_editor.h"
#include "exhaust_render.h"
#include "exhaust_text.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static ExDoc *doc_from(const char *marked) {
    char *plain = malloc(strlen(marked) + 1);
    size_t n = 0, a = 0, h = 0;
    int have_sel = 0;
    const char *p;
    ExDoc *d;
    ExSel s;
    for (p = marked; *p; p++) {
        if (*p == '\001') { a = h = n; }
        else if (*p == '\002') { a = n; have_sel = 1; }
        else if (*p == '\003') { h = n; have_sel = 1; }
        else plain[n++] = *p;
    }
    (void)have_sel;
    d = ex_doc_create(plain, n);
    s.anchor = a; s.head = h;
    ex_doc_set_selections(d, &s, 1);
    free(plain);
    return d;
}

static char *render(ExDoc *d) {
    size_t len, i, w = 0;
    ExSel s = ex_doc_sel(d, 0);
    char *t = ex_buffer_text(ex_doc_buffer(d), &len), *out = malloc(len + 8);
    for (i = 0; i <= len; i++) {
        if (s.anchor == s.head && i == s.head) out[w++] = '\001';
        else if (s.anchor != s.head && i == s.anchor) out[w++] = '\002';
        else if (s.anchor != s.head && i == s.head) out[w++] = '\003';
        if (i < len) out[w++] = t[i];
    }
    out[w] = 0;
    free(t);
    return out;
}

static void show(const char *s) {
    for (; *s; s++) { if (*s == '\001') fputs("<>", stderr); else if (*s == '\002') fputs("<<", stderr); else if (*s == '\003') fputs(">>", stderr); else if (*s == '\n') fputs("\\n", stderr); else fputc(*s, stderr); }
}

static const ExSmartOptions ALL = { 1, 1, 1 };

/* kind: 'e' Enter, 't' typed character, 'b' backspace */
static void expect(char kind, const char *before, const char *typed, const char *after, int handled) {
    ExDoc *d = doc_from(before);
    int got = 0;
    char *out;
    if (kind == 'e') got = ex_doc_smart_enter(d, 0);
    else if (kind == 't') got = ex_doc_smart_type(d, typed, strlen(typed), &ALL, 0);
    else got = ex_doc_smart_backspace(d, &ALL, 0);
    out = render(d);
    if (got != handled || strcmp(out, after) != 0) {
        fprintf(stderr, "%s: ", kind == 'e' ? "Enter" : kind == 't' ? "type" : "Backspace");
        show(before); fprintf(stderr, " %s -> ", typed ? typed : ""); show(out); fprintf(stderr, " (handled %d), wanted ", got); show(after); fprintf(stderr, " (handled %d)\n", handled);
        failures++;
    }
    free(out);
    ex_doc_free(d);
}

static void lists(void) {
    /* bullets keep their character and their blanks */
    expect('e', "- one\001", NULL, "- one\n- \001", 1);
    expect('e', "* one\001", NULL, "* one\n* \001", 1);
    expect('e', "+ one\001", NULL, "+ one\n+ \001", 1);
    expect('e', "  - nested\001", NULL, "  - nested\n  - \001", 1);
    expect('e', "\t- tab\001", NULL, "\t- tab\n\t- \001", 1);
    expect('e', "-   wide\001", NULL, "-   wide\n-   \001", 1);
    /* numbers count up, and keep their delimiter */
    expect('e', "1. a\001", NULL, "1. a\n2. \001", 1);
    expect('e', "9) a\001", NULL, "9) a\n10) \001", 1);
    /* tasks start unchecked */
    expect('e', "- [ ] todo\001", NULL, "- [ ] todo\n- [ ] \001", 1);
    expect('e', "- [x] done\001", NULL, "- [x] done\n- [ ] \001", 1);
    expect('e', "1. [X] done\001", NULL, "1. [X] done\n2. [ ] \001", 1);
    /* the text after the caret moves to the new item */
    expect('e', "- on\001e more", NULL, "- on\n- \001e more", 1);
    expect('e', "- \001all of it", NULL, "- \n- \001all of it", 1);
    /* a selection is replaced */
    expect('e', "- a\002bc\003d", NULL, "- a\n- \001d", 1);
    /* an empty item leaves the list: out one level when nested, else the marker goes */
    expect('e', "- one\n- \001", NULL, "- one\n\001", 1);
    expect('e', "- one\n  - \001", NULL, "- one\n- \001", 1);
    expect('e', "- one\n\t- \001", NULL, "- one\n- \001", 1);
    expect('e', "- one\n        - \001", NULL, "- one\n    - \001", 1);
    expect('e', "- [ ] \001", NULL, "\001", 1);
    expect('e', "1. \001", NULL, "\001", 1);
    expect('e', "- one\n-   \001  ", NULL, "- one\n\001", 1);
    /* not a list: a plain line break, so the editor types the newline */
    expect('e', "text\001", NULL, "text\001", 0);
    expect('e', "-\001", NULL, "-\001", 0);
    expect('e', "-text\001", NULL, "-text\001", 0);
    expect('e', "#hash\001", NULL, "#hash\001", 0);
    expect('e', "2024.\001", NULL, "2024.\001", 0);
    expect('e', "1234567890. a\001", NULL, "1234567890. a\001", 0);
    /* the caret inside the marker or before it */
    expect('e', "\001- one", NULL, "\001- one", 0);
    expect('e', "-\001 one", NULL, "-\001 one", 0);
    /* quotes and callouts */
    expect('e', "> said\001", NULL, "> said\n> \001", 1);
    expect('e', "> > deep\001", NULL, "> > deep\n> > \001", 1);
    expect('e', "> [!note] Title\001", NULL, "> [!note] Title\n> \001", 1);
    expect('e', "> said\n> \001", NULL, "> said\n\001", 1);
    expect('e', "> - item\001", NULL, "> - item\n> - \001", 1);
    expect('e', "> - \001", NULL, "> \001", 1);
    /* CRLF: the caret before the carriage return still sees an empty item */
    expect('e', "- a\r\n- \001\r\nb", NULL, "- a\r\n\001\r\nb", 1);
}

static void pairs(void) {
    expect('t', "\001", "(", "(\001)", 1);
    expect('t', "\001", "[", "[\001]", 1);
    expect('t', "\001", "{", "{\001}", 1);
    expect('t', "a \001", "\"", "a \"\001\"", 1);
    expect('t', "\001 x", "(", "(\001) x", 1);
    expect('t', "(\001)", "[", "([\001])", 1);
    expect('t', "[\001]", "[", "[[\001]]", 1);
    /* not before a word, and a quote not after one */
    expect('t', "\001word", "(", "\001word", 0);
    expect('t', "don\001", "\"", "don\001", 0);
    expect('t', "5\001", "\"", "5\001", 0);
    expect('t', "\"\001", "\"", "\"\001", 0);
    expect('t', "\001", "a", "\001", 0);
    /* type over a closer */
    expect('t', "(a\001)", ")", "(a)\001", 1);
    expect('t', "[[a\001]]", "]", "[[a]\001]", 1);
    expect('t', "\"a\001\"", "\"", "\"a\"\001", 1);
    expect('t', "a\001", ")", "a\001", 0);
    expect('t', "(a\001 )", ")", "(a\001 )", 0);
    /* a selection is wrapped and stays selected */
    expect('t', "a\002bc\003d", "(", "a(\002bc\003)d", 1);
    expect('t', "a\002bc\003d", "\"", "a\"\002bc\003\"d", 1);
    expect('t', "a\002bc\003d", "*", "a*\002bc\003*d", 1);
    expect('t', "a\002bc\003d", "_", "a_\002bc\003_d", 1);
    expect('t', "a\002bc\003d", "~", "a~\002bc\003~d", 1);
    expect('t', "a\002bc\003d", "=", "a=\002bc\003=d", 1);
    expect('t', "a\002bc\003d", "$", "a$\002bc\003$d", 1);
    expect('t', "a\002bc\003d", "`", "a`\002bc\003`d", 1);
    expect('t', "a\003bc\002d", "*", "a*\003bc\002*d", 1);           /* a backwards selection stays backwards */
    /* the marks do nothing without a selection, except the backtick */
    expect('t', "\001", "*", "\001", 0);
    expect('t', "\001", "_", "\001", 0);
    expect('t', "\001", "`", "`\001`", 1);
    expect('t', "`\001`", "`", "``\001", 1);
    expect('t', "``\001", "`", "``\001", 0);
    expect('t', "a\001", "`", "a\001", 0);
    /* Backspace between a pair */
    expect('b', "(\001)", NULL, "\001", 1);
    expect('b', "[\001]", NULL, "\001", 1);
    expect('b', "{\001}", NULL, "\001", 1);
    expect('b', "\"\001\"", NULL, "\001", 1);
    expect('b', "`\001`", NULL, "\001", 1);
    expect('b', "(a\001)", NULL, "(a\001)", 0);
    expect('b', "(\001", NULL, "(\001", 0);
    expect('b', "(\002a\003)", NULL, "(\002a\003)", 0);
}

/* The helpers are one undo step each. */
static void undo_steps(void) {
    ExDoc *d = doc_from("- a\001");
    char *t;
    ex_doc_smart_enter(d, 0);
    t = ex_buffer_text(ex_doc_buffer(d), NULL); CHECK(strcmp(t, "- a\n- ") == 0); free(t);
    CHECK(ex_doc_undo(d));
    t = ex_buffer_text(ex_doc_buffer(d), NULL); CHECK(strcmp(t, "- a") == 0); free(t);
    ex_doc_free(d);
    d = doc_from("a\002bc\003d");
    ex_doc_smart_type(d, "(", 1, &ALL, 0);
    CHECK(ex_doc_undo(d));
    t = ex_buffer_text(ex_doc_buffer(d), NULL); CHECK(strcmp(t, "abcd") == 0); free(t);
    ex_doc_free(d);
}

/* ---- through the editor ---- */

static char *text_of(ExEditor *e) { return ex_buffer_text(ex_doc_buffer(ex_editor_doc(e)), NULL); }
static void type(ExEditor *e, const char *s) { for (; *s; s++) ex_editor_text_input(e, s, 1); }

static ExEditor *new_editor(ExRenderer *r, const char *text, ExEditorMode mode, const ExSmartOptions *o) {
    ExDoc *d = ex_doc_create(text, strlen(text));
    ExEditor *e = ex_editor_create(d);
    ex_editor_set_font(e, ex_font_builtin(ex_renderer_fonts(r), EX_FONT_UI_REGULAR), 16.0f, 150);
    ex_editor_set_viewport(e, 0, 0, 800, 600);
    ex_editor_set_mode(e, mode);
    if (o) ex_editor_set_smart(e, o);
    ex_doc_set_caret(d, strlen(text));
    return e;
}
static void free_editor(ExEditor *e) { ExDoc *d = ex_editor_doc(e); ex_editor_free(e); ex_doc_free(d); }

static size_t suggest(void *u, const char *q, ExLinkSuggestion *out, size_t max) {
    (void)u; (void)q; (void)max;
    strcpy(out[0].label, "Target"); strcpy(out[0].insert, "Target"); out[0].detail[0] = 0;
    return 1;
}

static char *clip_plain(void *u) { (void)u; char *s = malloc(6); strcpy(s, "plain"); return s; }
static char *clip_url(void *u) { (void)u; char *s = malloc(40); strcpy(s, "https://example.com/a?b=1\n"); return s; }
static void clip_set(void *u, const char *t) { (void)u; (void)t; }
static char *html_md(void *u) { int *called = u; (*called)++; char *s = malloc(8); strcpy(s, "**web**"); return s; }

/* paste_html_as_markdown: a plain paste takes the Markdown made from the clipboard's formatted text; Paste without formatting and a setting that is off take the text. */
static void html_paste_cases(ExRenderer *r) {
    ExClipboard cb;
    int called = 0;
    ExEditor *e;
    char *t;
    cb.get = clip_plain; cb.set = clip_set; cb.user = NULL;
    e = new_editor(r, "", EX_EDITOR_LIVE, NULL);
    ex_editor_set_clipboard(e, &cb);
    ex_editor_perform(e, r, EX_ACT_PASTE, 0);                                 /* no hook: the text */
    t = text_of(e); CHECK(strcmp(t, "plain") == 0); free(t);
    ex_editor_set_html_paste(e, html_md, &called);
    ex_editor_perform(e, r, EX_ACT_PASTE, 0);
    t = text_of(e); CHECK(strcmp(t, "plain**web**") == 0); CHECK(called == 1); free(t);
    ex_editor_perform(e, r, EX_ACT_PASTE_PLAIN, 0);                           /* without formatting: the hook is not asked */
    t = text_of(e); CHECK(strcmp(t, "plain**web**plain") == 0); CHECK(called == 1); free(t);
    ex_editor_set_html_paste(e, NULL, NULL);
    ex_editor_perform(e, r, EX_ACT_PASTE, 0);
    t = text_of(e); CHECK(strcmp(t, "plain**web**plainplain") == 0); free(t);
    free_editor(e);
    e = new_editor(r, "", EX_EDITOR_PLAIN, NULL);                             /* a plain field takes the text even when the hook is set */
    ex_editor_set_clipboard(e, &cb);
    ex_editor_set_html_paste(e, html_md, &called);
    ex_editor_perform(e, r, EX_ACT_PASTE, 0);
    t = text_of(e); CHECK(strcmp(t, "plain") == 0); CHECK(called == 1); free(t);
    free_editor(e);
}

/* paste_url_on_selection: a web address over selected words makes a link of them */
static void url_paste_cases(ExRenderer *r) {
    ExClipboard cb;
    ExEditor *e;
    ExSmartOptions o = { 0, 0, 0, 1 }, off = { 0, 0, 0, 0 };
    char *t;
    cb.get = clip_url; cb.set = clip_set; cb.user = NULL;
    e = new_editor(r, "see the docs here", EX_EDITOR_LIVE, &o);
    ex_editor_set_clipboard(e, &cb);
    { ExSel s; s.anchor = 4; s.head = 12; ex_doc_set_selections(ex_editor_doc(e), &s, 1); }       /* "the docs" */
    ex_editor_perform(e, r, EX_ACT_PASTE, 0);
    t = text_of(e); CHECK(strcmp(t, "see [the docs](https://example.com/a?b=1) here") == 0); free(t);
    CHECK(ex_doc_sel(ex_editor_doc(e), 0).head == strlen("see [the docs](https://example.com/a?b=1)"));
    CHECK(ex_doc_undo(ex_editor_doc(e)) == 1);
    t = text_of(e); CHECK(strcmp(t, "see the docs here") == 0); free(t);
    { ExSel s; s.anchor = 4; s.head = 12; ex_doc_set_selections(ex_editor_doc(e), &s, 1); }
    ex_editor_perform(e, r, EX_ACT_PASTE_PLAIN, 0);                           /* without formatting: the address replaces the words */
    t = text_of(e); CHECK(strcmp(t, "see https://example.com/a?b=1\n here") == 0); free(t);
    free_editor(e);
    e = new_editor(r, "words", EX_EDITOR_LIVE, &o);                           /* nothing selected: the address as it is */
    ex_editor_set_clipboard(e, &cb);
    ex_editor_perform(e, r, EX_ACT_PASTE, 0);
    t = text_of(e); CHECK(strncmp(t, "wordshttps://example.com/a?b=1", 30) == 0 && strchr(t, '[') == NULL); free(t);
    free_editor(e);
    e = new_editor(r, "words", EX_EDITOR_LIVE, &off);                         /* setting off */
    ex_editor_set_clipboard(e, &cb);
    { ExSel s; s.anchor = 0; s.head = 5; ex_doc_set_selections(ex_editor_doc(e), &s, 1); }
    ex_editor_perform(e, r, EX_ACT_PASTE, 0);
    t = text_of(e); CHECK(strchr(t, '[') == NULL); free(t);
    free_editor(e);
}

/* the placeholder of an empty note */
static void placeholder_cases(ExRenderer *r) {
    ExEditor *e = new_editor(r, "", EX_EDITOR_LIVE, NULL);
    CHECK(!ex_editor_placeholder_visible(e));
    ex_editor_set_placeholder(e, "Start writing");
    CHECK(ex_editor_placeholder_visible(e));
    ex_editor_text_input(e, "a", 1);
    CHECK(!ex_editor_placeholder_visible(e));
    free_editor(e);
    e = new_editor(r, "", EX_EDITOR_PLAIN, NULL);                              /* a plain field (the title) never shows it */
    ex_editor_set_placeholder(e, "Start writing");
    CHECK(!ex_editor_placeholder_visible(e));
    free_editor(e);
}

static void editor_cases(ExRenderer *r) {
    ExEditor *e;
    char *t;
    ExSmartOptions off = { 0, 0, 0 }, lists_only = { 1, 0, 0 };

    /* typing a list, as the keys arrive */
    e = new_editor(r, "", EX_EDITOR_LIVE, &ALL);
    type(e, "- one"); ex_editor_key(e, r, EX_KEY_ENTER, 0); type(e, "two"); ex_editor_key(e, r, EX_KEY_ENTER, 0);
    ex_editor_key(e, r, EX_KEY_ENTER, 0);                                    /* the empty item ends the list */
    type(e, "after");
    t = text_of(e); CHECK(strcmp(t, "- one\n- two\nafter") == 0); free(t);
    free_editor(e);

    /* source mode too; a numbered list */
    e = new_editor(r, "", EX_EDITOR_SOURCE, &ALL);
    type(e, "1. a"); ex_editor_key(e, r, EX_KEY_ENTER, 0); type(e, "b");
    t = text_of(e); CHECK(strcmp(t, "1. a\n2. b") == 0); free(t);
    free_editor(e);

    /* plain fields (the title, the find bar) never use the helpers, even when told to */
    e = new_editor(r, "", EX_EDITOR_PLAIN, &ALL);
    type(e, "- a"); ex_editor_key(e, r, EX_KEY_ENTER, 0); type(e, "(");
    t = text_of(e); CHECK(strcmp(t, "- a\n(") == 0); free(t);
    free_editor(e);

    /* the settings off: Enter and "(" type as they always did */
    e = new_editor(r, "", EX_EDITOR_LIVE, &off);
    type(e, "- a"); ex_editor_key(e, r, EX_KEY_ENTER, 0); type(e, "(");
    t = text_of(e); CHECK(strcmp(t, "- a\n(") == 0); free(t);
    free_editor(e);
    e = new_editor(r, "", EX_EDITOR_LIVE, &lists_only);                     /* lists on, brackets off */
    type(e, "(");
    t = text_of(e); CHECK(strcmp(t, "(") == 0); free(t);
    free_editor(e);

    /* a code block is literal: no continuation, no backtick pair */
    e = new_editor(r, "```\n- in code\n```", EX_EDITOR_LIVE, &ALL);
    ex_doc_set_caret(ex_editor_doc(e), 13);
    ex_editor_key(e, r, EX_KEY_ENTER, 0);
    t = text_of(e); CHECK(strcmp(t, "```\n- in code\n\n```") == 0); free(t);
    type(e, "`");
    t = text_of(e); CHECK(strcmp(t, "```\n- in code\n`\n```") == 0); free(t);
    type(e, "(");                                                           /* brackets still pair: they are code too */
    t = text_of(e); CHECK(strcmp(t, "```\n- in code\n`()\n```") == 0); free(t);
    free_editor(e);

    /* front matter is literal */
    e = new_editor(r, "---\ntags:\n- a\n---\ntext", EX_EDITOR_LIVE, &ALL);
    ex_doc_set_caret(ex_editor_doc(e), 13);
    ex_editor_key(e, r, EX_KEY_ENTER, 0);
    t = text_of(e); CHECK(strcmp(t, "---\ntags:\n- a\n\n---\ntext") == 0); free(t);
    free_editor(e);

    /* more than one character at once (a paste, an IME commit) is typed as it is */
    e = new_editor(r, "", EX_EDITOR_LIVE, &ALL);
    ex_editor_text_input(e, "(a", 2);
    t = text_of(e); CHECK(strcmp(t, "(a") == 0); free(t);
    free_editor(e);

    /* several cursors: no helpers */
    e = new_editor(r, "a\nb", EX_EDITOR_LIVE, &ALL);
    ex_doc_set_caret(ex_editor_doc(e), 1);
    ex_doc_add_cursor(ex_editor_doc(e), 3);
    type(e, "(");
    t = text_of(e); CHECK(strcmp(t, "a(\nb(") == 0); free(t);
    free_editor(e);

    /* Backspace removes both of an empty pair, and a second one works on what is left */
    e = new_editor(r, "", EX_EDITOR_LIVE, &ALL);
    type(e, "[[");
    t = text_of(e); CHECK(strcmp(t, "[[]]") == 0); free(t);
    ex_editor_key(e, r, EX_KEY_BACKSPACE, 0);
    t = text_of(e); CHECK(strcmp(t, "[]") == 0); free(t);
    ex_editor_key(e, r, EX_KEY_BACKSPACE, 0);
    t = text_of(e); CHECK(strcmp(t, "") == 0); free(t);
    free_editor(e);

    /* the link list: "[[" gives "[[]]" with the list open; accepting an item lands after the brackets, and nothing is doubled */
    e = new_editor(r, "", EX_EDITOR_LIVE, &ALL);
    ex_editor_set_link_suggest(e, suggest, NULL);
    type(e, "[[Tar");
    t = text_of(e); CHECK(strcmp(t, "[[Tar]]") == 0); free(t);
    CHECK(ex_editor_link_suggest_open(e));
    ex_editor_key(e, r, EX_KEY_ENTER, 0);
    t = text_of(e); CHECK(strcmp(t, "[[Target]]") == 0); free(t);
    CHECK(ex_doc_sel(ex_editor_doc(e), 0).head == strlen("[[Target]]"));
    CHECK(!ex_editor_link_suggest_open(e));
    free_editor(e);

    /* typing the closing brackets by hand moves over the ones that are there */
    e = new_editor(r, "", EX_EDITOR_LIVE, &ALL);
    type(e, "[[a]]");
    t = text_of(e); CHECK(strcmp(t, "[[a]]") == 0); free(t);
    CHECK(ex_doc_sel(ex_editor_doc(e), 0).head == 5);
    free_editor(e);

    /* a Markdown link: "](" gives "()" and the address list accepts over the ")" */
    e = new_editor(r, "", EX_EDITOR_LIVE, &ALL);
    type(e, "[t](");
    t = text_of(e); CHECK(strcmp(t, "[t]()") == 0); free(t);
    free_editor(e);

    /* Tab and Shift+Tab move a list item in and out; elsewhere Tab types a tab and Shift+Tab does nothing */
    e = new_editor(r, "- a\n- b", EX_EDITOR_LIVE, &ALL);
    ex_editor_key(e, r, EX_KEY_TAB, 0);
    t = text_of(e); CHECK(strcmp(t, "- a\n\t- b") == 0); free(t);
    ex_editor_key(e, r, EX_KEY_TAB, EX_MOD_SHIFT);
    t = text_of(e); CHECK(strcmp(t, "- a\n- b") == 0); free(t);
    ex_editor_key(e, r, EX_KEY_TAB, EX_MOD_SHIFT);                          /* already at the top: nothing */
    t = text_of(e); CHECK(strcmp(t, "- a\n- b") == 0); free(t);
    CHECK(ex_doc_undo(ex_editor_doc(e)) == 1);
    free_editor(e);
    e = new_editor(r, "plain", EX_EDITOR_LIVE, &ALL);
    ex_editor_key(e, r, EX_KEY_TAB, 0);
    t = text_of(e); CHECK(strcmp(t, "plain\t") == 0); free(t);
    ex_editor_key(e, r, EX_KEY_TAB, EX_MOD_SHIFT);
    t = text_of(e); CHECK(strcmp(t, "plain\t") == 0); free(t);
    free_editor(e);
    e = new_editor(r, "- a", EX_EDITOR_LIVE, &off);                         /* smart lists off: a tab is typed */
    ex_editor_key(e, r, EX_KEY_TAB, 0);
    t = text_of(e); CHECK(strcmp(t, "- a\t") == 0); free(t);
    free_editor(e);

    /* one undo takes back one helper step */
    e = new_editor(r, "", EX_EDITOR_LIVE, &ALL);
    type(e, "- a"); ex_editor_key(e, r, EX_KEY_ENTER, 0);
    CHECK(ex_doc_undo(ex_editor_doc(e)));
    t = text_of(e); CHECK(strcmp(t, "- a") == 0); free(t);
    free_editor(e);
}

int main(void) {
    ExRenderer *r;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    r = ex_renderer_create("smart", 800, 600);
    if (!r) return 1;
    lists();
    pairs();
    undo_steps();
    editor_cases(r);
    html_paste_cases(r);
    url_paste_cases(r);
    placeholder_cases(r);
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("smart_typing: ok\n");
    return 0;
}
