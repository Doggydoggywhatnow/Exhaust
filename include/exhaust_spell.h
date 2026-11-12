/* Spell check and word count (sub-slice 3e).
 *
 * The checker reads Hunspell dictionaries (a `.aff` and a `.dic` file) and understands exactly these parts of them: `SET UTF-8`,
 * `FLAG` in its default form (one character per flag), `PFX` and `SFX` (strip, add, a condition made of characters, `.`, `[...]`
 * and `[^...]`, and the cross-product switch), `REP` (also with `^` and `$` anchors and `_` for a space), `KEY` (rows of keys, for
 * the typing slips a suggestion tries), `TRY` (the letters a suggestion may insert), `ICONV` (input conversion, for the curly
 * apostrophe), `NOSUGGEST` and `ONLYINCOMPOUND` (those words are never accepted alone). Everything else, compounding included, is
 * ignored; a dictionary that needs another `FLAG` form or another character set is refused. A word is correct when it is a stem of
 * the `.dic` file, or a stem with one suffix, one prefix or both (when both allow crossing). Case follows Hunspell: a stem in
 * lower case also accepts the word with a capital first letter or in capitals; a stem with a capital accepts capitals. */
#ifndef EXHAUST_SPELL_H
#define EXHAUST_SPELL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExSpell ExSpell;

/* Parses the two files (not copied beyond what is needed; the buffers may be freed afterwards). NULL with a message in err on failure. */
ExSpell *ex_spell_create(const char *aff, size_t aff_len, const char *dic, size_t dic_len, char *err, size_t errcap);
/* The English (US) dictionary built into the program. */
ExSpell *ex_spell_create_en_us(void);
void ex_spell_free(ExSpell *s);

/* 1 if the word (UTF-8, letters and apostrophes) is correct, else 0. Words with digits and one-letter words are always correct. */
int ex_spell_check(const ExSpell *s, const char *word, size_t len);

#define EX_SPELL_WORD_MAX 64
/* Up to `max` suggestions, best first: replacement pairs (REP), a swap of two letters, a key next to the one typed (KEY), a letter
 * missing, a letter too many, a wrong letter, and a space that splits the word into two correct ones. Returns how many. */
size_t ex_spell_suggest(const ExSpell *s, const char *word, size_t len, char out[][EX_SPELL_WORD_MAX], size_t max);

/* A word of the user's own: accepted as it is written, and in lower case as a stem. Returns 0, or -1 if the word is empty or too long. */
int ex_spell_add_word(ExSpell *s, const char *word, size_t len);
size_t ex_spell_stem_count(const ExSpell *s);

/* The words of a text: from *pos finds the next run of letters and digits (apostrophes and hyphens inside count as part of the
 * word; each Han, Hiragana and Katakana character is a word of its own). Sets [*start, *end) and moves *pos after it; returns 0 at the end. */
int ex_words_next(const char *text, size_t len, size_t *pos, size_t *start, size_t *end);

struct ExMd;
typedef struct ExWordCount { size_t words, characters; } ExWordCount;
/* Counts the words and the characters (not line breaks) of a note's text. With the Markdown tokens, front matter, comments and the
 * syntax characters (the `**` of bold text, the `#` of a heading, a link's address) are not counted; without them the whole text is. */
ExWordCount ex_wordcount(const char *text, size_t len, const struct ExMd *md);

#ifdef __cplusplus
}
#endif
#endif
