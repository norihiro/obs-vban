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
#include <string.h>
#include <util/platform.h>
#include <util/threading.h>
#include "plugin-macros.generated.h"
#include "vban.h"
#include "vban-output-internal.h"
#include "resolve-thread.h"

#if LIBOBS_API_VER < MAKE_SEMANTIC_VERSION(30, 1, 0)
#define AUDIO_ONLY_WORKAROUND
#endif

static const char *vban_out_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("VBAN.out");
}

static bool vban_out_start(void *data)
{
	struct vban_out_s *v = data;

	if (v->context) {
		audio_t *audio = obs_output_audio(v->context);
		v->channels = audio_output_get_channels(audio);
	}
	else {
		const struct audio_output_info *aoi = audio_output_get_info(obs_get_audio());
		v->channels = get_audio_channels(aoi->speakers);
	}

	v->cont = true;
	pthread_create(&v->thread, NULL, vban_out_thread_main, v);

	blog(LOG_INFO, "vban_out_start: starting... channels=%d", (int)v->channels);

	if (v->context) {
		obs_output_begin_data_capture(v->context, OBS_OUTPUT_VIDEO | OBS_OUTPUT_AUDIO);
	}

	return true;
}

static void vban_out_stop(void *data, uint64_t ts)
{
	struct vban_out_s *v = data;
	blog(LOG_INFO, "vban_out_stop: stoping...");

	if (v->context)
		obs_output_end_data_capture(v->context);

	v->cont = false;
	os_event_signal(v->event);
	pthread_join(v->thread, NULL);

	blog(LOG_INFO, "vban_out_stop: stopped");

	UNUSED_PARAMETER(ts);
}

static void vban_out_raw_audio(void *data, struct audio_data *frames)
{
	struct vban_out_s *v = data;

	struct audio_data pkt = *frames;
	for (size_t i = 0; i < v->channels; i++)
		pkt.data[i] = bmemdup(frames->data[i], pkt.frames * sizeof(float));
	for (size_t i = v->channels; i < MAX_AV_PLANES; i++)
		pkt.data[i] = NULL;

	pthread_mutex_lock(&v->mutex);

	circlebuf_push_back(&v->buffer, &pkt, sizeof(pkt));

	os_event_signal(v->event);

	pthread_mutex_unlock(&v->mutex);
}

static bool update_string(char **opt, obs_data_t *settings, const char *name)
{
	const char *val = obs_data_get_string(settings, name);
	if (!*opt || (val && strcmp(val, *opt) != 0)) {
		bfree(*opt);
		*opt = bstrdup(val);
		return true;
	}
	return false;
}

static void vban_out_update_ip_to(struct vban_out_s *v, const char *ip_to)
{
	if (v->rt) {
		resolve_thread_release(v->rt);
		v->rt = NULL;
	}

	struct in_addr addr = {0};
	if (inet_pton(AF_INET, ip_to, &addr)) {
		v->ip_to.s_addr = addr.s_addr;
		return;
	}

	v->rt = resolve_thread_create(ip_to);
	if (!v->rt)
		return;

	blog(LOG_DEBUG, "Resolving host name '%s'", ip_to);
	resolve_thread_start(v->rt);
}

static void vban_out_update(void *data, obs_data_t *settings)
{
	struct vban_out_s *v = data;

	pthread_mutex_lock(&v->mutex);

	v->port = (int)obs_data_get_int(settings, "port");
	update_string(&v->stream_name, settings, "stream_name");
	const char *ip_to = obs_data_get_string(settings, "ip_to");
	if (ip_to && *ip_to) {
		vban_out_update_ip_to(v, ip_to);
	}

	if (v->context) {
		size_t mixer = (size_t)obs_data_get_int(settings, "mixer") - 1;
		obs_output_set_mixer(v->context, mixer);
	}

	v->frequency = (int)obs_data_get_int(settings, "frequency");
	v->format_bit = (uint8_t)obs_data_get_int(settings, "format_bit");

	pthread_mutex_unlock(&v->mutex);
}

static obs_properties_t *vban_out_get_properties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();
	obs_property_t *prop;

	obs_properties_add_int(props, "port", obs_module_text("VBAN.out.prop.port"), 1, 65535, 1);
	obs_properties_add_text(props, "stream_name", obs_module_text("VBAN.out.prop.stream_name"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, "ip_to", obs_module_text("VBAN.out.prop.ip_to"), OBS_TEXT_DEFAULT);
	obs_properties_add_int(props, "mixer", obs_module_text("VBAN.out.prop.mixer"), 1, MAX_AUDIO_MIXES, 1);
	prop = obs_properties_add_list(props, "frequency", obs_module_text("VBAN.out.prop.frequency"),
				       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(prop, obs_module_text("VBAN.out.prop.frequency.default"), 0);
	for (int i = 0; i < VBAN_SR_MAXNUMBER; i++) {
		char name[16];
		int f = VBanSRList[i];
		snprintf(name, sizeof(name) - 1, "%d Hz", f);
		obs_property_list_add_int(prop, name, f);
	}

	prop = obs_properties_add_list(props, "format_bit", obs_module_text("VBAN.out.prop.format_bit"),
				       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(prop, obs_module_text("VBAN.out.prop.format_bit.int16"), VBAN_BITFMT_16_INT);
	obs_property_list_add_int(prop, obs_module_text("VBAN.out.prop.format_bit.int24"), VBAN_BITFMT_24_INT);
	obs_property_list_add_int(prop, obs_module_text("VBAN.out.prop.format_bit.flt32"), VBAN_BITFMT_32_FLOAT);

	return props;
}

static void vban_out_get_defaults(obs_data_t *data)
{
	obs_data_set_default_int(data, "port", 6980);
	obs_data_set_default_int(data, "mixer", 1);
	obs_data_set_default_int(data, "format_bit", VBAN_BITFMT_24_INT);
}

static void *vban_out_create(obs_data_t *settings, obs_output_t *output)
{
	blog(LOG_INFO, "vban_out_create creating...");
	struct vban_out_s *v = bzalloc(sizeof(struct vban_out_s));
	v->context = output;

	pthread_mutex_init(&v->mutex, NULL);
	os_event_init(&v->event, OS_EVENT_TYPE_AUTO);

	vban_out_update(v, settings);

	return v;
}

static void vban_out_destroy(void *data)
{
	blog(LOG_INFO, "vban_out_destroy destroying...");
	struct vban_out_s *v = data;

	while (v->buffer.size) {
		struct audio_data pkt;
		circlebuf_pop_front(&v->buffer, &pkt, sizeof(pkt));
		for (size_t i = 0; i < MAX_AV_PLANES; i++)
			bfree(pkt.data[i]);
	}

	if (v->rt) {
		resolve_thread_release(v->rt);
		v->rt = NULL;
	}

	circlebuf_free(&v->buffer);
	pthread_mutex_destroy(&v->mutex);
	os_event_destroy(v->event);
	bfree(v->stream_name);
	bfree(v);
	blog(LOG_INFO, "vban_out_destroy destroyed.");
}

#ifdef AUDIO_ONLY_WORKAROUND
void vban_out_raw_video(void *data, struct video_data *frame)
{
	// Audio does not arrive until first video is sent.
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(frame);
}
#endif

struct obs_output_info vban_output_info = {
	.id = ID_PREFIX "output",
	.flags = OBS_OUTPUT_AUDIO
#ifdef AUDIO_ONLY_WORKAROUND
		 | OBS_OUTPUT_VIDEO
#endif
	,
	.get_name = vban_out_get_name,
	.create = vban_out_create,
	.destroy = vban_out_destroy,
	.start = vban_out_start,
	.stop = vban_out_stop,
	.raw_audio = vban_out_raw_audio,
#ifdef AUDIO_ONLY_WORKAROUND
	.raw_video = vban_out_raw_video,
#endif
	.update = vban_out_update,
	.get_defaults = vban_out_get_defaults,
	.get_properties = vban_out_get_properties,
};
