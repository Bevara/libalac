/*
 *			GPAC - Multimedia Framework C SDK
 *
 *  This file is part of GPAC / ALAC decoder filter, based on Apple's reference
 *  implementation (https://github.com/macosforge/alac, Apache-2.0).
 *
 *  A link in a chain, like the Speex filter: ALAC lives inside an MP4/M4A
 *  container, which isobmff already demuxes - it emits a GF_CODECID_ALAC pid
 *  with the 'alac' sample entry as decoder config. This filter takes that pid
 *  and outputs raw 16-bit PCM; it never sees the container.
 *
 *  The Apple decoder calls live in alac_decode.cpp - see alac_decode.h.
 */

#include <gpac/filters.h>
#include <gpac/constants.h>
#include <string.h>
#include <stdlib.h>

#include "alac_decode.h"

typedef struct
{
	GF_FilterPid *ipid, *opid;
	u32 cfg_crc;

	void *dec;
	u32 frame_length, bit_depth, nb_chan, sample_rate;
} GF_ALACDecCtx;

static void alacdec_reset(GF_ALACDecCtx *ctx)
{
	if (ctx->dec)
	{
		alac_dec_free(ctx->dec);
		ctx->dec = NULL;
	}
}

static GF_Err alacdec_configure_pid(GF_Filter *filter, GF_FilterPid *pid, Bool is_remove)
{
	const GF_PropertyValue *p;
	GF_ALACDecCtx *ctx = gf_filter_get_udta(filter);

	if (is_remove)
	{
		if (ctx->opid)
		{
			gf_filter_pid_remove(ctx->opid);
			ctx->opid = NULL;
		}
		ctx->ipid = NULL;
		return GF_OK;
	}
	if (!gf_filter_pid_check_caps(pid))
		return GF_NOT_SUPPORTED;

	/* The magic cookie is the whole point of the decoder config here: it
	 * carries the frame length, the bit depth and the Rice parameters, none of
	 * which can be recovered from the packets. */
	p = gf_filter_pid_get_property(pid, GF_PROP_PID_DECODER_CONFIG);
	if (!p || !p->value.data.ptr || (p->value.data.size < 24))
	{
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[ALACDec] Missing or truncated ALAC magic cookie\n"));
		return GF_NOT_SUPPORTED;
	}
	{
		u32 ex_crc = gf_crc_32(p->value.data.ptr, p->value.data.size);
		if (ctx->dec && (ctx->cfg_crc == ex_crc))
			return GF_OK;
		ctx->cfg_crc = ex_crc;
	}

	alacdec_reset(ctx);
	ctx->dec = alac_dec_new((const unsigned char *)p->value.data.ptr, p->value.data.size,
	                        &ctx->frame_length, &ctx->bit_depth, &ctx->nb_chan, &ctx->sample_rate);
	if (!ctx->dec)
	{
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[ALACDec] Could not initialise the decoder from the magic cookie (%u bytes)\n",
		                                    p->value.data.size));
		return GF_NON_COMPLIANT_BITSTREAM;
	}
	if ((ctx->bit_depth != 16) && (ctx->bit_depth != 20) && (ctx->bit_depth != 24) && (ctx->bit_depth != 32))
	{
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[ALACDec] Unsupported bit depth %u\n", ctx->bit_depth));
		alacdec_reset(ctx);
		return GF_NOT_SUPPORTED;
	}

	ctx->ipid = pid;
	gf_filter_pid_set_framing_mode(pid, GF_TRUE);

	if (!ctx->opid)
		ctx->opid = gf_filter_pid_new(filter);

	gf_filter_pid_copy_properties(ctx->opid, pid);
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CODECID, &PROP_UINT(GF_CODECID_RAW));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_AUDIO_FORMAT, &PROP_UINT(GF_AUDIO_FMT_S16));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_SAMPLE_RATE, &PROP_UINT(ctx->sample_rate));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_NUM_CHANNELS, &PROP_UINT(ctx->nb_chan));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CHANNEL_LAYOUT,
	                           &PROP_LONGUINT((ctx->nb_chan == 2)
	                                              ? (GF_AUDIO_CH_FRONT_LEFT | GF_AUDIO_CH_FRONT_RIGHT)
	                                              : GF_AUDIO_CH_FRONT_CENTER));

	return GF_OK;
}

static GF_Err alacdec_process(GF_Filter *filter)
{
	GF_FilterPacket *pck, *dst_pck;
	u8 *data, *output;
	u32 size;
	unsigned int decoded = 0;
	GF_ALACDecCtx *ctx = gf_filter_get_udta(filter);

	if (!ctx->dec)
		return GF_OK;

	pck = gf_filter_pid_get_packet(ctx->ipid);
	if (!pck)
	{
		if (gf_filter_pid_is_eos(ctx->ipid))
		{
			gf_filter_pid_set_eos(ctx->opid);
			return GF_EOS;
		}
		return GF_OK;
	}
	data = (u8 *)gf_filter_pck_get_data(pck, &size);
	if (!data || !size)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_OK;
	}

	/* One packet holds at most frameLength samples per channel; the last one
	 * of a stream usually holds fewer, which is why the packet is allocated
	 * for the maximum and truncated afterwards. */
	dst_pck = gf_filter_pck_new_alloc(ctx->opid, ctx->frame_length * ctx->nb_chan * 2, &output);
	if (!dst_pck)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_OUT_OF_MEM;
	}

	if (alac_dec_decode(ctx->dec, data, size, (short *)output, ctx->frame_length, &decoded) != ALAC_DEC_OK)
	{
		gf_filter_pck_discard(dst_pck);
		gf_filter_pid_drop_packet(ctx->ipid);
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[ALACDec] Failed to decode a packet\n"));
		return GF_NON_COMPLIANT_BITSTREAM;
	}
	if (!decoded)
	{
		gf_filter_pck_discard(dst_pck);
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_OK;
	}
	if (decoded < ctx->frame_length)
		gf_filter_pck_truncate(dst_pck, decoded * ctx->nb_chan * 2);

	gf_filter_pck_merge_properties(pck, dst_pck);
	gf_filter_pck_set_cts(dst_pck, gf_filter_pck_get_cts(pck));
	gf_filter_pck_set_duration(dst_pck, decoded);
	gf_filter_pck_set_sap(dst_pck, GF_FILTER_SAP_1);
	gf_filter_pck_send(dst_pck);

	gf_filter_pid_drop_packet(ctx->ipid);
	return GF_OK;
}

static void alacdec_finalize(GF_Filter *filter)
{
	GF_ALACDecCtx *ctx = gf_filter_get_udta(filter);
	alacdec_reset(ctx);
}

static const GF_FilterCapability ALACDecCaps[] =
	{
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_AUDIO),
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_CODECID, GF_CODECID_ALAC),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_AUDIO),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_CODECID, GF_CODECID_RAW),
};

GF_FilterRegister ALACDecoderRegister = {
	.name = "alacdec",
	GF_FS_SET_DESCRIPTION("Apple Lossless (ALAC) decoder")
		GF_FS_SET_HELP("This filter decodes Apple Lossless audio using Apple's reference implementation. It takes the GF_CODECID_ALAC pid that isobmff produces from an MP4/M4A file and outputs raw 16-bit PCM.")
			.private_size = sizeof(GF_ALACDecCtx),
	SETCAPS(ALACDecCaps),
	.configure_pid = alacdec_configure_pid,
	.process = alacdec_process,
	.finalize = alacdec_finalize,
};

const GF_FilterRegister *EMSCRIPTEN_KEEPALIVE alacdec_register(GF_FilterSession *session)
{
	return &ALACDecoderRegister;
}

#include "filter_register.h"
__attribute__((constructor))
void register_alacdec(void) {
    gf_filter_auto_register("alacdec", alacdec_register);
}
