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
#include <util/platform.h>
#include "plugin-macros.generated.h"
#include "vban-udp.h"
#include "vban.h"

struct vban_src_s
{
	obs_source_t *context;

	// properties
	int port;
	char *stream_name;
	char *ip_from;

	vban_udp_t *vban;

	DARRAY(float) buffer;
};

static const char *vban_src_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("VBAN.src");
}

static void vban_src_callback(const char *buf, size_t buf_len, const struct sockaddr_in *addr, void *data);

static void update_port(struct vban_src_s *s, int port)
{
	vban_udp_t *old_vban = s->vban;

	s->vban = vban_udp_find_or_create(port);

	s->port = port;

	if (old_vban) {
		vban_udp_remove_callback(old_vban, vban_src_callback, s);
		vban_udp_release(old_vban);
	}

	vban_udp_add_callback(s->vban, vban_src_callback, s);
}

bool update_string(char **opt, obs_data_t *settings, const char *name)
{
	const char *val = obs_data_get_string(settings, name);
	if (!*opt || (val && strcmp(val, *opt) != 0)) {
		bfree(*opt);
		*opt = bstrdup(val);
		return true;
	}
	return false;
}

static void vban_src_update(void *data, obs_data_t *settings)
{
	struct vban_src_s *s = data;

	int port = (int)obs_data_get_int(settings, "port");
	if (port != s->port)
		update_port(s, port);

	update_string(&s->stream_name, settings, "stream_name");
	update_string(&s->ip_from, settings, "ip_from");
}

static obs_properties_t *vban_src_get_properties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();

	obs_properties_add_int(props, "port", obs_module_text("VBAN.src.prop.port"), 1, 65535, 1);
	obs_properties_add_text(props, "stream_name", obs_module_text("VBAN.src.prop.stream_name"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, "ip_from", obs_module_text("VBAN.src.prop.ip_from"), OBS_TEXT_DEFAULT);

	return props;
}

static void vban_src_get_defaults(obs_data_t *data)
{
	obs_data_set_default_int(data, "port", 6980);
}

static void *vban_src_create(obs_data_t *settings, obs_source_t *source)
{
	struct vban_src_s *s = bzalloc(sizeof(struct vban_src_s));
	s->context = source;

	vban_src_update(s, settings);

	return s;
}

static void vban_src_destroy(void *data)
{
	struct vban_src_s *s = data;

	if (s->vban) {
		vban_udp_remove_callback(s->vban, vban_src_callback, s);
		vban_udp_release(s->vban);
	}

	bfree(s->stream_name);
	bfree(s->ip_from);
	da_free(s->buffer);
	bfree(s);
}

const struct obs_source_info vban_source_info = {
	.id = ID_PREFIX "source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE,
	.get_name = vban_src_get_name,
	.create = vban_src_create,
	.destroy = vban_src_destroy,
	.update = vban_src_update,
	.get_properties = vban_src_get_properties,
	.get_defaults = vban_src_get_defaults,
	.icon_type = OBS_ICON_TYPE_AUDIO_INPUT,
};

static void convert_24le_to_fltp(struct vban_src_s *s, struct obs_source_audio *audio, const char *buf)
{
	da_resize(s->buffer, audio->speakers * audio->frames);
	float *dst = s->buffer.array;
	for (int ch = 0; ch < (int)audio->speakers; ch++) {
		audio->data[ch] = (void *)dst;
		const char *src = buf + ch * 3;
		for (uint32_t i = 0; i < audio->frames; i++) {
			int x = (src[0] & 0xFF) | ((src[1] & 0xFF) << 8) | ((src[2] & 0xFF) << 16) |
				((src[2] & 0x80) ? 0xFF000000 : 0);
			*dst++ = x * (1.0f / 8388608.0f);
			src += 3 * audio->speakers;
		}
	}
}

static void vban_src_callback(const char *buf, size_t buf_len, const struct sockaddr_in *addr, void *data)
{
	struct vban_src_s *s = data;

	const struct VBanHeader *header = (const struct VBanHeader *)buf;
	const char *payload = buf + VBAN_HEADER_SIZE;
	size_t payload_len = buf_len - VBAN_HEADER_SIZE;

	if (header->format_nbc + 1 > 8) {
		blog(LOG_ERROR, "Too many number of channels: %d", header->format_nbc + 1);
		return;
	}

	struct obs_source_audio audio = {
		.frames = header->format_nbs + 1,
		.speakers = header->format_nbc + 1,
		.samples_per_sec = VBanSRList[header->format_SR & VBAN_SR_MASK],
	};

	size_t len_exp =
		VBanBitResolutionSize[header->format_bit & VBAN_BIT_RESOLUTION_MASK] * audio.frames * audio.speakers;
	if (payload_len < len_exp) {
		blog(LOG_ERROR, "Too small payload size %d, expected %d", (int)payload_len,
		     VBanBitResolutionSize[header->format_bit & VBAN_BIT_RESOLUTION_MASK] * audio.frames *
			     (int)audio.speakers);
		return;
	}

	switch (header->format_bit) {
	case VBAN_BITFMT_8_INT:
		audio.format = AUDIO_FORMAT_U8BIT;
		audio.data[0] = (const uint8_t *)payload;
		break;
	case VBAN_BITFMT_16_INT:
		audio.format = AUDIO_FORMAT_16BIT;
		audio.data[0] = (const uint8_t *)payload;
		break;
	case VBAN_BITFMT_24_INT:
		audio.format = AUDIO_FORMAT_FLOAT_PLANAR;
		convert_24le_to_fltp(s, &audio, payload);
		break;
	case VBAN_BITFMT_32_INT:
		audio.format = AUDIO_FORMAT_32BIT;
		audio.data[0] = (const uint8_t *)payload;
		break;
	case VBAN_BITFMT_32_FLOAT:
		audio.format = AUDIO_FORMAT_FLOAT;
		audio.data[0] = (const uint8_t *)payload;
		break;
	default:
		blog(LOG_ERROR, "Unsupported format %d", header->format_bit);
		return;
	}

	audio.timestamp = os_gettime_ns() - (uint64_t)audio.frames * 1000000000 / audio.samples_per_sec;

	obs_source_output_audio(s->context, &audio);

	(void)addr; // TODO: lock to compare stream_name and ip_from or send stream_name and ip_from at `update` callback.
}
