/* AVIF (6c-i): the ISO base media file reader and the dav1d call, in avif.c. */
#ifndef EXHAUST_IMAGE_INTERNAL_H
#define EXHAUST_IMAGE_INTERNAL_H
#include <stddef.h>
#include <stdint.h>
int ex_avif_info(const void *data, size_t len, int *w, int *h, char *err, size_t errcap);
/* Decodes the still picture into malloc'ed RGBA (straight alpha). 0, or -1 with the reason in err. */
int ex_avif_decode(const void *data, size_t len, uint8_t **rgba, int *w, int *h, char *err, size_t errcap);
#endif
