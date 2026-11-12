#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_fuzzy.h"
#include "exhaust_slash.h"

/* ---- fuzzy matching ---- */

static int word_start(const char *t, size_t i) {
    if (i == 0) return 1;
    return t[i - 1] == ' ' || t[i - 1] == '-' || t[i - 1] == '_' || t[i - 1] == ':' || t[i - 1] == '/' || t[i - 1] == '.';
}

int ex_fuzzy_score(const char *query, const char *text) {
    size_t qn = strlen(query), tn = strlen(text), qi, ti = 0, prev = (size_t)-1, first = 0;
    int score = 0;
    if (qn == 0) return 0;
    for (qi = 0; qi < qn; qi++) {
        size_t found = (size_t)-1, k;
        char qc = (char)tolower((unsigned char)query[qi]);
        for (k = ti; k < tn; k++)                                   /* prefer the start of a word, or the character right after the last */
            if ((char)tolower((unsigned char)text[k]) == qc && (word_start(text, k) || (prev != (size_t)-1 && k == prev + 1))) { found = k; break; }
        if (found == (size_t)-1)
            for (k = ti; k < tn; k++) if ((char)tolower((unsigned char)text[k]) == qc) { found = k; break; }
        if (found == (size_t)-1) return -1;
        if (qi == 0) first = found;
        score += 10;
        if (word_start(text, found)) score += 20;
        if (prev != (size_t)-1 && found == prev + 1) score += 15;
        prev = found;
        ti = found + 1;
    }
    score -= (int)(first > 10 ? 10 : first);
    {   /* the whole query as one piece of the text */
        size_t k;
        for (k = 0; k + qn <= tn; k++) {
            size_t j;
            for (j = 0; j < qn; j++) if (tolower((unsigned char)text[k + j]) != tolower((unsigned char)query[j])) break;
            if (j == qn) { score += 30; if (k == 0) score += 20; break; }
        }
    }
    return score < 0 ? 0 : score;
}

/* ---- the menu ---- */

struct ExSlashMenu {
    ExCommands *commands;
    char query[128];
    ExSlashItem *items;
    size_t n, cap, selected;
    size_t *all;               /* indexes in the command table of the editor commands, in table order */
    size_t nall;
};

static void rebuild(ExSlashMenu *m) {
    size_t i, k;
    m->n = 0;
    for (i = 0; i < m->nall; i++) {
        const ExCommandDef *d = ex_commands_at(m->commands, m->all[i]);
        int score = ex_fuzzy_score(m->query, d->name);
        if (score < 0) continue;
        m->items[m->n].id = d->id;
        m->items[m->n].name = d->name;
        m->items[m->n].available = ex_commands_available(m->commands, d->id);
        m->items[m->n].score = score;
        m->n++;
    }
    if (m->query[0]) {                                      /* best first; the sort is stable, so ties stay in table order */
        for (i = 1; i < m->n; i++) {
            ExSlashItem it = m->items[i];
            for (k = i; k > 0 && m->items[k - 1].score < it.score; k--) m->items[k] = m->items[k - 1];
            m->items[k] = it;
        }
    }
    m->selected = 0;
}

ExSlashMenu *ex_slash_create(ExCommands *commands) {
    ExSlashMenu *m = calloc(1, sizeof *m);
    size_t i, total;
    if (!m) return NULL;
    m->commands = commands;
    total = ex_commands_count(commands);
    m->all = malloc((total ? total : 1) * sizeof *m->all);
    m->items = malloc((total ? total : 1) * sizeof *m->items);
    if (!m->all || !m->items) { ex_slash_free(m); return NULL; }
    for (i = 0; i < total; i++) if (ex_commands_at(commands, i)->editor) m->all[m->nall++] = i;
    m->cap = total;
    rebuild(m);
    return m;
}

void ex_slash_free(ExSlashMenu *m) {
    if (!m) return;
    free(m->all);
    free(m->items);
    free(m);
}

void ex_slash_set_query(ExSlashMenu *m, const char *query) {
    size_t n = strlen(query);
    if (n >= sizeof m->query) n = sizeof m->query - 1;
    memcpy(m->query, query, n);
    m->query[n] = 0;
    rebuild(m);
}

const char *ex_slash_query(const ExSlashMenu *m) { return m->query; }
size_t ex_slash_count(const ExSlashMenu *m) { return m->n; }
const ExSlashItem *ex_slash_item(const ExSlashMenu *m, size_t i) { return i < m->n ? &m->items[i] : NULL; }
size_t ex_slash_selected(const ExSlashMenu *m) { return m->selected; }
void ex_slash_select(ExSlashMenu *m, size_t i) { if (i < m->n) m->selected = i; }

void ex_slash_move(ExSlashMenu *m, int delta) {
    long s;
    if (!m->n) return;
    s = ((long)m->selected + delta) % (long)m->n;
    if (s < 0) s += (long)m->n;
    m->selected = (size_t)s;
}

int ex_slash_run(ExSlashMenu *m) {
    if (!m->n) return -1;
    return ex_commands_run(m->commands, m->items[m->selected].id);
}
