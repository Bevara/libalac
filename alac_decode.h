/*
 *  Minimal C surface over Apple's ALAC decoder.
 *
 *  ALACDecoder is a C++ class and its headers bring in their own integer
 *  typedefs, so everything that touches it lives in alac_decode.cpp and the
 *  filter itself only sees this header.
 */

#ifndef _ALAC_DECODE_H_
#define _ALAC_DECODE_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Creates a decoder from the magic cookie carried by the 'alac' sample entry.
 * Returns NULL on failure; on success fills in the stream parameters. */
void *alac_dec_new(const unsigned char *cookie, unsigned int cookie_size,
                   unsigned int *frame_length, unsigned int *bit_depth,
                   unsigned int *channels, unsigned int *sample_rate);

/* Decodes one packet into interleaved 16-bit samples. *out_samples receives
 * the number of samples *per channel*. Returns 0 on success. */
int alac_dec_decode(void *handle, const unsigned char *data, unsigned int size,
                    short *out, unsigned int max_samples, unsigned int *out_samples);

void alac_dec_free(void *handle);

#define ALAC_DEC_OK             0
#define ALAC_DEC_ERR_DECODE    -1
#define ALAC_DEC_ERR_MEMORY    -2

#ifdef __cplusplus
}
#endif

#endif
