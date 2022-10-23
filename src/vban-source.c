/*
 * OBS VBAN Plugin
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
#include "plugin-macros.generated.h"
#include "vban-udp.h"

struct vban_src_s
{
	obs_source_t *context;

	// properties
	int port;
	char *stream_name;
	char *ip_from;

	vban_udp_t *vban;
};

static const char *vban_src_get_name(void *)
{
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

	int port = obs_data_get_int(settings, "port");
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

static void vban_src_callback(const char *buf, size_t buf_len, const struct sockaddr_in *addr, void *data)
{
	struct vban_src_s *s = data;

	(void)s, (void)buf, (void)buf_len, (void)addr; // TODO: implement
	// TODO: lock
	// TODO: compare stream_name and ip_from?
}
