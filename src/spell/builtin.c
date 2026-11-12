#include "exhaust_spell.h"

extern const unsigned char ex_dict_en_us_aff[], ex_dict_en_us_dic[];
extern const unsigned long ex_dict_en_us_aff_len, ex_dict_en_us_dic_len;

ExSpell *ex_spell_create_en_us(void) {
    return ex_spell_create((const char *)ex_dict_en_us_aff, ex_dict_en_us_aff_len, (const char *)ex_dict_en_us_dic, ex_dict_en_us_dic_len, 0, 0);
}
