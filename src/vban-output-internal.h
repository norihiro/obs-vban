#pragma once

#include <util/threading.h>
#if LIBOBS_API_VER < MAKE_SEMANTIC_VERSION(30, 1, 0)
#include <util/circlebuf.h>
#define deque circlebuf
#define deque_pop_front circlebuf_pop_front
#define deque_push_back circlebuf_push_back
#define deque_free circlebuf_free
#else
#include <util/deque.h>
#endif
#include "socket.h"

struct vban_out_s
{
	obs_output_t *context;

	// properties
	int port;
	char *stream_name;
	struct in_addr ip_to;
	size_t mixer;
	int frequency;
	size_t channels;
	uint8_t format_bit;

	// thread
	pthread_mutex_t mutex;
	os_event_t *event;
	pthread_t thread;
	volatile bool cont;

	struct resolve_thread_s *rt;

	struct deque buffer;

	uint64_t cnt_packets;
	uint64_t cnt_frames;
};

void *vban_out_thread_main(void *data);
