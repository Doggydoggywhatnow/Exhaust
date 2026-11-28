/* hunt_extract: what the index takes out of a note (include/exhaust_extract.h) and the text surgery on properties (include/exhaust_props.h).
 *
 * Links, headings, block ids, tasks, tags and properties are what the graph, backlinks, search, rename and Bases are built from, and the property
 * editors rewrite the user's frontmatter: "replaces only those lines; no other byte of the note moves". This test
 *   A. checks the structure of every extraction on random and hostile notes: ranges inside the text and in order, lines that agree with the
 *      offsets, the text at a range really is the construct, sections that nest, nothing found inside code, properties inside the frontmatter;
 *   B. checks hand-written notes for what the documentation says each construct yields;
 *   C. runs random property edits (set, remove, rename, retype) on random frontmatter and checks that only the touched lines moved, the other properties
 *      read back the same, line endings and a byte-order mark survive, a note whose YAML is invalid is left alone, and values that would change meaning
 *      are quoted.
 * Link: exhaust_index. */
#include "hunt.h"

#include "exhaust_extract.h"
#include "exhaust_props.h"

/* ---- A. structure ---- */

static uint32_t brute_line(const char *t, size_t off) { uint32_t l = 1; size_t i; for (i = 0; i < off && i < strlen(t); i++) if (t[i] == '\n') l++; return l; }

static void check_structure(const char *text, size_t len, const char *what) {
    ExExtract *x = ex_extract(text, len);
    size_t i, nl = 1;
    char *copy = hunt_dup(text, len);
    if (!x) { HUNT_CHECKF(0, "%s: no extraction", what); free(copy); return; }
    for (i = 0; i < len; i++) if (copy[i] == '\n') nl++;
    HUNT_CHECKF(x->nlines == nl, "%s: %lu lines, the text has %lu", what, (unsigned long)x->nlines, (unsigned long)nl);
    if (x->nlines == nl) {
        size_t k = 0;
        HUNT_CHECK(x->line_starts[0] == 0);
        for (i = 0; i < len; i++) if (copy[i] == '\n') { k++; HUNT_CHECKF(x->line_starts[k] == i + 1, "%s: line %lu starts at %lu, want %lu", what, (unsigned long)k + 1, (unsigned long)x->line_starts[k], (unsigned long)i + 1); }
    }
    for (i = 0; i < len && i < 400; i += 1 + hunt_rnd_size(7)) HUNT_CHECKF(ex_extract_line_of(x, i) == brute_line(copy, i), "%s: line_of(%lu) = %u, want %u", what, (unsigned long)i, ex_extract_line_of(x, i), brute_line(copy, i));
    HUNT_CHECK(ex_extract_line_of(x, len) == (uint32_t)nl);
    HUNT_SUSPECT(ex_extract_line_of(x, len + 100) == (uint32_t)nl || ex_extract_line_of(x, len + 100) == 0, "line_of past the end should be the last line (or 0), not garbage");

    for (i = 0; i < x->nlinks; i++) {
        const ExXLink *l = &x->links[i];
        HUNT_CHECKF(l->start < l->end && l->end <= len, "%s: link %lu range %lu-%lu in a text of %lu", what, (unsigned long)i, (unsigned long)l->start, (unsigned long)l->end, (unsigned long)len);
        if (!(l->start < l->end && l->end <= len)) continue;
        HUNT_CHECKF(l->target != NULL, "%s: link %lu has no target", what, (unsigned long)i);
        HUNT_CHECKF(l->line == ex_extract_line_of(x, l->start), "%s: link %lu says line %u, its offset is on line %u", what, (unsigned long)i, l->line, ex_extract_line_of(x, l->start));
        if (l->kind == EX_LINK_WIKI || l->kind == EX_LINK_PROP) HUNT_CHECKF(copy[l->start] == '[' && copy[l->end - 1] == ']', "%s: wikilink %lu is \"%.*s\"", what, (unsigned long)i, (int)(l->end - l->start), copy + l->start);
        if (l->kind == EX_LINK_EMBED) HUNT_CHECKF(copy[l->start] == '!' && copy[l->start + 1] == '[' && copy[l->end - 1] == ']', "%s: embed %lu is \"%.*s\"", what, (unsigned long)i, (int)(l->end - l->start), copy + l->start);
        if (l->kind == EX_LINK_MD) HUNT_CHECKF(copy[l->start] == '[', "%s: markdown link %lu is \"%.*s\"", what, (unsigned long)i, (int)(l->end - l->start), copy + l->start);
        if (l->kind == EX_LINK_MD_EMBED) HUNT_CHECKF(copy[l->start] == '!', "%s: markdown image %lu is \"%.*s\"", what, (unsigned long)i, (int)(l->end - l->start), copy + l->start);
        HUNT_CHECK(l->kind >= 0 && l->kind <= 4);
        if (l->target) HUNT_CHECKF(!strchr(l->target, '\n') || l->kind == EX_LINK_MD || l->kind == EX_LINK_MD_EMBED, "%s: a link target with a line break: %.40s", what, l->target);
        if (l->kind == EX_LINK_WIKI && l->target) HUNT_CHECKF(!strchr(l->target, '|') && !strchr(l->target, '#'), "%s: the target of a wikilink still holds | or #: %s", what, l->target);
        if (i > 0 && x->links[i - 1].start > l->start) HUNT_CHECKF(0, "%s: links are not in the order of the note (%lu before %lu)", what, (unsigned long)x->links[i - 1].start, (unsigned long)l->start);
    }
    for (i = 0; i < x->nheadings; i++) {
        const ExXHeading *h = &x->headings[i];
        HUNT_CHECKF(h->level >= 1 && h->level <= 6, "%s: heading level %d", what, h->level);
        HUNT_CHECKF(h->start <= h->end && h->end <= h->section_end && h->section_end <= len, "%s: heading %lu ranges %lu %lu %lu in %lu", what, (unsigned long)i, (unsigned long)h->start, (unsigned long)h->end, (unsigned long)h->section_end, (unsigned long)len);
        HUNT_CHECKF(h->line == ex_extract_line_of(x, h->start), "%s: heading %lu says line %u", what, (unsigned long)i, h->line);
        HUNT_CHECK(h->text != NULL);
        if (i > 0) HUNT_CHECK(x->headings[i - 1].start < h->start);
        /* a section ends where the next heading of the same or a higher level starts */
        {
            size_t j, expect = len;
            for (j = i + 1; j < x->nheadings; j++) if (x->headings[j].level <= h->level) { expect = x->headings[j].start; break; }
            HUNT_CHECKF(h->section_end == expect, "%s: heading %lu (level %d) ends its section at %lu, the next heading of that level or higher starts at %lu", what, (unsigned long)i, h->level, (unsigned long)h->section_end, (unsigned long)expect);
        }
    }
    for (i = 0; i < x->nblocks; i++) {
        const ExXBlock *b = &x->blocks[i];
        HUNT_CHECKF(b->start <= b->end && b->end <= len && b->id && *b->id, "%s: block %lu", what, (unsigned long)i);
        if (b->id) { size_t k; for (k = 0; b->id[k]; k++) HUNT_CHECKF((b->id[k] >= '0' && b->id[k] <= '9') || (b->id[k] >= 'a' && b->id[k] <= 'z') || (b->id[k] >= 'A' && b->id[k] <= 'Z') || b->id[k] == '-', "%s: block id \"%s\" has a character that is not a letter, digit or -", what, b->id); }
        HUNT_CHECK(b->line == ex_extract_line_of(x, b->start) || b->line == ex_extract_line_of(x, b->end > 0 ? b->end - 1 : 0));
    }
    for (i = 0; i < x->ntasks; i++) {
        const ExXTask *t = &x->tasks[i];
        HUNT_CHECKF(t->start < t->end && t->end <= len && t->state >= 0 && t->state <= 2, "%s: task %lu", what, (unsigned long)i);
        HUNT_CHECK(t->line == ex_extract_line_of(x, t->start));
        HUNT_CHECK(t->text != NULL);
        if (t->start < t->end && t->end <= len) HUNT_CHECKF(!memchr(copy + t->start, '\n', t->end - t->start - 1), "%s: task %lu spans more than one line", what, (unsigned long)i);
    }
    for (i = 0; i < x->ntags; i++) {
        const ExXTag *t = &x->tags[i];
        HUNT_CHECKF(t->tag && *t->tag && t->tag[0] != '#' && t->start <= len, "%s: tag %lu \"%s\"", what, (unsigned long)i, t->tag ? t->tag : "(null)");
        if (t->tag) {
            size_t k; int nondigit = 0;
            for (k = 0; t->tag[k]; k++) if (t->tag[k] < '0' || t->tag[k] > '9') nondigit = 1;
            HUNT_SPECF(nondigit || t->in_props, "docs/formats.md, Markdown tokens: a tag must contain a character that is not a digit", "%s: the tag \"%s\" is only digits", what, t->tag);
            if (!t->in_props && t->start < len) HUNT_CHECKF(copy[t->start] == '#', "%s: tag %lu does not start at a #", what, (unsigned long)i);
        }
    }
    {
        size_t prev_end = 0;
        for (i = 0; i < x->nprops; i++) {
            const ExXProp *p = &x->props[i];
            HUNT_CHECKF(p->name && *p->name && p->start < p->end && p->end <= len && p->start >= prev_end, "%s: property %lu \"%s\" %lu-%lu after %lu", what, (unsigned long)i, p->name ? p->name : "", (unsigned long)p->start, (unsigned long)p->end, (unsigned long)prev_end);
            if (p->start < p->end && p->end <= len) {
                prev_end = p->end;
                HUNT_CHECKF(p->start >= x->front_start && p->end <= x->front_end + 1, "%s: property %lu is outside the frontmatter %lu-%lu", what, (unsigned long)i, (unsigned long)x->front_start, (unsigned long)x->front_end);
                HUNT_CHECKF(p->start == 0 || copy[p->start - 1] == '\n', "%s: property %lu does not start at the start of a line", what, (unsigned long)i);
                HUNT_CHECKF(p->end == len || copy[p->end - 1] == '\n', "%s: property %lu does not end after a line break", what, (unsigned long)i);
            }
            HUNT_CHECK(p->type >= 0 && p->type <= EX_PROP_LINKS && p->value != NULL);
        }
        if (!x->front_valid) HUNT_CHECKF(x->nprops == 0, "%s: invalid YAML but %lu properties", what, (unsigned long)x->nprops);
    }
    free(copy);
    ex_extract_free(x);
}

static void test_structure(void) {
    long rounds = hunt_iters(5000), r;
    static const char *const seeds[] = {
        "---\ntitle: T\ntags: [a, b]\nrelated: \"[[Other]]\"\n---\n# Head\n\ntext [[Link|alias]] #tag ^id1\n\n- [ ] todo\n- [x] done\n\n```\n[[not]] #not\n```\n",
        "# A\n## B\n### C\n# D\n\n[[x#A]] [[#B]] ![[img.png]] [m](A.md#B) ![i](p.png)\n",
        "---\nk: v\n---\n", "---\n---\n", "---\nbad: [\n---\ntext\n", "text\n---\nnot: front\n---\n",
    };
    for (r = 0; r < rounds; r++) {
        char text[1200];
        size_t len;
        int mode = (int)hunt_rnd(4);
        char *exact;
        if (mode == 0) len = hunt_random_valid_text(text, sizeof text, 80);
        else if (mode == 1) len = hunt_random_text(text, sizeof text, 80);
        else {
            const char *s = seeds[hunt_rnd(sizeof seeds / sizeof *seeds)];
            len = strlen(s);
            memcpy(text, s, len);
            len = hunt_mutate((unsigned char *)text, len, sizeof text - 8);
        }
        exact = hunt_dup(text, len);
        hunt_stepf("extract structure %ld", r);
        hunt_set_input(exact, len, "note text");
        check_structure(exact, len, "random note");
        hunt_clear_input();
        free(exact);
        if (hunt_.failures > 30) break;
    }
    hunt_step("extract: empty and tiny notes");
    { size_t k; for (k = 0; k < 4; k++) { const char *t[] = { "", "\n", "#", "[[" }; check_structure(t[k], strlen(t[k]), "tiny"); } }
}

/* ---- B. what each construct yields ---- */

static ExExtract *X(const char *text) { return ex_extract(text, strlen(text)); }

static void test_known(void) {
    ExExtract *x;
    hunt_step("known: wikilinks");
    x = X("a [[Note]] b [[Note|alias]] c [[Note#Head]] d [[Note#^blk]] e [[Note#Head|alias]] f [[#Same]] g [[]] h [[|only alias]]");
    HUNT_CHECK(x && x->nlinks >= 6);
    if (x && x->nlinks >= 6) {
        HUNT_CHECK(x->links[0].kind == EX_LINK_WIKI && !strcmp(x->links[0].target, "Note") && !x->links[0].heading && !x->links[0].alias);
        HUNT_CHECK(!strcmp(x->links[1].target, "Note") && x->links[1].alias && !strcmp(x->links[1].alias, "alias"));
        HUNT_CHECK(x->links[2].heading && !strcmp(x->links[2].heading, "Head") && !x->links[2].block);
        HUNT_CHECK(x->links[3].block && !strcmp(x->links[3].block, "blk") && !x->links[3].heading);
        HUNT_CHECK(x->links[4].heading && !strcmp(x->links[4].heading, "Head") && x->links[4].alias && !strcmp(x->links[4].alias, "alias"));
        HUNT_SPEC(!strcmp(x->links[5].target, "") && x->links[5].heading && !strcmp(x->links[5].heading, "Same"), "include/exhaust_extract.h: target is \"\" for a link inside the same note");
    }
    ex_extract_free(x);
    hunt_step("known: embeds, Markdown links and images");
    x = X("![[img.png]] ![[Note#H|300]] [t](Folder/Note.md) [u](<a b.md>) [v](a%20b.md#H) ![alt](pic.png) [w](https://x.y) [x](mailto:a@b)");
    HUNT_CHECK(x && x->nlinks >= 5);
    if (x && x->nlinks >= 5) {
        HUNT_CHECK(x->links[0].kind == EX_LINK_EMBED && !strcmp(x->links[0].target, "img.png"));
        HUNT_CHECK(x->links[1].kind == EX_LINK_EMBED && !strcmp(x->links[1].target, "Note") && x->links[1].heading && !strcmp(x->links[1].heading, "H"));
        HUNT_CHECK(x->links[2].kind == EX_LINK_MD && !strcmp(x->links[2].target, "Folder/Note.md"));
        HUNT_CHECK(x->links[3].kind == EX_LINK_MD && !strcmp(x->links[3].target, "a b.md"));
        HUNT_SPEC(x->links[4].kind == EX_LINK_MD && !strcmp(x->links[4].target, "a b.md") && x->links[4].heading && !strcmp(x->links[4].heading, "H"), "docs/formats.md, Index: target is decoded for Markdown links");
        HUNT_CHECK(x->nlinks < 6 || (x->links[5].kind == EX_LINK_MD_EMBED && !strcmp(x->links[5].target, "pic.png")));
        { size_t k, web = 0; for (k = 0; k < x->nlinks; k++) if (x->links[k].target && (strstr(x->links[k].target, "https:") || strstr(x->links[k].target, "mailto:"))) web++; HUNT_SPEC(web == 0, "docs/formats.md, Index: web links and other links with a scheme are not stored"); }
    }
    ex_extract_free(x);
    hunt_step("known: nothing inside code, comments or escapes");
    {
        const char *none[] = { "`[[a]]`", "```\n[[a]]\n```", "~~~\n[[a]]\n~~~", "    [[a]]", "\\[[a]]", "%%[[a]]%%", "<!-- [[a]] -->", "$[[a]]$", "`#tag`", "```\n#tag\n```", "<code>[[a]]</code>" };
        size_t k;
        for (k = 0; k < sizeof none / sizeof *none; k++) {
            x = X(none[k]);
            HUNT_SPECF(x && x->nlinks == 0 && x->ntags == 0, "docs/formats.md, Markdown tokens: code, comments and escapes hold no links or tags", "\"%s\" gave %lu links and %lu tags", none[k], x ? (unsigned long)x->nlinks : 0ul, x ? (unsigned long)x->ntags : 0ul);
            ex_extract_free(x);
        }
    }
    hunt_step("known: headings and sections");
    x = X("# One\ntext\n## Two ##\nmore\n### Three\n# Four\nlast\nSetext\n======\nSetext two\n------\n####### seven\n#nospace\n");
    HUNT_CHECK(x && x->nheadings >= 6);
    if (x && x->nheadings >= 6) {
        HUNT_CHECK(x->headings[0].level == 1 && !strcmp(x->headings[0].text, "One"));
        HUNT_SUSPECT(!strcmp(x->headings[1].text, "Two"), "the closing # of an ATX heading is not part of its text");
        HUNT_CHECK(x->headings[2].level == 3);
        HUNT_CHECK(x->headings[3].level == 1 && !strcmp(x->headings[3].text, "Four"));
        HUNT_CHECK(x->headings[4].level == 1 && !strcmp(x->headings[4].text, "Setext"));
        HUNT_CHECK(x->headings[5].level == 2 && !strcmp(x->headings[5].text, "Setext two"));
        HUNT_CHECK(x->nheadings == 6);              /* seven hashes and #nospace are not headings */
    }
    ex_extract_free(x);
    hunt_step("known: tasks, tags, block ids");
    x = X("- [ ] open task\n- [x] done task\n- [X] big X\n- [-] other\n  - [ ] nested\n1. [ ] numbered\n* [ ] star\n+ [x] plus\n[ ] not a list\n- [] no\n- [ ]no space\n#a #b/c #1 #2024x text#no (#paren)\nline ^blk-1\n> quote ^q1\n- item ^i1\n");
    HUNT_CHECK(x && x->ntasks >= 7);
    if (x && x->ntasks >= 7) {
        HUNT_CHECK(x->tasks[0].state == 0 && x->tasks[1].state == 1 && x->tasks[2].state == 1 && x->tasks[3].state == 2 && x->tasks[4].state == 0 && x->tasks[5].state == 0);
        HUNT_CHECK(!strcmp(x->tasks[0].text, "open task"));
    }
    HUNT_SUSPECT(x && x->ntasks == 8, "a task is a list item with [ ] and then a space: `- [ ]no space` is not one, `[ ] not a list` is not one");
    if (x) {
        size_t k, found_a = 0, found_bc = 0, found_1 = 0, found_no = 0, found_paren = 0;
        for (k = 0; k < x->ntags; k++) { if (!strcmp(x->tags[k].tag, "a")) found_a++; if (!strcmp(x->tags[k].tag, "b/c")) found_bc++; if (!strcmp(x->tags[k].tag, "1")) found_1++; if (!strcmp(x->tags[k].tag, "no")) found_no++; if (!strcmp(x->tags[k].tag, "paren")) found_paren++; }
        HUNT_CHECK(found_a == 1 && found_bc == 1 && found_1 == 0 && found_no == 0 && found_paren == 1);
        HUNT_CHECK(x->nblocks == 3 && x->nblocks == 3 && x->blocks[0].id && !strcmp(x->blocks[0].id, "blk-1"));
    }
    ex_extract_free(x);
    hunt_step("known: properties");
    x = X("---\ntitle: Hello\ntags: [a, b]\naliases:\n  - A1\n  - A2\nn: 42\nf: 3.5\nneg: -7\ndone: true\nno: false\nd: 2024-02-29\ndt: 2024-02-29T10:30:00\ndt2: 2024-02-29 10:30\nq: \"2024-02-29\"\nl: \"[[Note]]\"\nl2: [[Note]]\nempty:\ntxt: a: b\nlst: [x, y]\nn2: '5'\nbig: 1e3\n---\nbody\n");
    HUNT_CHECK(x && x->front_valid);
    if (x && x->front_valid) {
        struct { const char *name; int type; } ty[] = { { "title", EX_PROP_TEXT }, { "tags", EX_PROP_LIST }, { "aliases", EX_PROP_LIST }, { "n", EX_PROP_NUMBER }, { "f", EX_PROP_NUMBER }, { "neg", EX_PROP_NUMBER }, { "done", EX_PROP_CHECKBOX },
            { "no", EX_PROP_CHECKBOX }, { "d", EX_PROP_DATE }, { "dt", EX_PROP_DATETIME }, { "dt2", EX_PROP_DATETIME }, { "q", EX_PROP_TEXT }, { "l", EX_PROP_LINKS }, { "lst", EX_PROP_LIST }, { "n2", EX_PROP_TEXT } };
        size_t k, j;
        for (k = 0; k < sizeof ty / sizeof *ty; k++) {
            int found = 0;
            for (j = 0; j < x->nprops; j++) if (!strcmp(x->props[j].name, ty[k].name)) { found = 1; HUNT_SPECF(x->props[j].type == ty[k].type, "docs/formats.md, Properties: types are read from the value", "property %s has type %d, want %d (value \"%s\")", ty[k].name, x->props[j].type, ty[k].type, x->props[j].value); }
            HUNT_CHECKF(found, "property %s was not found", ty[k].name);
        }
        for (j = 0; j < x->nprops; j++) if (!strcmp(x->props[j].name, "aliases")) HUNT_SPEC(!strcmp(x->props[j].value, "A1\nA2"), "include/exhaust_extract.h: a list's items are joined by a newline");
    }
    ex_extract_free(x);
    hunt_step("known: frontmatter that has no properties");
    {
        const char *noprops[] = { "---\ntitle: a\ntitle: b\n---\n", "---\n- a\n- b\n---\n", "---\n{a: 1}\n---\n", "---\na: [\n---\n", "---\na: 1\n  b: 2\n---\n", "---\n\ta: 1\n---\n", "---\na: &x 1\nb: *y\n---\n", "no front\n---\na: 1\n---\n", "---\nunterminated: 1\n" };
        size_t k;
        for (k = 0; k < sizeof noprops / sizeof *noprops; k++) {
            x = X(noprops[k]);
            HUNT_SPECF(x && x->nprops == 0, "docs/formats.md, Index: a frontmatter that is not valid YAML (or has a flow mapping at the top, a key that is not at the start of a line, or a key twice) has no properties", "\"%s\" gave %lu properties", noprops[k], x ? (unsigned long)x->nprops : 0ul);
            ex_extract_free(x);
        }
    }
    hunt_step("known: links in properties and tags in properties");
    x = X("---\nrelated: \"[[Note]]\"\nlist:\n  - \"[[A]]\"\n  - \"[[B|b]]\"\ntags: [x, y/z]\nmore: tag1, tag2\n---\nbody [[Body]] #inbody\n");
    if (x) {
        size_t k, prop_links = 0, body_links = 0, prop_tags = 0, body_tags = 0;
        for (k = 0; k < x->nlinks; k++) { if (x->links[k].kind == EX_LINK_PROP) prop_links++; else body_links++; }
        for (k = 0; k < x->ntags; k++) { if (x->tags[k].in_props) prop_tags++; else body_tags++; }
        HUNT_CHECKF(prop_links == 3 && body_links == 1, "%lu links in properties and %lu in the body, want 3 and 1", (unsigned long)prop_links, (unsigned long)body_links);
        HUNT_CHECKF(prop_tags >= 2 && body_tags == 1, "%lu tags in properties and %lu in the body, want at least 2 and 1", (unsigned long)prop_tags, (unsigned long)body_tags);
    }
    ex_extract_free(x);
    hunt_step("known: CRLF and a byte-order mark are not part of any name");
    x = X("---\r\ntitle: T\r\n---\r\n# Head\r\n\r\n[[Link]] #tag\r\n- [ ] task\r\n");
    if (x) {
        HUNT_CHECK(x->nheadings == 1 && x->headings[0].text && !strchr(x->headings[0].text, '\r'));
        HUNT_CHECK(x->nlinks == 1 && !strchr(x->links[0].target, '\r'));
        HUNT_CHECK(x->ntags == 1 && !strchr(x->tags[0].tag, '\r'));
        HUNT_CHECK(x->ntasks == 1 && x->tasks[0].text && !strchr(x->tasks[0].text, '\r'));
        HUNT_CHECK(x->nprops == 1 && x->props[0].value && !strchr(x->props[0].value, '\r'));
    }
    ex_extract_free(x);
}

/* ---- C. property surgery ---- */

static const char *const FM_LINES[] = {
    "title: Hello\n", "tags: [a, b]\n", "tags:\n  - x\n  - y\n", "aliases: A, B\n", "date: 2024-01-31\n", "n: 42\n", "done: true\n", "link: \"[[Note]]\"\n", "empty:\n", "s: 'single'\n", "k: \"dq: colon\"\n",
    "# a comment\n", "m:\n  a: 1\n  b: 2\n", "t: |\n  text\n  more\n", "url: http://x.y/z#a\n", "k2: a #comment\n", "list: [1, 2, 3]\n", "cssclasses: wide\n", "other_key: some value\n", "z: 0\n", "\n",
};
#define NFM (sizeof FM_LINES / sizeof *FM_LINES)

static const ExXProp *find_prop(const ExExtract *x, const char *name) { size_t i; for (i = 0; i < x->nprops; i++) if (!strcmp(x->props[i].name, name)) return &x->props[i]; return NULL; }

/* The common prefix and suffix of two texts: a pure edit in one place has cp + cs >= the shorter text. */
static void common_ends(const char *a, size_t al, const char *b, size_t bl, size_t *cp, size_t *cs) {
    size_t p = 0, s = 0, lim = al < bl ? al : bl;
    while (p < lim && a[p] == b[p]) p++;
    while (s < lim - p && a[al - 1 - s] == b[bl - 1 - s]) s++;
    *cp = p; *cs = s;
}

static void check_others_unchanged(const ExExtract *before, const ExExtract *after, const char *skip1, const char *skip2, const char *what) {
    size_t i;
    for (i = 0; i < before->nprops; i++) {
        const ExXProp *b = &before->props[i], *a;
        if ((skip1 && !strcmp(b->name, skip1)) || (skip2 && !strcmp(b->name, skip2))) continue;
        a = find_prop(after, b->name);
        HUNT_CHECKF(a != NULL, "%s: the property %s disappeared", what, b->name);
        if (a) HUNT_CHECKF(a->type == b->type && !strcmp(a->value, b->value), "%s: the property %s changed from \"%s\" (%d) to \"%s\" (%d)", what, b->name, b->value, b->type, a->value, a->type);
    }
    for (i = 0; i < after->nprops; i++) {
        const ExXProp *a = &after->props[i];
        if ((skip1 && !strcmp(a->name, skip1)) || (skip2 && !strcmp(a->name, skip2))) continue;
        HUNT_CHECKF(find_prop(before, a->name) != NULL, "%s: the property %s appeared", what, a->name);
    }
}

static size_t gen_front(char *out, size_t cap, int crlf, int bom, int trailing_body) {
    size_t n = 0, k, nl = hunt_rnd_size(8);
    char tmp[400];
    size_t tn = 0;
    int used[NFM] = { 0 };
    if (bom) { memcpy(out, "\xEF\xBB\xBF", 3); n = 3; }
    memcpy(out + n, "---\n", 4); n += 4;
    for (k = 0; k < nl; k++) {
        size_t pick = hunt_rnd_size(NFM);
        if (used[pick] && pick != 11 && pick != 20) continue;
        used[pick] = 1;
        tn = strlen(FM_LINES[pick]);
        memcpy(tmp, FM_LINES[pick], tn);
        if (n + tn + 20 < cap) { memcpy(out + n, tmp, tn); n += tn; }
    }
    memcpy(out + n, "---\n", 4); n += 4;
    if (trailing_body) { const char *b = "# Body\n\ntext [[Link]]\n"; size_t bl = strlen(b); memcpy(out + n, b, bl); n += bl; }
    out[n] = 0;
    if (crlf) {
        char *c = (char *)malloc(n * 2 + 8);
        size_t j = 0, i;
        for (i = 0; i < n; i++) { if (out[i] == '\n') c[j++] = '\r'; c[j++] = out[i]; }
        memcpy(out, c, j); n = j; out[n] = 0;
        free(c);
    }
    return n;
}

static void test_surgery(void) {
    long rounds = hunt_iters(6000), r;
    struct { const char *name; int type; const char *value; } vals[] = {
        { "title", EX_PROP_TEXT, "New title" }, { "title", EX_PROP_TEXT, "true" }, { "title", EX_PROP_TEXT, "123" }, { "title", EX_PROP_TEXT, "a: b" }, { "title", EX_PROP_TEXT, "x #y" }, { "title", EX_PROP_TEXT, "- dash" }, { "title", EX_PROP_TEXT, "[brackets]" },
        { "title", EX_PROP_TEXT, "{braces}" }, { "title", EX_PROP_TEXT, "\"quoted\"" }, { "title", EX_PROP_TEXT, "it's" }, { "title", EX_PROP_TEXT, "&anchor" }, { "title", EX_PROP_TEXT, "*alias" }, { "title", EX_PROP_TEXT, "!tag" }, { "title", EX_PROP_TEXT, "" },
        { "title", EX_PROP_TEXT, "null" }, { "title", EX_PROP_TEXT, "~" }, { "title", EX_PROP_TEXT, "2024-01-01" }, { "title", EX_PROP_TEXT, " lead" }, { "title", EX_PROP_TEXT, "trail " }, { "title", EX_PROP_TEXT, "\xC3\xA9\xF0\x9F\x98\x80" },
        { "tags", EX_PROP_LIST, "a\nb\nc" }, { "tags", EX_PROP_LIST, "single" }, { "tags", EX_PROP_LIST, "with space\nx: y\n#hash" }, { "tags", EX_PROP_LIST, "" }, { "n", EX_PROP_NUMBER, "7" }, { "n", EX_PROP_NUMBER, "-3.5" }, { "n", EX_PROP_NUMBER, "abc" },
        { "done", EX_PROP_CHECKBOX, "false" }, { "done", EX_PROP_CHECKBOX, "true" }, { "done", EX_PROP_CHECKBOX, "maybe" }, { "date", EX_PROP_DATE, "2024-02-29" }, { "date", EX_PROP_DATE, "2024-13-45" }, { "dt", EX_PROP_DATETIME, "2024-02-29T10:30" },
        { "link", EX_PROP_LINKS, "[[Other]]" }, { "link", EX_PROP_LINKS, "[[A]]\n[[B]]" }, { "newprop", EX_PROP_TEXT, "added" }, { "newlist", EX_PROP_LIST, "p\nq" }, { "new num", EX_PROP_TEXT, "bad name" }, { "bad:name", EX_PROP_TEXT, "x" }, { "bad#name", EX_PROP_TEXT, "x" },
        { "bad\"name", EX_PROP_TEXT, "x" }, { "bad[name", EX_PROP_TEXT, "x" }, { "bad,name", EX_PROP_TEXT, "x" }, { "", EX_PROP_TEXT, "x" }, { "bad\nname", EX_PROP_TEXT, "x" },
    };
    for (r = 0; r < rounds; r++) {
        char text[900];
        size_t len, n2 = 0;
        int crlf = hunt_chance(25), bom = hunt_chance(15), body = hunt_chance(70);
        size_t vi = hunt_rnd_size(sizeof vals / sizeof *vals);
        const char *name = vals[vi].name;
        char *edited;
        ExExtract *before, *after;
        const ExXProp *pb;
        int op = (int)hunt_rnd(4);
        char what[200];
        len = gen_front(text, sizeof text, crlf, bom, body);
        snprintf(what, sizeof what, "surgery %ld op %d name '%s'", r, op, name);
        hunt_stepf("%s", what);
        hunt_set_input(text, len, "note text");
        before = ex_extract(text, len);
        pb = before ? find_prop(before, name) : NULL;
        if (op == 0) {                                      /* set */
            edited = ex_prop_text_set(text, len, name, vals[vi].type, vals[vi].value, &n2);
            if (!edited) { HUNT_CHECKF(!pb || !*name || 1, "%s", what); }
            else {
                size_t cp, cs;
                after = ex_extract(edited, n2);
                common_ends(text, len, edited, n2, &cp, &cs);
                if (pb) HUNT_CHECKF(cp >= pb->start && cs >= len - pb->end, "%s: bytes outside the lines of the property changed (common start %lu of %lu, common end %lu of %lu)", what, (unsigned long)cp, (unsigned long)pb->start, (unsigned long)cs, (unsigned long)(len - pb->end));
                else HUNT_CHECKF(cp + cs >= len, "%s: setting a new property changed bytes that were there (common %lu + %lu of %lu)", what, (unsigned long)cp, (unsigned long)cs, (unsigned long)len);
                if (after) {
                    const ExXProp *pa = find_prop(after, name);
                    HUNT_CHECKF(pa != NULL, "%s: the property is not there after setting it:\n%s", what, edited);
                    if (pa && vals[vi].type == EX_PROP_TEXT && vals[vi].value[0]) HUNT_SPECF(pa->type == EX_PROP_TEXT || pa->type == EX_PROP_LINKS, "docs/formats.md, Properties: values that would change meaning without quotes are written in double quotes", "%s: text \"%s\" read back as type %d (value \"%s\") from:\n%s", what, vals[vi].value, pa->type, pa->value, edited);
                    if (pa && vals[vi].type == EX_PROP_TEXT && vals[vi].value[0] && pa->type == EX_PROP_TEXT) HUNT_CHECKF(!strcmp(pa->value, vals[vi].value) || (pa->value[0] == '"' || pa->value[0] == '\''), "%s: text \"%s\" read back as \"%s\"", what, vals[vi].value, pa->value);
                    if (pa && vals[vi].type == EX_PROP_LIST) { HUNT_SPECF(pa->type == EX_PROP_LIST || !vals[vi].value[0], "docs/formats.md, Properties: a list is written as a block list", "%s: list read back as type %d\n%s", what, pa->type, edited); }
                    if (pa && vals[vi].type == EX_PROP_LIST && pa->type == EX_PROP_LIST && vals[vi].value[0] && !strchr(vals[vi].value, ':') && !strchr(vals[vi].value, '#')) HUNT_CHECKF(!strcmp(pa->value, vals[vi].value), "%s: list \"%s\" read back as \"%s\"", what, vals[vi].value, pa->value);
                    if (before) check_others_unchanged(before, after, name, NULL, what);
                } else HUNT_CHECKF(0, "%s: no extraction of the edited text", what);
                /* a name or value that cannot be written must have been refused */
                if (strpbrk(name, ":#\"'[]{},\r\n") || !*name) HUNT_SPECF(0, "docs/formats.md, Properties: property names cannot contain : # \" ' [ ] { } , or a control or line-break character", "%s: the name was accepted and gave:\n%s", what, edited);
                HUNT_CHECKF(bom == 0 || (n2 >= 3 && !memcmp(edited, "\xEF\xBB\xBF", 3)), "%s: the byte-order mark was lost", what);
                if (crlf) { size_t i, bare = 0; for (i = 0; i < n2; i++) if (edited[i] == '\n' && (i == 0 || edited[i - 1] != '\r')) bare++; HUNT_CHECKF(bare == 0, "%s: %lu bare LF in a CRLF note after setting a property", what, (unsigned long)bare); }
                if (after) ex_extract_free(after);
                free(edited);
            }
        } else if (op == 1) {                               /* remove */
            edited = ex_prop_text_remove(text, len, name, &n2);
            if (pb) {
                HUNT_CHECKF(edited != NULL, "%s: removing an existing property gave NULL", what);
                if (edited) {
                    size_t cp, cs;
                    after = ex_extract(edited, n2);
                    common_ends(text, len, edited, n2, &cp, &cs);
                    HUNT_CHECKF(n2 == len - (pb->end - pb->start) && cp >= pb->start && cs >= len - pb->end, "%s: removing the property changed bytes outside its lines (%lu bytes left of %lu)", what, (unsigned long)n2, (unsigned long)(len - (pb->end - pb->start)));
                    if (after) { HUNT_CHECKF(find_prop(after, name) == NULL, "%s: the property is still there", what); if (before) check_others_unchanged(before, after, name, NULL, what); ex_extract_free(after); }
                }
            } else HUNT_CHECKF(edited == NULL || (n2 == len && !memcmp(edited, text, len)), "%s: removing a property that is not there changed the text", what);
            free(edited);
        } else if (op == 2) {                               /* rename */
            const char *to = vals[hunt_rnd_size(sizeof vals / sizeof *vals)].name;
            edited = ex_prop_text_rename(text, len, name, to, &n2);
            if (edited && pb && strcmp(name, to)) {
                size_t cp, cs;
                after = ex_extract(edited, n2);
                common_ends(text, len, edited, n2, &cp, &cs);
                HUNT_CHECKF(cp >= pb->start && cs >= len - pb->end, "%s: renaming to '%s' changed bytes outside the property's lines", what, to);
                if (after) {
                    const ExXProp *pa = find_prop(after, to);
                    HUNT_CHECKF(pa != NULL, "%s: the renamed property is not under its new name", what);
                    if (pa) HUNT_CHECKF(pa->type == pb->type && !strcmp(pa->value, pb->value), "%s: renaming changed the value (\"%s\" to \"%s\")", what, pb->value, pa->value);
                    if (before && !find_prop(before, to)) check_others_unchanged(before, after, name, to, what);
                    HUNT_SUSPECT(find_prop(before, to) == NULL, "renaming a property to the name of another one that exists should be refused, or it makes a key twice (and then neither reads)");
                    ex_extract_free(after);
                }
                if (strpbrk(to, ":#\"'[]{},\r\n") || !*to) HUNT_SPECF(0, "docs/formats.md, Properties: property names cannot contain : # \" ' [ ] { } , or a control or line-break character", "%s: renamed to a bad name '%s' and gave:\n%s", what, to, edited);
            }
            if (!pb) HUNT_CHECKF(edited == NULL || (n2 == len && !memcmp(edited, text, len)), "%s: renaming a property that is not there changed the text", what);
            free(edited);
        } else {                                            /* retype */
            int to = (int)hunt_rnd(7);
            edited = ex_prop_text_retype(text, len, name, to, &n2);
            if (edited && pb) {
                size_t cp, cs;
                common_ends(text, len, edited, n2, &cp, &cs);
                HUNT_CHECKF(cp >= pb->start && cs >= len - pb->end, "%s: retyping changed bytes outside the property's lines", what);
                after = ex_extract(edited, n2);
                if (after) { if (before) check_others_unchanged(before, after, name, NULL, what); HUNT_CHECKF(find_prop(after, name) != NULL, "%s: the retyped property is gone", what); ex_extract_free(after); }
            }
            free(edited);
        }
        ex_extract_free(before);
        hunt_clear_input();
        if (hunt_.failures > 40) break;
    }

    hunt_step("surgery: a note without frontmatter gets one at the top, a note with invalid YAML is left alone");
    {
        const char *t = "# Title\n\nbody [[x]]\n";
        size_t n = 0;
        char *e = ex_prop_text_set(t, strlen(t), "status", EX_PROP_TEXT, "draft", &n);
        HUNT_SPEC(e && n > strlen(t) && !strcmp(e + n - strlen(t), t) && !strncmp(e, "---\n", 4), "docs/formats.md, Properties: a note without frontmatter gets one at the top");
        free(e);
        {
            const char *bad[] = { "---\nbad: [\n---\nbody\n", "---\na: 1\n  b: 2\n---\n", "---\na: b: c\n---\n", "---\n\ta: 1\n---\n" };
            size_t k;
            for (k = 0; k < sizeof bad / sizeof *bad; k++) {
                char *s1 = ex_prop_text_set(bad[k], strlen(bad[k]), "x", EX_PROP_TEXT, "y", &n), *s2 = ex_prop_text_remove(bad[k], strlen(bad[k]), "a", &n), *s3 = ex_prop_text_rename(bad[k], strlen(bad[k]), "a", "z", &n), *s4 = ex_prop_text_retype(bad[k], strlen(bad[k]), "a", EX_PROP_LIST, &n);
                HUNT_SPECF(s1 == NULL && s2 == NULL && s3 == NULL && s4 == NULL, "docs/formats.md, Properties: a note whose frontmatter is not valid YAML is never edited", "note %lu was edited (set %p remove %p rename %p retype %p)", (unsigned long)k, (void *)s1, (void *)s2, (void *)s3, (void *)s4);
                free(s1); free(s2); free(s3); free(s4);
            }
        }
        e = ex_prop_text_set("", 0, "a", EX_PROP_TEXT, "b", &n);
        HUNT_CHECK(e != NULL);
        free(e);
        e = ex_prop_text_remove("", 0, "a", &n);
        HUNT_CHECK(e == NULL || n == 0);
        free(e);
    }
    hunt_step("surgery: set then remove gives back the note");
    {
        long k;
        for (k = 0; k < hunt_iters(600); k++) {
            char text[900];
            size_t len = gen_front(text, sizeof text, hunt_chance(20), hunt_chance(10), 1), n1 = 0, n2 = 0;
            char *a, *b;
            { ExExtract *xx = ex_extract(text, len); int has = xx && find_prop(xx, "zzz_new"); ex_extract_free(xx); if (has) continue; }
            a = ex_prop_text_set(text, len, "zzz_new", EX_PROP_TEXT, "value", &n1);
            if (!a) continue;
            b = ex_prop_text_remove(a, n1, "zzz_new", &n2);
            HUNT_SUSPECT(b && n2 == len && !memcmp(b, text, len), "adding a property and removing it again should give the note back byte for byte");
            if (b && (n2 != len || memcmp(b, text, len))) hunt_note_("before (%lu bytes):\n%s\nafter (%lu bytes):\n%s", (unsigned long)len, text, (unsigned long)n2, b);
            free(a); free(b);
            if (hunt_.suspects > 10) break;
        }
    }
}

int main(int argc, char **argv) {
    hunt_begin("hunt_extract", argc, argv);
    test_structure();
    test_known();
    test_surgery();
    return hunt_end();
}
