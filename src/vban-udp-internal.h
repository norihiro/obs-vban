#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <util/threading.h>
#include "vban.h"
#include "vban-udp.h"
#include "socket.h"

struct source_list_s
{
	vban_udp_cb_t cb;
	void *data;
	struct vban_udp_set_host_thread_s *resolving;

	struct source_list_s *next;
	struct source_list_s **prev_next;

	struct in_addr addr;
	struct in_addr mask;
	char stream_name[VBAN_STREAM_NAME_SIZE];
};

struct vban_udp_s
{
	// instances
	int port;
	vban_udp_t *next;
	vban_udp_t **prev_next;
	volatile long refcnt;

	// locking
	pthread_mutex_t mutex;
	pthread_t thread;
	struct source_list_s *sources;

	// VBAN
	socket_t vban_socket;

	// statistics
	int packets_received;
	int packets_missed;
	int packets_missed_llog;

	uint16_t counter_last;
	bool got_packet;
};

void *vban_udp_thread_main(void *);
