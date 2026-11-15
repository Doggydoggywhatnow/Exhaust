/* table_edit (5f): the 15 table.* commands on a sample table. Each gives the expected text and caret, changes only the lines of the table
 * (the text before and after is byte for byte the same), is one undo step, and is unavailable where it does not apply. A sample marks the
 * caret with \x01. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_editor.h"

static int failures;
#define S "\x01"

static ExDoc *doc_from(const char *marked) {
    char *plain = malloc(strlen(marked) + 1);
    size_t n = 0, at = 0;
    const char *p;
    ExDoc *d;
    ExSel s;
    for (p = marked; *p; p++) { if (*p == '\x01') at = n; else plain[n++] = *p; }
    d = ex_doc_create(plain, n);
    s.anchor = s.head = at;
    ex_doc_set_selections(d, &s, 1);
    free(plain);
    return d;
}

static char *render(ExDoc *d) {
    size_t len, i, w = 0, caret = ex_doc_sel(d, 0).head;
    char *t = ex_buffer_text(ex_doc_buffer(d), &len), *out = malloc(len + 4);
    for (i = 0; i <= len; i++) {
        if (i == caret) out[w++] = '\x01';
        if (i < len) out[w++] = t[i];
    }
    out[w] = 0;
    free(t);
    return out;
}

static void show(const char *s) {
    for (; *s; s++) { if (*s == '\x01') fputs("<>", stderr); else if (*s == '\n') fputs("\\n", stderr); else if (*s == '\r') fputs("\\r", stderr); else fputc(*s, stderr); }
}

/* Runs the command; checks the result, that it is one undo step and that undo restores the text. want == NULL: the command must not apply. */
static void check(int line, const char *in, ExTableCmd c, const char *want) {
    ExDoc *d = doc_from(in);
    size_t nodes = ex_doc_node_count(d);
    char *before = render(d), *got;
    int can = ex_doc_table_can(d, c), changed = ex_doc_table(d, c);
    got = render(d);
    if (!want) {
        if (can || changed || strcmp(got, in) != 0) { failures++; fprintf(stderr, "line %d: command %d should not apply (can %d, changed %d)\n", line, (int)c, can, changed); }
    } else {
        if (!can || !changed || strcmp(got, want) != 0) {
            failures++;
            fprintf(stderr, "line %d: command %d (can %d) gave [", line, (int)c, can); show(got); fputs("], wanted [", stderr); show(want); fputs("]\n", stderr);
        } else if (ex_doc_node_count(d) != nodes + 1) { failures++; fprintf(stderr, "line %d: not one undo step\n", line); }
        else {
            char *back;
            ex_doc_undo(d);
            back = render(d);
            { char *a = before, *b = back; size_t la = strlen(a), lb = strlen(b), i, j; char *pa = malloc(la + 1), *pb = malloc(lb + 1);
              for (i = j = 0; a[i]; i++) if (a[i] != '\x01') pa[j++] = a[i];
              pa[j] = 0;
              for (i = j = 0; b[i]; i++) if (b[i] != '\x01') pb[j++] = b[i];
              pb[j] = 0;
              if (strcmp(pa, pb) != 0) { failures++; fprintf(stderr, "line %d: undo does not restore the text\n", line); }
              free(pa); free(pb); }
            free(back);
        }
        /* nothing outside the table changes: the first and the last lines are the same */
        if (strncmp(got, "Intro\n", 6) != 0 || strlen(got) < 7 || strcmp(got + strlen(got) - 6, "Outro\n") != 0) {
            if (strncmp(in, "Intro\n", 6) == 0) { failures++; fprintf(stderr, "line %d: text outside the table changed\n", line); }
        }
    }
    free(before); free(got);
    ex_doc_free(d);
}
#define T(in, c, want) check(__LINE__, in, c, want)
/* The command applies here but there is nothing to change: no edit, no undo step. */
static void check_nochange(int line, const char *in, ExTableCmd c) {
    ExDoc *d = doc_from(in);
    size_t nodes = ex_doc_node_count(d);
    int can = ex_doc_table_can(d, c), changed = ex_doc_table(d, c);
    char *got = render(d);
    if (!can || changed || strcmp(got, in) != 0 || ex_doc_node_count(d) != nodes) { failures++; fprintf(stderr, "line %d: command %d should apply and change nothing\n", line, (int)c); }
    free(got);
    ex_doc_free(d);
}

#define HEAD "Intro\n| Name | Qty |\n| :--- | --: |\n| a    | 1   |\n"
#define S0 HEAD "| " S "bb   | 22  |\nOutro\n"          /* the caret in the first column of the last row */
#define S1 HEAD "| bb   | " S "22  |\nOutro\n"          /* in the second column */
#define S2 "Intro\n| Name | Qty |\n| :--- | --: |\n| " S "a    | 1   |\n| bb   | 22  |\nOutro\n"   /* in the first body row */
#define SH "Intro\n| " S "Name | Qty |\n| :--- | --: |\n| a    | 1   |\n| bb   | 22  |\nOutro\n"  /* in the header */
#define SD "Intro\n| Name | Qty |\n| :--- | " S "--: |\n| a    | 1   |\n| bb   | 22  |\nOutro\n" /* in the delimiter row */

int main(void) {
    /* columns */
    T(S0, EX_TBL_COL_LEFT,  "Intro\n|     | Name | Qty |\n| --- | :--- | --: |\n|     | a    |   1 |\n| " S "    | bb   |  22 |\nOutro\n");
    T(S0, EX_TBL_COL_RIGHT, "Intro\n| Name |     | Qty |\n| :--- | --- | --: |\n| a    |     |   1 |\n| bb   | " S "    |  22 |\nOutro\n");
    T(S0, EX_TBL_DEL_COL,   "Intro\n| Qty |\n| --: |\n|   1 |\n|  " S "22 |\nOutro\n");
    T(S0, EX_TBL_DUP_COL,   "Intro\n| Name | Name | Qty |\n| :--- | :--- | --: |\n| a    | a    |   1 |\n| bb   | " S "bb   |  22 |\nOutro\n");
    T(S1, EX_TBL_MOVE_COL_LEFT, "Intro\n| Qty | Name |\n| --: | :--- |\n|   1 | a    |\n|  " S "22 | bb   |\nOutro\n");
    T(S0, EX_TBL_MOVE_COL_RIGHT, "Intro\n| Qty | Name |\n| --: | :--- |\n|   1 | a    |\n|  22 | " S "bb   |\nOutro\n");
    T(S0, EX_TBL_MOVE_COL_LEFT, NULL);                       /* the first column cannot go left */
    T(S1, EX_TBL_MOVE_COL_RIGHT, NULL);                      /* nor the last one right */
    /* rows */
    T(S0, EX_TBL_ROW_ABOVE, HEAD "| " S "     |     |\n| bb   | 22  |\nOutro\n");
    T(S0, EX_TBL_ROW_BELOW, HEAD "| bb   | 22  |\n| " S "     |     |\nOutro\n");
    T(S0, EX_TBL_DEL_ROW,   "Intro\n| Name | Qty |\n| :--- | --: |\n| " S "a    | 1   |\nOutro\n");
    T(S2, EX_TBL_DEL_ROW,   "Intro\n| Name | Qty |\n| :--- | --: |\n| " S "bb   | 22  |\nOutro\n");
    T(S0, EX_TBL_DUP_ROW,   HEAD "| bb   | 22  |\n| " S "bb   | 22  |\nOutro\n");
    T(S0, EX_TBL_MOVE_ROW_UP, "Intro\n| Name | Qty |\n| :--- | --: |\n| " S "bb   | 22  |\n| a    | 1   |\nOutro\n");
    T(S2, EX_TBL_MOVE_ROW_DOWN, "Intro\n| Name | Qty |\n| :--- | --: |\n| bb   | 22  |\n| " S "a    | 1   |\nOutro\n");
    T(S2, EX_TBL_MOVE_ROW_UP, NULL);                         /* the first body row cannot go above the delimiter row */
    T(S0, EX_TBL_MOVE_ROW_DOWN, NULL);                       /* the last row has nothing below it */
    /* the header and the delimiter row are not rows to remove, copy or move; a row can be added below them */
    T(SH, EX_TBL_DEL_ROW, NULL);
    T(SH, EX_TBL_DUP_ROW, NULL);
    T(SH, EX_TBL_ROW_ABOVE, NULL);
    T(SD, EX_TBL_MOVE_ROW_DOWN, NULL);
    T(SH, EX_TBL_ROW_BELOW, "Intro\n| Name | Qty |\n| :--- | --: |\n| " S "     |     |\n| a    | 1   |\n| bb   | 22  |\nOutro\n");
    /* alignment: only the delimiter row's cell changes */
    T(S0, EX_TBL_ALIGN_CENTER, "Intro\n| Name | Qty |\n| :--: | --: |\n| a    | 1   |\n| " S "bb   | 22  |\nOutro\n");
    T(S0, EX_TBL_ALIGN_RIGHT,  "Intro\n| Name | Qty |\n| ---: | --: |\n| a    | 1   |\n| " S "bb   | 22  |\nOutro\n");
    T(S1, EX_TBL_ALIGN_LEFT,   "Intro\n| Name | Qty |\n| :--- | :-- |\n| a    | 1   |\n| bb   | " S "22  |\nOutro\n");
    check_nochange(__LINE__, S0, EX_TBL_ALIGN_LEFT);         /* already left: nothing to do, no undo step */
    /* the caret outside a table: nothing applies */
    {
        ExTableCmd c;
        const char *outside[] = { "Intro\n" S "text | with a pipe\nnot a table\n", S "plain\n", "| a | b |\n" S "\n| c | d |\n", "```\n| a | b |\n|---|---|\n| " S "1 | 2 |\n```\n" };
        size_t i;
        for (i = 0; i < sizeof outside / sizeof *outside; i++)
            for (c = 0; c < EX_TBL_COUNT; c++) {
                ExDoc *d = doc_from(outside[i]);
                if (ex_doc_table_can(d, c) || ex_doc_table(d, c)) { failures++; fprintf(stderr, "case %zu: command %d applied outside a table\n", i, (int)c); }
                ex_doc_free(d);
            }
    }
    /* other shapes of table */
    T("| a | b\n|---|---\n| " S "1 | 2\n", EX_TBL_ROW_BELOW, "| a | b\n|---|---\n| 1 | 2\n| " S "    |     |\n");     /* no closing pipes, no final newline gap */
    T("| a | b |\n|---|---|\n| 1 | " S "2 |", EX_TBL_DEL_ROW, "| a | b |\n|---|" S "---|");                  /* the last line of the note, no final newline */
    T("| a | b |\n|---|---|\n| 1 | " S "2 |", EX_TBL_ROW_BELOW, "| a | b |\n|---|---|\n| 1 | 2 |\n|     | " S "    |");
    T("a | b\n--|--\n" S "1 | 2\n", EX_TBL_DEL_COL, "| b   |\n| --- |\n| " S "2   |\n");                               /* a table with no outer pipes at all */
    T("| a \\| b | c |\n|---|---|\n| " S "1 | 2 |\n", EX_TBL_DEL_COL, "| c   |\n| --- |\n| " S "2   |\n");   /* an escaped pipe does not split a cell */
    T("| a | b |\r\n|---|---|\r\n| " S "1 | 2 |\r\n", EX_TBL_ROW_BELOW, "| a | b |\r\n|---|---|\r\n| 1 | 2 |\r\n| " S "    |     |\r\n");   /* CRLF is kept */
    T("| a | b |\n|---|---|\n| 1 | 2 | 3 |\n| " S "x |\n", EX_TBL_COL_RIGHT, "| a   |     | b   |     |\n| --- | --- | --- | --- |\n| 1   |     | 2   | 3   |\n| x   | " S "    |     |     |\n"); /* no cell is lost */
    {   /* a command applies to the primary caret only, and a caret on a line far from the table's start counts */
        ExDoc *d = doc_from("para\n\n| a | b |\n|---|---|\n| 1 | 2 |\n| 3 | " S "4 |\n\nafter\n");
        if (!ex_doc_table(d, EX_TBL_DEL_ROW)) { failures++; fputs("a caret in the last row of a table in the middle of a note\n", stderr); }
        else { char *got = render(d); if (strncmp(got, "para\n\n| a | b |\n|---|---|\n| 1 | ", 28) != 0 || !strstr(got, "\n\nafter\n")) { failures++; fputs("middle of a note: wrong text\n", stderr); } free(got); }
        ex_doc_free(d);
    }
    {   /* random text made of table characters: no command crashes, and each one that changes the text is undone by one undo */
        unsigned seed = 12345;
        int iter, in_table = 0;
        for (iter = 0; iter < 4000; iter++) {
            static const char *frag[] = { "| a | b |", "|---|---|", "a|b", "--|:-:", "| x \\| y | z |", "", "|", "| :-- | --: | - |", "> | q |", "```", "text", "|a|", "|--|", " | a |  b ", "\r" };
            char buf[400];
            size_t n = 0, i, at;
            size_t lines;
            seed = seed * 1103515245u + 12345u;
            lines = 3 + (seed >> 16) % 6;
            for (i = 0; i < lines; i++) {
                const char *f;
                seed = seed * 1103515245u + 12345u;
                f = frag[(seed >> 16) % (sizeof frag / sizeof *frag)];
                if (i == 1 && (seed & 0x100)) f = frag[1];
                memcpy(buf + n, f, strlen(f)); n += strlen(f);
                if (i + 1 < lines || (seed & 0x200)) {
                    if (seed & 0x400) buf[n++] = '\r';
                    buf[n++] = '\n';
                }
            }
            ExDoc *d;
            ExSel sel;
            ExTableCmd c;
            char *before, *after;
            seed = seed * 1103515245u + 12345u;
            at = (seed >> 16) % (n + 1);
            d = ex_doc_create(buf, n);
            sel.anchor = sel.head = at;
            ex_doc_set_selections(d, &sel, 1);
            before = render(d);
            if (ex_doc_table_can(d, EX_TBL_COL_LEFT)) in_table++;
            for (c = 0; c < EX_TBL_COUNT; c++) {
                int can = ex_doc_table_can(d, c);
                if (ex_doc_table(d, c)) {
                    if (!can) { failures++; fprintf(stderr, "random %d: command %d changed the text but was not available\n", iter, (int)c); }
                    ex_doc_undo(d);
                    after = render(d);
                    { size_t a1 = 0, b1 = 0; const char *x = before, *y = after;           /* the carets may differ: compare the text */
                      while (x[a1] || y[b1]) {
                          if (x[a1] == '\x01') { a1++; continue; }
                          if (y[b1] == '\x01') { b1++; continue; }
                          if (x[a1] != y[b1]) { failures++; fprintf(stderr, "random %d: undo of command %d did not restore the text\n", iter, (int)c); break; }
                          a1++; b1++;
                      } }
                    free(after);
                }
            }
            free(before);
            ex_doc_free(d);
        }
        if (in_table < 100) { failures++; fprintf(stderr, "the random text made only %d tables\n", in_table); }
    }
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    puts("table_edit ok");
    return 0;
}
