/* Tables for Unicode normalization and case folding (generated into nfc_data.c). */
#ifndef EXHAUST_NFC_DATA_H
#define EXHAUST_NFC_DATA_H
typedef struct ExNfdEntry { unsigned int cp; unsigned short off, len; } ExNfdEntry;   /* sorted by cp; the mapping is pool[off .. off+len) */
typedef struct ExCccEntry { unsigned int cp; unsigned char ccc; } ExCccEntry;         /* sorted by cp */
typedef struct ExCompEntry { unsigned int a, b, cp; } ExCompEntry;                    /* sorted by (a, b) */
extern const unsigned int ex_nfd_pool[];
extern const ExNfdEntry ex_nfd_table[];
extern const unsigned ex_nfd_count;
extern const ExCccEntry ex_ccc_table[];
extern const unsigned ex_ccc_count;
extern const ExCompEntry ex_comp_table[];
extern const unsigned ex_comp_count;
extern const unsigned int ex_fold_pool[];
extern const ExNfdEntry ex_fold_table[];
extern const unsigned ex_fold_count;
#endif
