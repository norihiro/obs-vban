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
