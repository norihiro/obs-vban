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

/* Maximum audio latency kept by the sender. Older audio is dropped so that a
 * stalled or slow sender recovers instead of accumulating an unbounded backlog
 * and later bursting it faster than real time. When the cap is hit, or when a
 * stall left a backlog, audio is trimmed down to VBAN_OUT_TARGET_LATENCY_MS.
 * nuFrame keeps counting sent packets, so a receiver does not see a gap in
 * nuFrame for dropped audio, only a discontinuity in the samples. */
#define VBAN_OUT_MAX_LATENCY_MS 200
#define VBAN_OUT_TARGET_LATENCY_MS 50

/* Packets the sender may catch up after starvation or a stall. */
#define VBAN_OUT_MAX_LAG_PACKETS 2

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
	size_t queued_frames;        // frames held in `buffer`, protected by `mutex`
	size_t max_queued_frames;    // latency cap for `buffer`, see VBAN_OUT_MAX_LATENCY_MS
	size_t target_queued_frames; // size of `buffer` after a trim
	uint64_t dropped_frames;     // frames dropped by the latency cap, protected by `mutex`

	uint64_t cnt_packets;
	uint64_t cnt_frames;
};

void *vban_out_thread_main(void *data);
