#pragma once

#include <string.h>
#include "socket.h"

typedef struct vban_udp_s vban_udp_t;

vban_udp_t *vban_udp_get_ref(vban_udp_t *dev);
vban_udp_t *vban_udp_find_or_create(int port);
void vban_udp_release(vban_udp_t *dev);

typedef void (*vban_udp_cb_t)(const char *buf, size_t buf_len, const struct sockaddr_in *addr, void *data);
void vban_udp_add_callback(vban_udp_t *dev, vban_udp_cb_t cb, void *data);
void vban_udp_remove_callback(vban_udp_t *dev, vban_udp_cb_t cb, void *data);
