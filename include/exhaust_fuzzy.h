/* Fuzzy matching for lists of commands: the slash menu now, the command palette and the quick switcher later, so they
 * all rank the same way. The query's characters must appear in the text in order (without regard to case); the score
 * is higher for matches at the start of words, for characters that follow each other, and for matches near the start. */
#ifndef EXHAUST_FUZZY_H
#define EXHAUST_FUZZY_H

#ifdef __cplusplus
extern "C" {
#endif

/* -1 if the query does not match, else a score of 0 or more (an empty query scores 0). */
int ex_fuzzy_score(const char *query, const char *text);

#ifdef __cplusplus
}
#endif
#endif
