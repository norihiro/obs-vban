/*
 * OBS VBAN Audio Plugin
 * Copyright (C) 2022 Norihiro Kamae <norihiro@nagater.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <obs-module.h>
#include <media-io/audio-resampler.h>
#include "plugin-macros.generated.h"
#include "vban.h"
#include "socket.h"
#include "vban-output-internal.h"
#include "vban-pacing.h"
#include "resolve-thread.h"
#include <util/platform.h>

struct output_thread_s
{
	struct VBanHeader *header;
	char *payload;

	struct vban_out_s *v;

	int frequency_vban;
	int frequency_src;
	audio_resampler_t *resampler;

	struct darray buffer;
	uint64_t buf_ts_ns;

	socket_t vban_socket;
};

static enum audio_format closest_format(uint8_t format_bit)
{
	switch (format_bit) {
	case VBAN_BITFMT_16_INT:
		return AUDIO_FORMAT_16BIT;
	case VBAN_BITFMT_24_INT:
		return AUDIO_FORMAT_32BIT;
	case VBAN_BITFMT_32_INT:
		return AUDIO_FORMAT_32BIT;
	case VBAN_BITFMT_32_FLOAT:
		return AUDIO_FORMAT_FLOAT;
	default:
		blog(LOG_ERROR, "Unimplemented format %d is requested", (int)format_bit);
		return AUDIO_FORMAT_UNKNOWN;
	}
}

static bool thread_loop_start(struct output_thread_s *t)
{
	struct vban_out_s *v = t->v;
	struct VBanHeader *header = t->header;

	pthread_mutex_lock(&v->mutex);
	const obs_output_t *output = t->v->context;

	const struct audio_output_info *aoi;
	if (output)
		aoi = audio_output_get_info(obs_output_audio(output));
	else
		aoi = audio_output_get_info(obs_get_audio());

	t->frequency_src = aoi->samples_per_sec;

	if (v->frequency)
		t->frequency_vban = v->frequency;
	else
		t->frequency_vban = t->frequency_src;

	header->format_bit = v->format_bit;
	header->format_nbc = (uint8_t)(v->channels - 1);

	pthread_mutex_unlock(&v->mutex);

	memcpy(&header->vban, "VBAN", 4);

	bool sr_found = false;
	for (uint8_t sr = 0; sr < VBAN_SR_MAXNUMBER; sr++) {
		if (VBanSRList[sr] == t->frequency_vban) {
			header->format_SR = sr;
			sr_found = true;
			break;
		}
	}
	if (!sr_found) {
		blog(LOG_ERROR, "VBAN cannot handle sampling frequency %d Hz", t->frequency_vban);
		return false;
	}

	blog(LOG_INFO, "vban-out starting format_bit=%d channels=%d frequency=%u", (int)v->format_bit, (int)v->channels,
	     t->frequency_vban);

	if (t->frequency_vban != t->frequency_src) {
		const struct resample_info src = {
			.samples_per_sec = aoi->samples_per_sec,
			.format = AUDIO_FORMAT_FLOAT_PLANAR,
			.speakers = aoi->speakers,
		};

		const struct resample_info dst = {
			.samples_per_sec = t->frequency_vban,
			.format = closest_format(v->format_bit),
			.speakers = aoi->speakers,
		};

		blog(LOG_INFO, "configuring resampler frequency %u -> %u", aoi->samples_per_sec, t->frequency_vban);

		t->resampler = audio_resampler_create(&dst, &src);
	}

	header->nuFrame = 0;

	t->vban_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	return true;
}

static bool bring_settings_unlocked(struct vban_out_s *v, struct output_thread_s *t, struct sockaddr_in *addr)
{
	bool restart = false;

	if (v->rt && resolve_thread_done(v->rt)) {
		resolve_thread_get_addr(v->rt, &v->ip_to);
		resolve_thread_release(v->rt);
		v->rt = NULL;
	}

	addr->sin_port = htons(v->port);
	addr->sin_addr.s_addr = v->ip_to.s_addr;

	if (v->frequency && v->frequency != t->frequency_vban) {
		blog(LOG_INFO, "restarting to change frequency from %d to %d", (int)t->frequency_vban,
		     (int)v->frequency);
		restart = true;
	}

	if (v->format_bit != t->header->format_bit) {
		blog(LOG_INFO, "restarting to change format_bit from %d to %d", t->header->format_bit, v->format_bit);
		restart = true;
	}

	strncpy(t->header->streamname, v->stream_name, VBAN_STREAM_NAME_SIZE);

	return restart;
}

static void resample_from_packet(struct output_thread_s *t, const struct audio_data *pkt)
{
	uint8_t *data[MAX_AV_PLANES] = {0};
	uint32_t out_samples = 0;
	uint64_t ts_offset = 0;
	if (!audio_resampler_resample(t->resampler, data, &out_samples, &ts_offset, (const uint8_t *const *)pkt->data,
				      pkt->frames)) {
		blog(LOG_ERROR, "Failed to resample");
		return;
	}

	size_t channels = t->v->channels;
	size_t fmt_size = VBanBitResolutionSize[t->header->format_bit & VBAN_BIT_RESOLUTION_MASK];
	size_t sample_size = channels * fmt_size;

	size_t offset = t->buffer.num;
	darray_resize(1, &t->buffer, offset + out_samples * sample_size);
	char *dst = (char *)t->buffer.array + offset;
	char *src = (char *)data[0];

	switch (t->header->format_bit) {
	case VBAN_BITFMT_24_INT:
		for (uint32_t i = 0; i < channels * out_samples; i++) {
			src++;
			*dst++ = *src++;
			*dst++ = *src++;
			*dst++ = *src++;
		}
		break;
	case VBAN_BITFMT_16_INT:
	case VBAN_BITFMT_32_INT:
	case VBAN_BITFMT_32_FLOAT:
		memcpy(dst, src, out_samples * sample_size);
		break;
	default:
		blog(LOG_ERROR, "Unimplemented format %d", (int)t->header->format_bit);
	}
}

static void convert_from_packet(struct output_thread_s *t, struct audio_data *pkt)
{
	size_t channels = t->v->channels;
	size_t fmt_size = VBanBitResolutionSize[t->header->format_bit & VBAN_BIT_RESOLUTION_MASK];
	size_t sample_size = channels * fmt_size;

	size_t offset = t->buffer.num;
	darray_resize(1, &t->buffer, offset + pkt->frames * sample_size);

	char *dst = (char *)t->buffer.array + offset;

	switch (t->header->format_bit) {
	case VBAN_BITFMT_16_INT:
		for (uint32_t i = 0; i < pkt->frames; i++) {
			for (size_t ch = 0; ch < channels; ch++) {
				int16_t v = (int16_t)(((float *)pkt->data[ch])[i] * (1 << 15));
				*dst++ = v & 0xFF;
				*dst++ = v >> 8;
			}
		}
		break;
	case VBAN_BITFMT_24_INT:
		for (uint32_t i = 0; i < pkt->frames; i++) {
			for (size_t ch = 0; ch < channels; ch++) {
				int32_t v = (int32_t)(((float *)pkt->data[ch])[i] * (1 << 23));
				*dst++ = v & 0xFF;
				*dst++ = (v >> 8) & 0xFF;
				*dst++ = v >> 16;
			}
		}
		break;
	case VBAN_BITFMT_32_FLOAT:
		for (uint32_t i = 0; i < pkt->frames; i++) {
			for (size_t ch = 0; ch < channels; ch++) {
				float v = ((float *)pkt->data[ch])[i];
				((float *)dst)[0] = v;
				dst += 4;
			}
		}
		break;
	default:
		blog(LOG_ERROR, "Cannot convert for format_bit=%d", (int)t->header->format_bit);
	}
}

static void vban_out_loop(struct vban_out_s *v)
{
	struct audio_data pkt = {0};

	char vban_buf[VBAN_PROTOCOL_MAX_SIZE];

	struct output_thread_s t = {
		.header = (void *)vban_buf,
		.payload = vban_buf + VBAN_HEADER_SIZE,
		.v = v,
	};

	if (!thread_loop_start(&t)) {
		blog(LOG_INFO, "Cannot start VBAN output");
		v->cont = false;
		return;
	}

	const size_t channels = v->channels;
	const size_t fmt_size = VBanBitResolutionSize[t.header->format_bit & VBAN_BIT_RESOLUTION_MASK];
	const size_t sample_size = channels * fmt_size;
	size_t nbs = VBAN_DATA_MAX_SIZE / sample_size;
	if (nbs > 256)
		nbs = 256;
	const size_t packet_size = nbs * sample_size;
	const size_t max_frames = (size_t)t.frequency_vban * VBAN_OUT_MAX_LATENCY_MS / 1000;
	const size_t target_frames = (size_t)t.frequency_vban * VBAN_OUT_TARGET_LATENCY_MS / 1000;

	struct vban_pacing pacing;
	vban_pacing_init(&pacing, nbs, (uint64_t)t.frequency_vban, VBAN_OUT_MAX_LAG_PACKETS, max_frames, target_frames);

	uint64_t dropped_frames = 0;
	uint64_t dropped_logged = 0;
	uint64_t last_drop_log_ns = 0;
	uint64_t last_debug_log_ns = 0;
	unsigned long wait_ms = 100;

	while (v->cont) {
		// copy of properties
		struct sockaddr_in addr;
		addr.sin_family = AF_INET;

		os_event_timedwait(v->event, wait_ms);

		pthread_mutex_lock(&v->mutex);
		bool restart = bring_settings_unlocked(v, &t, &addr);
		pthread_mutex_unlock(&v->mutex);

		if (restart)
			break;

		/* Convert every queued chunk, so that the sender is never limited to
		 * one chunk per wake-up. */
		for (;;) {
			pthread_mutex_lock(&v->mutex);
			bool have_pkt = v->buffer.size >= sizeof(pkt);
			if (have_pkt) {
				deque_pop_front(&v->buffer, &pkt, sizeof(pkt));
				v->queued_frames -= pkt.frames;
			}
			pthread_mutex_unlock(&v->mutex);
			if (!have_pkt)
				break;

			t.buf_ts_ns =
				pkt.timestamp - (uint64_t)(t.buffer.num / sample_size) * 1000000000 / t.frequency_vban;
			if (t.resampler)
				resample_from_packet(&t, &pkt);
			else
				convert_from_packet(&t, &pkt);
			for (size_t i = 0; i < MAX_AV_PLANES; i++) {
				bfree(pkt.data[i]);
				pkt.data[i] = NULL;
			}
			pkt.frames = 0;
		}

		/* Drop audio beyond the latency cap or left by a stall, then send
		 * every packet whose deadline has passed. */
		uint64_t now = os_gettime_ns();
		struct vban_pacing_step step = vban_pacing_step(&pacing, now, t.buffer.num / sample_size, 100);
		if (step.drop_frames) {
			size_t drop = step.drop_frames * sample_size;
			memmove(t.buffer.array, (char *)t.buffer.array + drop, t.buffer.num - drop);
			t.buffer.num -= drop;
			dropped_frames += step.drop_frames;
		}
		if ((step.lag_reset || step.drop_frames) && now - last_debug_log_ns > 1000000000ULL) {
			blog(LOG_DEBUG, "pacing: lag_reset=%d dropped_frames=%zu buffered_frames=%zu",
			     (int)step.lag_reset, step.drop_frames, t.buffer.num / sample_size);
			last_debug_log_ns = now;
		}
		for (size_t i = 0; i < step.send_packets; i++) {
			t.header->format_nbs = (uint8_t)(nbs - 1);
			memcpy(t.payload, t.buffer.array, packet_size);
			memmove(t.buffer.array, (char *)t.buffer.array + packet_size, t.buffer.num - packet_size);
			t.buffer.num -= packet_size;
			sendto(t.vban_socket, vban_buf, VBAN_HEADER_SIZE + packet_size, 0, (struct sockaddr *)&addr,
			       (socklen_t)sizeof(addr));

#ifdef DEBUG_PACKET
			blog(LOG_DEBUG, "sent packet nuFrame: %d", t.header->nuFrame);
#endif

			t.header->nuFrame++;
		}

		wait_ms = step.wait_ms;

		/* Report drops at most every 10 s. */
		pthread_mutex_lock(&v->mutex);
		uint64_t total_dropped = dropped_frames + v->dropped_frames;
		pthread_mutex_unlock(&v->mutex);
		if (total_dropped != dropped_logged && now - last_drop_log_ns > 10000000000ULL) {
			blog(LOG_INFO, "latency cap (%d ms): dropped %" PRIu64 " frames so far",
			     VBAN_OUT_MAX_LATENCY_MS, total_dropped);
			dropped_logged = total_dropped;
			last_drop_log_ns = now;
		}
	}

	blog(LOG_INFO, "Total number of output packets: %" PRIu32, t.header->nuFrame);

	if (t.resampler)
		audio_resampler_destroy(t.resampler);
	for (size_t i = 0; i < MAX_AV_PLANES; i++)
		bfree(pkt.data[i]);
	closesocket(t.vban_socket);
	darray_free(&t.buffer);
}

void *vban_out_thread_main(void *data)
{
	os_set_thread_name("vban-out");
	struct vban_out_s *v = data;

	while (v->cont) {
		vban_out_loop(v);
	}

	return NULL;
}
