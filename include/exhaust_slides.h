/* Slides (sub-slice 6e-iii): a note is split into slides at the lines that hold `---`. See docs/formats.md (Slides). */
#ifndef EXHAUST_SLIDES_H
#define EXHAUST_SLIDES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExSlide { size_t start, end; } ExSlide;   /* the bytes of the slide's text in the note, without the blank lines round it and without the separator */

/* Splits the text. A separator is a line of three or more `-` (spaces and a carriage return allowed after them) that stands alone: the line before it is blank or
 * it is the first line of the text, so a `---` that underlines a text (a heading) is not one. Not looked at: the properties at the start of the note, lines inside
 * a fenced code block (``` or ~~~), lines inside a `$$` math block and an HTML comment. Slides with nothing but blank lines are left out. A text with no
 * separator is one slide; a text with nothing in it is none. Returns 0 and a malloc'ed array (free with ex_slides_free), or -1 on memory. */
int ex_slides_split(const char *text, size_t len, ExSlide **out, size_t *n);
void ex_slides_free(ExSlide *s);

#ifdef __cplusplus
}
#endif
#endif
