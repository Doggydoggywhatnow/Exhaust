/* The properties of the open note (sub-slice 4d): add, edit, change the type of and remove a property of the note in the
 * active editor. Each action is a text edit of the front matter (exhaust_props.h) that touches only the lines of that
 * property, applied as one undo step of the open buffer; a note whose front matter is not valid YAML is left alone. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_props.h"
#include "panels_internal.h"

static const char *const TYPES[] = {"Text", "List", "Number", "Checkbox", "Date", "Date and time", "Links"};

static const ExListRow *selected(ExPanels *p) {
    View *v = &p->views[V_FILEPROPS];
    return v->list ? ex_list_row(v->list, ex_list_selected(v->list)) : NULL;
}

static void say(ExPanels *p, const char *text) { if (p->in.host.message) p->in.host.message(p->in.host.user, text); }

/* Runs one edit on the active note's text: what 0 set, 1 remove, 2 retype. */
static void edit(ExPanels *p, int what, const char *name, int type, const char *value) {
    size_t len = 0, out_len = 0;
    char *text, *out = NULL;
    if (!p->cur_path || !p->in.host.active_text || !p->in.host.replace_text) return;
    text = p->in.host.active_text(p->in.host.user, &len);
    if (!text) return;
    if (what == 0) out = ex_prop_text_set(text, len, name, type, value, &out_len);
    else if (what == 1) out = ex_prop_text_remove(text, len, name, &out_len);
    else out = ex_prop_text_retype(text, len, name, type, &out_len);
    if (!out) say(p, "The property could not be changed (a name that is not valid, a value that does not fit its type, or front matter that is not YAML).");
    else if (out_len != len || memcmp(out, text, len)) {
        if (p->in.host.replace_text(p->in.host.user, p->cur_path, out, out_len) != 0) say(p, "The property could not be changed.");
    }
    free(out); free(text);
    p->views[V_FILEPROPS].stale = 1;
}

/* "a, b, c" -> "a\nb\nc" for a list. */
static char *list_value(const char *v) {
    char *o = malloc(strlen(v) + 1), *w = o;
    int skip = 1;
    if (!o) return NULL;
    for (; *v; v++) {
        if (*v == ',') { *w++ = '\n'; skip = 1; continue; }
        if (skip && (*v == ' ' || *v == '\t')) continue;
        skip = 0;
        *w++ = *v;
    }
    *w = 0;
    return o;
}

static void set_value(ExPanels *p, const char *name, int type, const char *value) {
    char *v = type == EX_PROP_LIST || type == EX_PROP_LINKS ? list_value(value) : dup_s(value);
    if (v) edit(p, 0, name, type, v);
    free(v);
}

static void value_done(ExPanels *p, const char *text, void *arg) {
    char name[200];
    int type;
    const char *s = arg;
    if (!s) return;
    type = s[0] - '0';
    snprintf(name, sizeof name, "%s", s + 1);
    set_value(p, name, type, text);
}

static void ask_value(ExPanels *p, const char *name, int type, const char *initial) {
    char *arg = malloc(strlen(name) + 3);
    if (!arg) return;
    arg[0] = (char)('0' + type); strcpy(arg + 1, name);
    free(p->dialog_arg_text);
    p->dialog_arg_text = arg;
    dialog_prompt(p, name, type == EX_PROP_LIST || type == EX_PROP_LINKS ? "Value (items separated by commas)" : "Value", initial, value_done, p->dialog_arg_text);
}

static void name_done(ExPanels *p, const char *text, void *arg) {
    int type = ex_prop_type_get(p->root, text);
    (void)arg;
    if (!text[0]) return;
    if (type < 0) type = EX_PROP_TEXT;
    if (type == EX_PROP_CHECKBOX) { edit(p, 0, text, type, "false"); return; }
    ask_value(p, text, type, "");
}

void fileprops_on_add(ExWidget *w, void *u) {
    ExPanels *p = u;
    (void)w;
    if (!p->cur_path) return;
    dialog_prompt(p, "New property", "Name", "", name_done, NULL);
}

void fileprops_on_edit(ExWidget *w, void *u) {
    ExPanels *p = u;
    const ExListRow *r = selected(p);
    (void)w;
    if (!r || !r->key) return;
    if ((int)r->id == EX_PROP_CHECKBOX) {                                   /* a checkbox flips */
        edit(p, 0, r->key, EX_PROP_CHECKBOX, r->detail && !strcmp(r->detail, "true") ? "false" : "true");
        return;
    }
    ask_value(p, r->key, (int)r->id, r->detail ? r->detail : "");
}

void fileprops_on_remove(ExWidget *w, void *u) {
    ExPanels *p = u;
    const ExListRow *r = selected(p);
    (void)w;
    if (r && r->key) { char *name = dup_s(r->key); if (name) edit(p, 1, name, 0, NULL); free(name); }
}

static void type_done(ExPanels *p, size_t index, const char *key, void *arg) {
    (void)key;
    if (!arg) return;
    ex_prop_type_set(p->root, (const char *)arg, (int)index);           /* the type belongs to the name, in the whole vault */
    edit(p, 2, (const char *)arg, (int)index, NULL);
    p->views[V_PROPS].stale = 1;
}

void fileprops_on_type(ExWidget *w, void *u) {
    ExPanels *p = u;
    const ExListRow *r = selected(p);
    (void)w;
    if (!r || !r->key) return;
    free(p->dialog_arg_text);
    p->dialog_arg_text = dup_s(r->key);
    dialog_choice(p, "Type of this property", (const char **)TYPES, 7, (size_t)r->id, type_done, p->dialog_arg_text);
}
