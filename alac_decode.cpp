/*
 *  ALAC-facing half of the Apple Lossless filter - see alac_decode.h for why
 *  it is a translation unit of its own.
 */

#include <stdlib.h>
#include <string.h>

#include <ALACDecoder.h>
#include <ALACBitUtilities.h>

#include "alac_decode.h"

struct AlacDecHandle
{
	ALACDecoder *dec;
	unsigned int frame_length;
	unsigned int bit_depth;
	unsigned int channels;
	unsigned int sample_rate;
	unsigned char *scratch; /* holds one frame at the stream's native depth */
	size_t scratch_size;
};

extern "C" void alac_dec_free(void *handle)
{
	AlacDecHandle *h = (AlacDecHandle *)handle;
	if (!h)
		return;
	delete h->dec;
	free(h->scratch);
	free(h);
}

extern "C" void *alac_dec_new(const unsigned char *cookie, unsigned int cookie_size,
                              unsigned int *frame_length, unsigned int *bit_depth,
                              unsigned int *channels, unsigned int *sample_rate)
{
	if (!cookie || (cookie_size < 24))
		return NULL;

	/* ALACDecoder::Init already skips a leading 'frma' or 'alac' atom header,
	 * which covers the 36-byte decoder config GPAC hands over (box header,
	 * version/flags, then the 24-byte ALACSpecificConfig). What it does not
	 * expect is the version/flags on their own, so that shape is trimmed here:
	 * a bare config starts with frameLength, which is never zero. */
	const unsigned char *ck = cookie;
	unsigned int ck_size = cookie_size;
	if ((ck_size >= 28) && !ck[0] && !ck[1] && !ck[2] && !ck[3] &&
	    !(ck[4] == 'a' && ck[5] == 'l' && ck[6] == 'a' && ck[7] == 'c') &&
	    !(ck[4] == 'f' && ck[5] == 'r' && ck[6] == 'm' && ck[7] == 'a'))
	{
		ck += 4;
		ck_size -= 4;
	}

	AlacDecHandle *h = (AlacDecHandle *)calloc(1, sizeof(AlacDecHandle));
	if (!h)
		return NULL;
	h->dec = new ALACDecoder();
	if (!h->dec)
	{
		free(h);
		return NULL;
	}
	if (h->dec->Init((void *)ck, ck_size) != 0)
	{
		alac_dec_free(h);
		return NULL;
	}

	h->frame_length = h->dec->mConfig.frameLength;
	h->bit_depth = h->dec->mConfig.bitDepth;
	h->channels = h->dec->mConfig.numChannels;
	h->sample_rate = h->dec->mConfig.sampleRate;
	if (!h->frame_length || !h->channels || !h->sample_rate)
	{
		alac_dec_free(h);
		return NULL;
	}

	/* Decode() writes the stream's own depth: 2 bytes per sample at 16 bits,
	 * 3 at 20/24, 4 at 32. The scratch buffer is sized for the widest. */
	h->scratch_size = (size_t)h->frame_length * h->channels * 4;
	h->scratch = (unsigned char *)malloc(h->scratch_size);
	if (!h->scratch)
	{
		alac_dec_free(h);
		return NULL;
	}

	if (frame_length) *frame_length = h->frame_length;
	if (bit_depth) *bit_depth = h->bit_depth;
	if (channels) *channels = h->channels;
	if (sample_rate) *sample_rate = h->sample_rate;
	return h;
}

extern "C" int alac_dec_decode(void *handle, const unsigned char *data, unsigned int size,
                               short *out, unsigned int max_samples, unsigned int *out_samples)
{
	AlacDecHandle *h = (AlacDecHandle *)handle;
	BitBuffer bits;
	uint32_t decoded = 0;

	if (!h || !data || !out)
		return ALAC_DEC_ERR_DECODE;

	BitBufferInit(&bits, (uint8_t *)data, size);
	if (h->dec->Decode(&bits, h->scratch, h->frame_length, h->channels, &decoded) != 0)
		return ALAC_DEC_ERR_DECODE;

	if (decoded > max_samples)
		decoded = max_samples;

	const size_t n = (size_t)decoded * h->channels;
	if (h->bit_depth == 16)
	{
		memcpy(out, h->scratch, n * 2);
	}
	else if (h->bit_depth == 24 || h->bit_depth == 20)
	{
		/* three bytes per sample, little-endian, signed: keep the top 16 bits.
		 * The pid is S16, so the low bits are dropped rather than dithered -
		 * a deliberate, lossy step, and the only one in this filter. */
		const unsigned char *p = h->scratch;
		for (size_t i = 0; i < n; i++)
			out[i] = (short)(p[i * 3 + 1] | (p[i * 3 + 2] << 8));
	}
	else if (h->bit_depth == 32)
	{
		const int32_t *p = (const int32_t *)h->scratch;
		for (size_t i = 0; i < n; i++)
			out[i] = (short)(p[i] >> 16);
	}
	else
	{
		return ALAC_DEC_ERR_DECODE;
	}

	if (out_samples)
		*out_samples = decoded;
	return ALAC_DEC_OK;
}
