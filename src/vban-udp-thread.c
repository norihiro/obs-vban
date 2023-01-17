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
#include <stdio.h>
#include <util/threading.h>
#include "plugin-macros.generated.h"
#include "vban-udp-internal.h"
#include "socket.h"
#include <vban.h>

static bool init_socket(vban_udp_t *dev)
{
	int ret;

	char thread_name[16];
	snprintf(thread_name, sizeof(thread_name), "vban-r-%d", dev->port);
	thread_name[sizeof(thread_name) - 1] = 0;
	os_set_thread_name(thread_name);

	dev->vban_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (!valid_socket(dev->vban_socket)) {
		blog(LOG_ERROR, "Failed to create socket");
		return false;
	}

	int opt = 1;
	setsockopt(dev->vban_socket, SOL_SOCKET, SO_REUSEADDR, (void *)&opt, sizeof(int));

	struct sockaddr_in si_me;
	memset(&si_me, 0, sizeof(si_me));
	si_me.sin_family = AF_INET;
	si_me.sin_port = htons(dev->port);
	si_me.sin_addr.s_addr = htonl(INADDR_ANY);

	ret = bind(dev->vban_socket, (struct sockaddr const *)&si_me, sizeof(si_me));
	if (ret < 0) {
		blog(LOG_ERROR, "Failed to bind port %d", (int)dev->port);
		closesocket(dev->vban_socket);
		dev->vban_socket = INVALID_SOCKET;
		return false;
	}

	return true;
}

static void finalize_socket(vban_udp_t *dev)
{
	if (dev->vban_socket != INVALID_SOCKET) {
		closesocket(dev->vban_socket);
		dev->vban_socket = INVALID_SOCKET;
	}
}

static bool select_socket(vban_udp_t *dev)
{
	fd_set fd_read;
	FD_ZERO(&fd_read);
	FD_SET(dev->vban_socket, &fd_read);

	struct timeval tv = {
		.tv_sec = 0,
		.tv_usec = 100000,
	};

	int ret = select((int)dev->vban_socket + 1, &fd_read, NULL, NULL, &tv);
	if (ret > 0 && FD_ISSET(dev->vban_socket, &fd_read))
		return true;

	return false;
}

void *vban_udp_thread_main(void *data)
{
	vban_udp_t *dev = data;

	if (!init_socket(dev)) {
		blog(LOG_ERROR, "vban_udp_thread_main: Failed to initialize.");
		return NULL;
	}

	while (os_atomic_load_long(&dev->refcnt) > -1) {
		struct sockaddr_in addr;
		socklen_t slen = sizeof(addr);

		if (!select_socket(dev))
			continue;

		char buf[VBAN_PROTOCOL_MAX_SIZE];
		int ret = recvfrom(dev->vban_socket, buf, VBAN_PROTOCOL_MAX_SIZE, 0, (struct sockaddr *)&addr, &slen);
		if (ret < 0) {
			blog(LOG_ERROR, "recvfrom returns error");
			continue;
		}

		const struct VBanHeader *header = (const struct VBanHeader *)buf;

		if (memcmp(&header->vban, "VBAN", 4) != 0)
			continue;

		// Assuming VBAN_PROTOCOL_AUDIO = 0
		if (header->format_SR >= VBAN_SR_MAXNUMBER)
			continue;

		if ((header->format_bit & VBAN_CODEC_MASK) != VBAN_CODEC_PCM) {
			blog(LOG_WARNING, "Unsupported VBAN-CODEC: 0x%x", (int)header->format_bit);
			continue;
		}

		pthread_mutex_lock(&dev->mutex);
		for (struct source_list_s *src = dev->sources; src; src = src->next) {
			if ((addr.sin_addr.s_addr & src->mask.s_addr) != (src->addr.s_addr & src->mask.s_addr))
				continue;
			if (src->stream_name[0] &&
			    strncmp(header->streamname, src->stream_name, VBAN_STREAM_NAME_SIZE) != 0)
				continue;
			src->cb(buf, ret, src->data);
		}
		pthread_mutex_unlock(&dev->mutex);
	}

	finalize_socket(dev);

	return NULL;
}
