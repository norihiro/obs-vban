#pragma once

#include <util/threading.h>
#include <util/circlebuf.h>
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

	struct circlebuf buffer;

	uint64_t cnt_packets;
	uint64_t cnt_frames;
};

void *vban_out_thread_main(void *data);
