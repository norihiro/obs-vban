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

#include <stdlib.h>
#include <obs-module.h>
#include "plugin-macros.generated.h"
#include <util/threading.h>
#include "vban-udp-internal.h"
#include "resolve-thread.h"

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static vban_udp_t *devices = NULL;

vban_udp_t *vban_udp_get_ref(vban_udp_t *dev)
{
	// This function is equivalent to this code but thread-safe.
	// if (dev->refcnt > -1) {
	//   dev->refcnt ++;
	//   return dev;
	// } else {
	//   return NULL;
	// }
	long owners = os_atomic_load_long(&dev->refcnt);
	while (owners > -1) {
		// Code block below is equivalent to this code.
		// if (dev->refcnt == owners) {
		//   dev->refcnt = owners + 1;
		//   return dev;
		// } else {
		//   owners = dev->refcnt;
		// }
		if (os_atomic_compare_exchange_long(&dev->refcnt, &owners, owners + 1))
			return dev;
	}
	return NULL;
}

static vban_udp_t *vban_udp_find_unlocked(int port)
{
	for (vban_udp_t *dev = devices; dev; dev = dev->next) {
		if (dev->port == port)
			return vban_udp_get_ref(dev);
	}
	return NULL;
}

static void vban_udp_remove_from_devices_unlocked(vban_udp_t *dev)
{
	if (dev && dev->prev_next) {
		*dev->prev_next = dev->next;
		if (dev->next)
			dev->next->prev_next = dev->prev_next;
		dev->prev_next = NULL;
		dev->next = NULL;
	}
}

static vban_udp_t *vban_udp_create_unlocked(int port);
static void vban_udp_destroy(vban_udp_t *dev);

vban_udp_t *vban_udp_find_or_create(int port)
{
	pthread_mutex_lock(&mutex);
	vban_udp_t *dev = vban_udp_find_unlocked(port);
	if (dev) {
		pthread_mutex_unlock(&mutex);
		return dev;
	}

	dev = vban_udp_create_unlocked(port);

	pthread_mutex_unlock(&mutex);

	return dev;
}

void vban_udp_release(vban_udp_t *dev)
{
	if (os_atomic_dec_long(&dev->refcnt) == -1)
		vban_udp_destroy(dev);
}

static vban_udp_t *vban_udp_create_unlocked(int port)
{
	vban_udp_t *dev = bzalloc(sizeof(struct vban_udp_s));
	dev->port = port;
	dev->next = devices;
	dev->prev_next = &devices;
	if (dev->next)
		dev->next->prev_next = &dev->next;
	devices = dev;

	pthread_mutex_init(&dev->mutex, NULL);
	pthread_create(&dev->thread, NULL, vban_udp_thread_main, dev);

	return dev;
}

static void vban_udp_destroy(vban_udp_t *dev)
{
	pthread_mutex_lock(&mutex);
	vban_udp_remove_from_devices_unlocked(dev);
	pthread_mutex_unlock(&mutex);

	pthread_join(dev->thread, NULL);
	if (dev->sources)
		blog(LOG_ERROR, "vban_udp_destroy: sources are remaining");
	pthread_mutex_destroy(&dev->mutex);

	bfree(dev);
}

void vban_udp_add_callback(vban_udp_t *dev, vban_udp_cb_t cb, void *data)
{
	struct source_list_s *item = bzalloc(sizeof(struct source_list_s));
	item->cb = cb;
	item->data = data;

	pthread_mutex_lock(&dev->mutex);
	item->next = dev->sources;
	item->prev_next = &dev->sources;
	dev->sources = item;
	if (item->next)
		item->next->prev_next = &item->next;

	pthread_mutex_unlock(&dev->mutex);
}

void vban_udp_remove_callback(vban_udp_t *dev, vban_udp_cb_t cb, void *data)
{
	pthread_mutex_lock(&dev->mutex);

	for (struct source_list_s *item = dev->sources; item; item = item->next) {
		if (item->cb != cb)
			continue;
		if (item->data != data)
			continue;

		*item->prev_next = item->next;
		if (item->next)
			item->next->prev_next = item->prev_next;
		bfree(item);
		break;
	}

	pthread_mutex_unlock(&dev->mutex);
}

static void copy_stream_name(char *dst, const char *src)
{
	int i = 0;
	for (; i < VBAN_STREAM_NAME_SIZE && src[i]; i++)
		dst[i] = src[i];
	for (; i < VBAN_STREAM_NAME_SIZE; i++)
		dst[i] = 0;
}

void vban_udp_set_name(vban_udp_t *dev, vban_udp_cb_t cb, void *data, const char *name)
{
	blog(LOG_INFO, "stream-name: '%s'", name);

	pthread_mutex_lock(&dev->mutex);

	for (struct source_list_s *item = dev->sources; item; item = item->next) {
		if (item->cb != cb)
			continue;
		if (item->data != data)
			continue;

		copy_stream_name(item->stream_name, name);
		break;
	}

	pthread_mutex_unlock(&dev->mutex);
}

static void vban_udp_set_addr_mask(vban_udp_t *dev, vban_udp_cb_t cb, void *data, const struct in_addr *addr,
				   const struct in_addr *mask)
{
	pthread_mutex_lock(&dev->mutex);

	for (struct source_list_s *item = dev->sources; item; item = item->next) {
		if (item->cb != cb)
			continue;
		if (item->data != data)
			continue;

		item->addr = *addr;
		item->mask = *mask;
		item->resolving = NULL;
		break;
	}

	pthread_mutex_unlock(&dev->mutex);
}

struct vban_udp_set_host_thread_s
{
	vban_udp_t *dev;
};

static inline void mark_resolving(vban_udp_t *dev, vban_udp_cb_t cb, void *data,
				  struct vban_udp_set_host_thread_s *resolving)
{
	pthread_mutex_lock(&dev->mutex);

	for (struct source_list_s *item = dev->sources; item; item = item->next) {
		if (item->cb != cb)
			continue;
		if (item->data != data)
			continue;

		/* If there is an earlier resolving thread, the result from the
		 * earlier thread will be ignored even if the earlier thread
		 * takes more time than the later resolving thread(s). */
		item->resolving = resolving;
		break;
	}

	pthread_mutex_unlock(&dev->mutex);
}

static void resolve_finalize(struct vban_udp_set_host_thread_s *ctx, const struct in_addr *addr)
{
	pthread_mutex_lock(&ctx->dev->mutex);

	for (struct source_list_s *item = ctx->dev->sources; item; item = item->next) {
		if (item->resolving != ctx)
			continue;

		if (addr) {
			struct in_addr mask = {0};
			mask.s_addr = 0xFFFFFFFF;
			item->addr = *addr;
			item->mask = mask;
		}
		item->resolving = NULL;
		break;
	}

	pthread_mutex_unlock(&ctx->dev->mutex);

	vban_udp_release(ctx->dev);
	bfree(ctx);
}

static void resolve_succeeded(void *data, const struct in_addr *addr)
{
	struct vban_udp_set_host_thread_s *ctx = data;
	resolve_finalize(ctx, addr);
}

static void resolve_failed(void *data)
{
	struct vban_udp_set_host_thread_s *ctx = data;
	resolve_finalize(ctx, NULL);
}

void vban_udp_set_host(vban_udp_t *dev, vban_udp_cb_t cb, void *data, const char *host)
{
	struct in_addr addr = {0};
	struct in_addr mask = {0};

	if (!host || !*host) {
		blog(LOG_INFO, "host address: %s (accepting everything)", inet_ntoa(addr));
		vban_udp_set_addr_mask(dev, cb, data, &addr, &mask);
		return;
	}

	if (inet_pton(AF_INET, host, &addr)) {
		blog(LOG_INFO, "host address: %s", inet_ntoa(addr));
		mask.s_addr = 0xFFFFFFFF;
		vban_udp_set_addr_mask(dev, cb, data, &addr, &mask);
		return;
	}

	resolve_thread_t *rt = resolve_thread_create(host);
	if (!rt)
		return;

	dev = vban_udp_get_ref(dev);
	if (!dev) {
		blog(LOG_ERROR, "vban_udp_set_host: Error: Cannot get reference");
		resolve_thread_release(rt);
		return;
	}

	struct vban_udp_set_host_thread_s *ctx = bzalloc(sizeof(struct vban_udp_set_host_thread_s));
	ctx->dev = dev;
	mark_resolving(dev, cb, data, ctx);

	resolve_thread_set_callbacks(rt, ctx, resolve_succeeded, resolve_failed);

	blog(LOG_DEBUG, "%p: Resolving '%s'", ctx, host);
	resolve_thread_start(rt);

	resolve_thread_release(rt);
}
