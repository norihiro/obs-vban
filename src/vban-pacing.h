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

#pragma once

/*
 * Real-time pacing for the VBAN sender thread.
 *
 * Each packet has a deadline. On every wake-up the sender sends all packets
 * whose deadline has passed, so a late wake-up is made up immediately instead
 * of making the stream fall behind real time.
 *
 * After starvation (no full packet queued, e.g. an idle source) or a stall,
 * the schedule is not allowed to lag more than `max_lag_ns` behind the clock,
 * so a backlog is never sent faster than real time. If a backlog larger than
 * `target_frames` is left, it is trimmed at once, so a stall costs one
 * discontinuity instead of a permanent latency increase. This applies to a
 * stalled sender thread and also to a hiccup of the OBS audio thread that
 * delivers several chunks at once (about 45 ms or more late): by then the
 * receiver has already run dry, and keeping the backlog would only add latency.
 *
 * The buffer is also capped: above `max_frames` it is trimmed down to
 * `target_frames` (hysteresis), so the cap causes one drop, not a series of
 * small drops.
 *
 * Pure logic with no OBS dependency so that it can be unit-tested.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VBAN_PACING_SLACK_NS 1000000ULL // a packet due within 1 ms is sent now

struct vban_pacing
{
	uint64_t next_ns;   // deadline of the next packet, 0 = not started
	uint64_t period_ns; // duration of one packet
	uint64_t max_lag_ns;
	size_t packet_frames;
	size_t max_frames;    // buffer cap
	size_t target_frames; // buffer size after a trim
};

struct vban_pacing_step
{
	size_t drop_frames;    // frames to drop from the head of the buffer, before sending
	size_t send_packets;   // packets to send now
	unsigned long wait_ms; // sleep before the next step
	bool lag_reset;        // the schedule was reset after starvation or a stall
};

static inline void vban_pacing_init(struct vban_pacing *p, size_t packet_frames, uint64_t sample_rate,
				    uint64_t max_lag_packets, size_t max_frames, size_t target_frames)
{
	p->next_ns = 0;
	p->packet_frames = packet_frames;
	p->period_ns = (uint64_t)packet_frames * 1000000000ULL / sample_rate;
	p->max_lag_ns = p->period_ns * max_lag_packets;
	p->max_frames = max_frames;
	p->target_frames = target_frames < max_frames ? target_frames : max_frames;
}

/* Decide what to do on one wake-up of the sender, given the number of frames
 * currently buffered. Never returns wait_ms == 0 while the next deadline is in
 * the future, so the thread does not spin. */
static inline struct vban_pacing_step vban_pacing_step(struct vban_pacing *p, uint64_t now_ns, size_t buffered_frames,
						       unsigned long idle_ms)
{
	struct vban_pacing_step s = {0, 0, idle_ms, false};

	if (buffered_frames > p->max_frames)
		s.drop_frames = buffered_frames - p->target_frames;
	size_t frames = buffered_frames - s.drop_frames;
	size_t available = frames / p->packet_frames;
	if (!available)
		return s;

	if (!p->next_ns) {
		p->next_ns = now_ns;
	}
	else if (p->next_ns + p->max_lag_ns < now_ns) {
		p->next_ns = now_ns - p->max_lag_ns;
		s.lag_reset = true;
		if (frames > p->target_frames) {
			s.drop_frames += frames - p->target_frames;
			frames = p->target_frames;
			available = frames / p->packet_frames;
		}
	}

	while (s.send_packets < available && p->next_ns <= now_ns + VBAN_PACING_SLACK_NS) {
		p->next_ns += p->period_ns;
		s.send_packets++;
	}

	if (s.send_packets < available) {
		/* next_ns > now + slack here, so the wait is at least 1 ms. Round
		 * up: the slack absorbs the rounding. */
		uint64_t d = p->next_ns - now_ns - VBAN_PACING_SLACK_NS;
		uint64_t ms = (d + 999999ULL) / 1000000ULL;
		s.wait_ms = ms < idle_ms ? (unsigned long)ms : idle_ms;
	}
	return s;
}
