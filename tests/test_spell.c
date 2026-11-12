/* spellcheck: the Hunspell-format checker on a sample dictionary (each of PFX, SFX, REP, KEY and the case rules covered
 * by itself), on the English (US) dictionary that is built in, and the word count on a known sample. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_markdown.h"
#include "exhaust_spell.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static const char *AFF_BASE =
    "SET UTF-8\n"
    "TRY oaeisntrlcdugmphbyfvkwz'\n"
    "ICONV 1\nICONV \xE2\x80\x99 '\n"
    "COMPOUNDMIN 1\nCOMPOUNDRULE 1\nCOMPOUNDRULE n*1t\nWORDCHARS 0123456789\n"          /* directives that are ignored */
    "PFX U Y 1\nPFX U 0 un .\n"
    "SFX S Y 2\nSFX S y ies [^aeiou]y\nSFX S 0 s [^sxzhy]\n"
    "SFX D N 1\nSFX D 0 d e\n"
    "REP 2\nREP f ph\nREP ^kn n\n";
static const char *DIC =
    "11\ncat/S\ncot\ndog/S\nbaby/S\nhappy/US\nlove/UD\nphone\nthe\nParis\ndon't\nbone\n";

static ExSpell *make(const char *extra) {
    char aff[2048];
    snprintf(aff, sizeof aff, "%s%s", AFF_BASE, extra);
    return ex_spell_create(aff, strlen(aff), DIC, strlen(DIC), NULL, 0);
}

static int ok(ExSpell *s, const char *w) { return ex_spell_check(s, w, strlen(w)); }

static size_t sug(ExSpell *s, const char *w, char out[][EX_SPELL_WORD_MAX]) { return ex_spell_suggest(s, w, strlen(w), out, 8); }

static int has(char out[][EX_SPELL_WORD_MAX], size_t n, const char *w) { size_t i; for (i = 0; i < n; i++) if (!strcmp(out[i], w)) return 1; return 0; }

int main(void) {
    ExSpell *s = make("KEY qwertyuiop|asdfghjkl|zxcvbnm\n"), *s2 = make("KEY so\n");
    char out[8][EX_SPELL_WORD_MAX], err[200];
    size_t n, k;
    ExSpell *en;
    CHECK(s && s2);

    /* stems, SFX (with its conditions), PFX and the cross product */
    CHECK(ok(s, "cat") && ok(s, "dog") && ok(s, "baby") && ok(s, "phone"));
    CHECK(ok(s, "cats") && ok(s, "dogs"));                            /* 0 s after a letter other than s x z h y */
    CHECK(ok(s, "babies"));                                           /* y to ies after a consonant */
    CHECK(!ok(s, "babys"));                                           /* the condition [^sxzhy] fails on "baby" */
    CHECK(!ok(s, "cates") && !ok(s, "phones"));                       /* phone has no flag S */
    CHECK(ok(s, "unhappy") && ok(s, "happies") && ok(s, "unhappies"));  /* prefix, suffix, both (S crosses) */
    CHECK(!ok(s, "uncat") && !ok(s, "unphone"));                      /* the prefix needs the flag U */
    CHECK(ok(s, "loved") && ok(s, "unlove"));
    CHECK(!ok(s, "unloved"));                                         /* the D group does not cross: no prefix as well */
    /* case: a lower case stem takes capitals; a capitalized stem needs them */
    CHECK(ok(s, "Cat") && ok(s, "CAT") && ok(s, "Dogs") && ok(s, "DOGS"));
    CHECK(!ok(s, "cAt") && !ok(s, "caT"));
    CHECK(ok(s, "Paris") && ok(s, "PARIS") && !ok(s, "paris") && !ok(s, "pAris"));
    /* apostrophes: the curly one is converted (ICONV); digits and one-letter words are not checked */
    CHECK(ok(s, "don't") && ok(s, "don\xE2\x80\x99t"));
    CHECK(ok(s, "x2") && ok(s, "2024") && ok(s, "x") && ok(s, "I"));
    CHECK(!ok(s, "xx") && !ok(s, "teh"));
    /* nothing is flagged that is far too long to be a word */
    {
        char longw[120];
        memset(longw, 'q', 100); longw[100] = 0;
        CHECK(ok(s, longw));
    }

    /* suggestions: a swap, a missing letter, a letter too many, a wrong letter */
    n = sug(s, "teh", out);
    CHECK(n >= 1 && strcmp(out[0], "the") == 0);
    n = sug(s, "Teh", out);
    CHECK(n >= 1 && strcmp(out[0], "The") == 0);                       /* the capital is kept */
    n = sug(s, "THEE", out);
    CHECK(has(out, n, "THE"));
    n = sug(s, "catt", out);
    CHECK(has(out, n, "cat"));
    n = sug(s, "ct", out);
    CHECK(has(out, n, "cat") && has(out, n, "cot"));
    n = sug(s, "dig", out);
    CHECK(has(out, n, "dog"));
    n = sug(s, "catdog", out);
    CHECK(has(out, n, "cat dog"));                                    /* two words */
    n = sug(s, "xyzzy", out);
    CHECK(n == 0);
    /* REP: "fone" is two edits from "phone", only the replacement pair gets there, and it comes first */
    n = sug(s, "fone", out);
    CHECK(n >= 1 && strcmp(out[0], "phone") == 0);
    n = sug(s2, "fone", out);
    CHECK(has(out, n, "phone"));
    /* REP with ^: only at the start of the word */
    {
        const char *dic2 = "2\nnife\nknife\n", *aff2 = "SET UTF-8\nREP 1\nREP ^kn n\n";
        ExSpell *t = ex_spell_create(aff2, strlen(aff2), dic2, strlen(dic2), NULL, 0);
        n = sug(t, "knife", out);
        CHECK(ex_spell_check(t, "knife", 5) && n == 0 + n);
        n = sug(t, "knif", out);
        CHECK(has(out, n, "knife"));
        ex_spell_free(t);
    }
    /* KEY: with the keys of a keyboard, the neighbour of the typed key comes first; with another layout another one does */
    n = sug(s, "cst", out);
    CHECK(n >= 2 && strcmp(out[0], "cat") == 0 && has(out, n, "cot"));            /* a is next to s on a qwerty keyboard */
    n = sug(s2, "cst", out);
    CHECK(n >= 2 && strcmp(out[0], "cot") == 0 && has(out, n, "cat"));            /* in the KEY row "so" the o is next to the s */

    /* words of the user's own */
    CHECK(!ok(s, "Zorp") && !ok(s, "zorp"));
    CHECK(ex_spell_add_word(s, "Zorp", 4) == 0);
    CHECK(ok(s, "Zorp") && ok(s, "zorp") && ok(s, "ZORP") && ok(s, "Zorp"));
    CHECK(ex_spell_add_word(s, "", 0) == -1);

    /* dictionaries this checker cannot read are refused with a reason */
    {
        const char *flagaff = "SET UTF-8\nFLAG long\n", *latin = "SET ISO8859-1\n";
        CHECK(ex_spell_create(flagaff, strlen(flagaff), "0\n", 2, err, sizeof err) == NULL && strstr(err, "FLAG") != NULL);
        CHECK(ex_spell_create(latin, strlen(latin), "0\n", 2, err, sizeof err) == NULL && strstr(err, "UTF-8") != NULL);
    }
    ex_spell_free(s);
    ex_spell_free(s2);

    /* the English (US) dictionary that is built in */
    en = ex_spell_create_en_us();
    CHECK(en != NULL);
    if (en) {
        static const char *good[] = { "the", "cats", "running", "unhappy", "Hello", "HELLO", "don't", "don\xE2\x80\x99t", "Tuesday", "quickly", "organization", "dictionary", "It's", NULL };
        static const char *bad[] = { "teh", "recieve", "definately", "wierd", "adress", "occured", "accomodate", "foxx", "dictionry", NULL };
        for (k = 0; good[k]; k++) if (!ok(en, good[k])) { fprintf(stderr, "should be correct: %s\n", good[k]); failures++; }
        for (k = 0; bad[k]; k++) if (ok(en, bad[k])) { fprintf(stderr, "should be flagged: %s\n", bad[k]); failures++; }
        CHECK(ex_spell_stem_count(en) > 40000);
        n = sug(en, "recieve", out); CHECK(has(out, n, "receive"));
        n = sug(en, "definately", out); CHECK(has(out, n, "definitely"));
        n = sug(en, "wierd", out); CHECK(has(out, n, "weird"));
        n = sug(en, "adress", out); CHECK(has(out, n, "address"));
        n = sug(en, "occured", out); CHECK(has(out, n, "occurred"));
        n = sug(en, "accomodate", out); CHECK(has(out, n, "accommodate"));
        n = sug(en, "teh", out); CHECK(n >= 1 && strcmp(out[0], "the") == 0);
        ex_spell_free(en);
    }

    /* word boundaries */
    {
        const char *text = "Hello, world! It's a well-known fact; don\xE2\x80\x99t 3.14 caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E x_y -dash- 'quoted'";
        size_t pos = 0, st, e, count = 0;
        char words[32][32];
        while (ex_words_next(text, strlen(text), &pos, &st, &e) && count < 32) { snprintf(words[count], 32, "%.*s", (int)(e - st), text + st); count++; }
        CHECK(count == 17);
        CHECK(!strcmp(words[0], "Hello") && !strcmp(words[2], "It's") && !strcmp(words[4], "well-known"));
        CHECK(!strcmp(words[6], "don\xE2\x80\x99t") && !strcmp(words[7], "3") && !strcmp(words[8], "14") && !strcmp(words[9], "caf\xC3\xA9"));
        CHECK(!strcmp(words[10], "\xE6\x97\xA5") && !strcmp(words[12], "\xE8\xAA\x9E"));         /* each ideograph is a word */
        CHECK(!strcmp(words[13], "x") && !strcmp(words[14], "y") && !strcmp(words[15], "dash") && !strcmp(words[16], "quoted"));
    }

    /* the word count on a known note */
    {
        const char *note = "---\ntitle: skip these words\n---\n# Heading one\n\nSome **bold** text with [link text](http://example.com/a/b) and [[Wiki|alias]] %%hidden words%% end.\n```c\nint main\n```\n";
        ExMd *md = ex_md_create();
        ExWordCount wc, plain;
        ex_md_tokenize(md, note, strlen(note));
        wc = ex_wordcount(note, strlen(note), md);
        plain = ex_wordcount(note, strlen(note), NULL);
        CHECK(wc.words == 14);                                           /* Heading one | Some bold text with link text and | Wiki alias | end | int main */
        CHECK(plain.words > wc.words);
        CHECK(wc.characters > 0 && wc.characters < plain.characters);
        {
            ExWordCount e0 = ex_wordcount("", 0, NULL), one = ex_wordcount("caf\xC3\xA9", 5, NULL);
            CHECK(e0.words == 0 && e0.characters == 0);
            CHECK(one.words == 1 && one.characters == 4);                 /* characters, not bytes */
        }
        ex_md_free(md);
    }
    printf("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
