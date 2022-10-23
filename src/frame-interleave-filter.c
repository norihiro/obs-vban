/*
 * OBS Frame Interleave Filter Plugin
 * Copyright (C) 2022 Norihiro Kamae <norihiro@nagater.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <obs-module.h>
#include "plugin-macros.generated.h"

#ifndef MSEC_TO_NSEC
#define MSEC_TO_NSEC 1000000ULL
#endif

struct frame_interleave_s
{
	obs_source_t *context;
	uint64_t interleave_ns;
	uint64_t next_ns;
};

static const char *get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("FrameInterleaveFilter");
}

static void update(void *data, obs_data_t *settings)
{
	struct frame_interleave_s *f = data;

	f->interleave_ns = obs_data_get_int(settings, "interleave_ms") * MSEC_TO_NSEC;
}

static struct obs_source_frame *filter_video(void *data, struct obs_source_frame *frame)
{
	struct frame_interleave_s *f = data;

	uint64_t ns = obs_get_video_frame_time();
	obs_source_t *parent = obs_filter_get_parent(f->context);

	if (obs_source_active(parent) || !f->next_ns || ns >= f->next_ns) {
		if (!f->next_ns || ns - f->next_ns > f->interleave_ns * 2)
			f->next_ns = ns + f->interleave_ns;
		else
			f->next_ns += f->interleave_ns;
		return frame;
	}

	obs_source_release_frame(parent, frame);
	return NULL;
}

static void *create(obs_data_t *settings, obs_source_t *source)
{
	struct frame_interleave_s *f = bzalloc(sizeof(struct frame_interleave_s));
	f->context = source;

	update(f, settings);

	return f;
}

static void destroy(void *data)
{
	bfree(data);
}

static obs_properties_t *get_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_property_t *p;
	obs_properties_t *props = obs_properties_create();

	p = obs_properties_add_int(props, "interleave_ms", obs_module_text("Prop.Interleave"), 0, 1000, 1);
	obs_property_int_set_suffix(p, " ms");

	return props;
}

static void get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "interleave_ms", 34);
}

const struct obs_source_info frame_interleave_filter_info = {
	.id = ID_PREFIX "filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_ASYNC,
	.get_name = get_name,
	.create = create,
	.destroy = destroy,
	.update = update,
	.get_properties = get_properties,
	.get_defaults = get_defaults,
	.filter_video = filter_video,
};
