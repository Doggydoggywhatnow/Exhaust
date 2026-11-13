#ifndef EXHAUST_EXTRACT_INTERNAL_H
#define EXHAUST_EXTRACT_INTERNAL_H
#include "exhaust_extract.h"
/* props.c: reads the frontmatter (libyaml) into x->props, the links and tags in it into x->links and x->tags. */
void ex_extract_props(ExExtract *x, const char *text, size_t len);
int ex_prop_type_of_text(const char *s, int quoted);
#endif
