/* editing_format: the text formatting commands (the editor commands of the slash menu) on small samples. A sample marks
 * its selection with \x01 (start) and \x02 (end; absent means a caret at the \x01). The result is written the same way. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_editor.h"

static int failures;

static ExDoc *doc_from(const char *marked) {
    char *plain = malloc(strlen(marked) + 1);
    size_t n = 0, a = 0, b = 0;
    int have_b = 0;
    const char *p;
    ExDoc *d;
    ExSel s;
    for (p = marked; *p; p++) {
        if (*p == '\x01') a = n;
        else if (*p == '\x02') { b = n; have_b = 1; }
        else plain[n++] = *p;
    }
    if (!have_b) b = a;
    d = ex_doc_create(plain, n);
    s.anchor = a; s.head = b;
    ex_doc_set_selections(d, &s, 1);
    free(plain);
    return d;
}

static char *render(ExDoc *d) {
    size_t len, i, n = ex_doc_sel_count(d), w = 0;
    char *t = ex_buffer_text(ex_doc_buffer(d), &len), *out = malloc(len + 4 * n + 8);
    for (i = 0; i <= len; i++) {
        size_t k;
        for (k = 0; k < n; k++) {
            ExSel s = ex_doc_sel(d, k);
            size_t a = s.anchor < s.head ? s.anchor : s.head, b = s.anchor < s.head ? s.head : s.anchor;
            if (i == a) out[w++] = '\x01';
            if (i == b && b != a) out[w++] = '\x02';
        }
        if (i < len) out[w++] = t[i];
    }
    out[w] = 0;
    free(t);
    return out;
}

static void show(const char *s) {
    for (; *s; s++) { if (*s == '\x01') fputs("<", stderr); else if (*s == '\x02') fputs(">", stderr); else if (*s == '\n') fputs("\\n", stderr); else if (*s == '\t') fputs("\\t", stderr); else fputc(*s, stderr); }
}

#define S "\x01"
#define E "\x02"
static ExFormatOptions OPT = { 1, 4 };

static void check(const char *line, const char *in, ExFormat f, const char *want) {
    ExDoc *d = doc_from(in);
    char *got;
    ex_doc_format(d, f, &OPT);
    got = render(d);
    if (strcmp(got, want) != 0) {
        failures++;
        fprintf(stderr, "line %s: [", line); show(in); fputs("] gave [", stderr); show(got); fputs("], wanted [", stderr); show(want); fputs("]\n", stderr);
    }
    free(got);
    ex_doc_free(d);
}
#define T(in, f, want) check(#f ":" #in, in, f, want)

int main(void) {
    ExDoc *d;
    char *got;
    /* inline toggles */
    T("a " S "word" E " b", EX_FMT_BOLD, "a **" S "word" E "** b");
    T("a **" S "word" E "** b", EX_FMT_BOLD, "a " S "word" E " b");
    T("a " S "**word**" E " b", EX_FMT_BOLD, "a " S "word" E " b");
    T("a " S " b", EX_FMT_BOLD, "a **" S "** b");
    T("a **" S "** b", EX_FMT_BOLD, "a " S " b");
    T("" S "x" E "", EX_FMT_ITALIC, "*" S "x" E "*");
    T("**" S "x" E "**", EX_FMT_ITALIC, "***" S "x" E "***");
    T("***" S "x" E "***", EX_FMT_ITALIC, "**" S "x" E "**");
    T("***" S "x" E "***", EX_FMT_BOLD, "*" S "x" E "*");
    T("**" S "x" E "**", EX_FMT_BOLD, "" S "x" E "");
    T("" S "x" E "", EX_FMT_STRIKE, "~~" S "x" E "~~");
    T("" S "x" E "", EX_FMT_HIGHLIGHT, "==" S "x" E "==");
    T("==" S "x" E "==", EX_FMT_HIGHLIGHT, "" S "x" E "");
    T("" S "x" E "", EX_FMT_CODE, "`" S "x" E "`");
    T("" S "x" E "", EX_FMT_INLINE_MATH, "$" S "x" E "$");
    T("" S "x" E "", EX_FMT_COMMENT, "%%" S "x" E "%%");
    T("**" S "**", EX_FMT_ITALIC, "***" S "***");                   /* a caret in "**|**" is bold: italic adds a pair */
    T("" S "caf\xC3\xA9" E "", EX_FMT_BOLD, "**" S "caf\xC3\xA9" E "**");
    /* headings */
    T("" S "title", EX_FMT_HEADING_2, "## " S "title");
    T("## " S "title", EX_FMT_HEADING_2, "" S "title");
    T("## " S "title", EX_FMT_HEADING_3, "### " S "title");
    T("" S "title", EX_FMT_HEADING_CYCLE, "# " S "title");
    T("# " S "title", EX_FMT_HEADING_CYCLE, "## " S "title");
    T("###### " S "title", EX_FMT_HEADING_CYCLE, "" S "title");
    T("### " S "title", EX_FMT_REMOVE_HEADING, "" S "title");
    T("" S "a\nb" E "", EX_FMT_HEADING_1, "# " S "a\n# b" E "");
    /* lists and quotes */
    T("" S "a\nb" E "", EX_FMT_BULLET, "- " S "a\n- b" E "");
    T("- " S "a\n- b" E "", EX_FMT_BULLET, "" S "a\nb" E "");
    T("" S "a\n\nb" E "", EX_FMT_BULLET, "- " S "a\n\n- b" E "");        /* blank lines stay blank */
    T("" S "a\nb" E "", EX_FMT_NUMBERED, "1. " S "a\n2. b" E "");
    T("1. " S "a\n2. b" E "", EX_FMT_NUMBERED, "" S "a\nb" E "");
    T("- " S "a", EX_FMT_NUMBERED, "1. " S "a");
    T("1. " S "a", EX_FMT_BULLET, "- " S "a");
    T("" S "a\n\nb" E "", EX_FMT_QUOTE, "> " S "a\n> \n> b" E "");
    T("> " S "a\n> b" E "", EX_FMT_QUOTE, "" S "a\nb" E "");
    T("  - " S "a", EX_FMT_NUMBERED, "  1. " S "a");
    /* checkboxes */
    T("- " S "a", EX_FMT_CHECKBOX_STATUS, "- [ ] " S "a");
    T("- [ ] " S "a", EX_FMT_CHECKBOX_STATUS, "- [x] " S "a");
    T("- [x] " S "a", EX_FMT_CHECKBOX_STATUS, "- [ ] " S "a");
    T("" S "a", EX_FMT_CHECKBOX_STATUS, "- [ ] " S "a");
    T("1. " S "a", EX_FMT_CHECKBOX_STATUS, "1. [ ] " S "a");
    T("" S "a", EX_FMT_CYCLE_BULLET_CHECKBOX, "- " S "a");
    T("- " S "a", EX_FMT_CYCLE_BULLET_CHECKBOX, "- [ ] " S "a");
    T("- [x] " S "a", EX_FMT_CYCLE_BULLET_CHECKBOX, "" S "a");
    /* indent */
    T("- " S "a", EX_FMT_INDENT, "\t- " S "a");
    T("" S "a", EX_FMT_INDENT, "" S "" "a");                         /* not a list item: nothing */
    T("\t- " S "a", EX_FMT_UNINDENT, "- " S "a");
    T("- " S "a", EX_FMT_UNINDENT, "- " S "a");
    OPT.use_tabs = 0; OPT.indent_width = 2;
    T("- " S "a", EX_FMT_INDENT, "  - " S "a");
    T("    - " S "a", EX_FMT_UNINDENT, "  - " S "a");
    OPT.use_tabs = 1; OPT.indent_width = 4;
    /* moving and deleting */
    T("" S "" "a\nb\nc", EX_FMT_MOVE_LINE_DOWN, "b\n" S "" "a\nc");
    T("a\nb" S "\nc", EX_FMT_MOVE_LINE_UP, "b" S "\na\nc");
    T("" S "" "a\nb", EX_FMT_MOVE_LINE_UP, "" S "" "a\nb");
    T("a\n" S "" "b" E "\nc\nd", EX_FMT_MOVE_LINE_DOWN, "a\nc\n" S "" "b" E "\nd");
    T("a" S "\nb", EX_FMT_MOVE_LINE_DOWN, "b\na" S "");              /* the last line has no line break */
    T("p1\np" S "" "2\n\nq\n", EX_FMT_DELETE_PARAGRAPH, "" S "q\n");
    T("q\n\np" S "" "1", EX_FMT_DELETE_PARAGRAPH, "q\n" S "");
    /* blocks */
    T("text" S "", EX_FMT_HR, "text\n\n---" S "");
    T("" S "", EX_FMT_HR, "---" S "");
    T("" S "x" E "", EX_FMT_CODE_BLOCK, "```\n" S "x" E "\n```");
    T("" S "", EX_FMT_CODE_BLOCK, "```\n" S "\n```");
    T("" S "x" E "", EX_FMT_MATH_BLOCK, "$$\n" S "x" E "\n$$");
    T("" S "line", EX_FMT_CALLOUT, "> [!note]\n> " S "line");
    T("" S "a\nb" E "", EX_FMT_CALLOUT, "> [!note]\n> " S "" "a\n> b");
    T("" S "", EX_FMT_TABLE, "| " S "| |\n|---|---|\n| | |");
    T("Hello" S "", EX_FMT_FOOTNOTE, "Hello[^1]\n\n[^1]: " S "");
    T("x" S "y\n", EX_FMT_FOOTNOTE, "x[^1]y\n\n[^1]: " S "");
    T("a[^1]b" S "\n\n[^1]: one\n", EX_FMT_FOOTNOTE, "a[^1]b[^2]\n\n[^1]: one\n\n[^2]: " S "");
    T("" S "txt" E "", EX_FMT_MD_LINK, "[" S "txt" E "]()");
    T("" S "", EX_FMT_MD_LINK, "[" S "]()");
    T("" S "", EX_FMT_INTERNAL_LINK, "[[" S "]]");
    T("" S "Page" E "", EX_FMT_INTERNAL_LINK, "[[" S "Page" E "]]");
    T("" S "", EX_FMT_EMBED, "![[" S "]]");
    T("" S "", EX_FMT_TAG, "#" S "");
    T("" S "**b** and ==h== `c`" E "", EX_FMT_CLEAR, "" S "" "b and h c" E "");
    T("# " S "head", EX_FMT_CLEAR, "" S "head" E "");

    /* several selections, and one undo step undoes the whole command */
    d = doc_from("" S "" "a" E " b");
    {
        ExSel two[2];
        two[0].anchor = 0; two[0].head = 1; two[1].anchor = 2; two[1].head = 3;
        ex_doc_set_selections(d, two, 2);
        ex_doc_format(d, EX_FMT_BOLD, &OPT);
        got = render(d);
        if (strcmp(got, "**" S "" "a" E "** **" S "" "b" E "**") != 0) { failures++; fputs("two selections: [", stderr); show(got); fputs("]\n", stderr); }
        free(got);
        ex_doc_undo(d);
        got = ex_buffer_text(ex_doc_buffer(d), NULL);
        if (strcmp(got, "a b") != 0) { failures++; fprintf(stderr, "undo gave [%s]\n", got); }
        free(got);
    }
    ex_doc_free(d);
    /* an action for Ctrl+B and Ctrl+I */
    {
        ExEditAction a;
        int ext;
        if (!ex_edit_action_for(EX_KEY_B, EX_MOD_PRIMARY, 0, &a, &ext) || a != EX_ACT_BOLD) { failures++; fputs("Ctrl+B\n", stderr); }
        if (!ex_edit_action_for(EX_KEY_I, EX_MOD_PRIMARY, 1, &a, &ext) || a != EX_ACT_ITALIC) { failures++; fputs("Cmd+I\n", stderr); }
    }
    printf("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
