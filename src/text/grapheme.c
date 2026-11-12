/* Extended grapheme cluster boundaries (Unicode UAX #29, with the GB9c Indic
 * conjunct rule), from tables generated out of Unicode 17.0.0 data. */
#include "exhaust_text.h"
#include "grapheme_data.h"

typedef enum { GCB_OTHER, GCB_CR, GCB_LF, GCB_CONTROL, GCB_EXTEND, GCB_ZWJ, GCB_RI, GCB_PREPEND,
               GCB_SPACING, GCB_L, GCB_V, GCB_T, GCB_LV, GCB_LVT } Gcb;
enum { INCB_NONE, INCB_CONSONANT, INCB_LINKER, INCB_EXTEND };

static int lookup(const ExUcdRange *t, int n, uint32_t cp) {
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (cp < t[mid].lo) hi = mid - 1;
        else if (cp > t[mid].hi) lo = mid + 1;
        else return t[mid].v;
    }
    return 0;
}

int ex_is_extended_pictographic(uint32_t cp) { return lookup(ex_ucd_extpict, ex_ucd_extpict_count, cp) != 0; }
int ex_is_emoji_presentation(uint32_t cp) { return lookup(ex_ucd_emoji_presentation, ex_ucd_emoji_presentation_count, cp) != 0; }

static Gcb gcb_of(uint32_t cp) { return (Gcb)lookup(ex_ucd_gcb, ex_ucd_gcb_count, cp); }

size_t ex_grapheme_next(const char *s, size_t len, size_t pos) {
    size_t i = pos;
    uint32_t cp;
    Gcb prev;
    int pict_seq;      /* GB11: ExtPict Extend* seen, ZWJ may follow */
    int pict_zwj;      /* GB11: ExtPict Extend* ZWJ just seen */
    int ri_count;      /* GB12/13 */
    int incb_state;    /* GB9c: 0 none, 1 consonant [extend]*, 2 consonant ... linker ... */
    if (pos >= len) return len;

    cp = ex_utf8_decode(s, len, &i);
    prev = gcb_of(cp);
    pict_seq = ex_is_extended_pictographic(cp);
    pict_zwj = 0;
    ri_count = prev == GCB_RI;
    {
        int ib = lookup(ex_ucd_incb, ex_ucd_incb_count, cp);
        incb_state = ib == INCB_CONSONANT ? 1 : 0;
    }

    while (i < len) {
        size_t next = i;
        uint32_t c2 = ex_utf8_decode(s, len, &next);
        Gcb cur = gcb_of(c2);
        int ib = lookup(ex_ucd_incb, ex_ucd_incb_count, c2);
        int brk = 1;

        if (prev == GCB_CR && cur == GCB_LF) brk = 0;                                   /* GB3 */
        else if (prev == GCB_CONTROL || prev == GCB_CR || prev == GCB_LF) brk = 1;      /* GB4 */
        else if (cur == GCB_CONTROL || cur == GCB_CR || cur == GCB_LF) brk = 1;         /* GB5 */
        else if (prev == GCB_L && (cur == GCB_L || cur == GCB_V || cur == GCB_LV || cur == GCB_LVT)) brk = 0; /* GB6 */
        else if ((prev == GCB_LV || prev == GCB_V) && (cur == GCB_V || cur == GCB_T)) brk = 0;  /* GB7 */
        else if ((prev == GCB_LVT || prev == GCB_T) && cur == GCB_T) brk = 0;                   /* GB8 */
        else if (cur == GCB_EXTEND || cur == GCB_ZWJ) brk = 0;                          /* GB9 */
        else if (cur == GCB_SPACING) brk = 0;                                           /* GB9a */
        else if (prev == GCB_PREPEND) brk = 0;                                          /* GB9b */
        else if (incb_state == 2 && ib == INCB_CONSONANT) brk = 0;                      /* GB9c */
        else if (pict_zwj && ex_is_extended_pictographic(c2)) brk = 0;                  /* GB11 */
        else if (prev == GCB_RI && cur == GCB_RI && (ri_count & 1)) brk = 0;            /* GB12, GB13 */

        if (brk) break;

        /* update the rule state for the character just absorbed */
        pict_zwj = pict_seq && cur == GCB_ZWJ;
        if (ex_is_extended_pictographic(c2)) pict_seq = 1;
        else if (cur != GCB_EXTEND && cur != GCB_ZWJ) pict_seq = 0;
        ri_count = cur == GCB_RI ? ri_count + 1 : 0;
        if (incb_state >= 1) {
            if (ib == INCB_LINKER) incb_state = 2;
            else if (ib == INCB_EXTEND) { /* stays */ }
            else incb_state = ib == INCB_CONSONANT ? 1 : 0;
        }
        prev = cur;
        i = next;
    }
    return i;
}

size_t ex_grapheme_prev(const char *s, size_t len, size_t pos) {
    size_t b = 0, n;
    if (pos == 0) return 0;
    if (pos > len) pos = len;
    while ((n = ex_grapheme_next(s, len, b)) < pos && n > b) b = n;
    return b;
}
