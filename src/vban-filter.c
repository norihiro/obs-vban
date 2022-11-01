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
#include "vban-output-internal.h"
#include "vban.h"

struct vban_flt_s
{
	obs_source_t *context;
	void *output;
};

extern struct obs_output_info vban_output_info;

static const char *vban_flt_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("VBAN.flt");
}

static void vban_flt_update(void *data, obs_data_t *settings)
{
	struct vban_flt_s *s = data;
	vban_output_info.update(s->output, settings);
}

static obs_properties_t *vban_flt_get_properties(void *data)
{
	struct vban_flt_s *s = data;
	obs_properties_t *props = vban_output_info.get_properties(s ? s->output : NULL);

	obs_properties_remove_by_name(props, "mixer");

	return props;
}

static void vban_flt_get_defaults(obs_data_t *data)
{
	vban_output_info.get_defaults(data);
}

static void *vban_flt_create(obs_data_t *settings, obs_source_t *source)
{
	struct vban_flt_s *s = bzalloc(sizeof(struct vban_flt_s));
	s->context = source;
	s->output = vban_output_info.create(settings, NULL);

	vban_output_info.start(s->output);

	return s;
}

static void vban_flt_destroy(void *data)
{
	struct vban_flt_s *s = data;
	vban_output_info.stop(s->output, 0);
	vban_output_info.destroy(s->output);
	bfree(s);
}

static struct obs_audio_data *vban_flt_audio(void *data, struct obs_audio_data *audio)
{
	struct vban_flt_s *s = data;
	struct vban_out_s *o = s->output;

	struct audio_data frames = {
		.frames = audio->frames,
		.timestamp = audio->timestamp,
	};
	for (size_t i = 0; i < o->channels; i++)
		frames.data[i] = audio->data[i];

	vban_output_info.raw_audio(s->output, &frames);

	return audio;
}

const struct obs_source_info vban_filter_info = {
	.id = ID_PREFIX "filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = vban_flt_get_name,
	.create = vban_flt_create,
	.destroy = vban_flt_destroy,
	.update = vban_flt_update,
	.get_properties = vban_flt_get_properties,
	.get_defaults = vban_flt_get_defaults,
	.filter_audio = vban_flt_audio,
};
