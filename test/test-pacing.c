/*
 * Unit tests for src/vban-pacing.h (no OBS dependency).
 * Build: cc -I src test/test-pacing.c -o test-pacing && ./test-pacing
 *
 * The sender loop is simulated: OBS delivers 1024-frame chunks every 21.33 ms,
 * the sender thread wakes up later than requested (as measured on macOS), and
 * each wake-up applies vban_pacing_step() exactly like vban-output-thread.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include "vban-pacing.h"

#define RATE 48000ULL
#define PACKET 239 // 24-bit stereo: 1434-byte payload
#define CHUNK 1024 // OBS audio chunk
#define NS 1000000000ULL
#define MS 1000000ULL
#define MAX_FRAMES 9600    // 200 ms
#define TARGET_FRAMES 2400 // 50 ms

static int failures = 0;

#define CHECK(cond, ...)                     \
	do {                                 \
		if (!(cond)) {               \
			printf("FAIL: ");    \
			printf(__VA_ARGS__); \
			printf("\n");        \
			failures++;          \
		}                            \
	} while (0)

struct sim_result
{
	double rate;              // frames/s sent over the last half of the run
	size_t max_burst;         // packets sent in one wake-up after the warm-up
	size_t drop_events;       // wake-ups that dropped audio
	size_t final_buffered;    // frames buffered at the end
	size_t max_late_buffered; // max frames buffered over the last 10 s
	unsigned long zero_waits; // wake-ups that asked for a 0 ms sleep with data pending
};

/* Simulate the sender for `seconds`. `wake_late_ns` is added to every sleep.
 * The sender thread is stalled during [stall_at, stall_at + stall_ns). */
static struct sim_result simulate(int legacy, uint64_t wake_late_ns, double seconds, uint64_t stall_at_ns,
				  uint64_t stall_ns)
{
	struct sim_result r = {0};
	struct vban_pacing p;
	vban_pacing_init(&p, PACKET, RATE, 2, MAX_FRAMES, TARGET_FRAMES);

	const uint64_t t0 = 1000 * NS;
	const uint64_t end = t0 + (uint64_t)(seconds * NS);
	const uint64_t half = t0 + (end - t0) / 2;
	uint64_t next_chunk = t0, wake = t0, chunks = 0;
	size_t buffered = 0;
	uint64_t sent_late = 0;
	unsigned long legacy_wait_ms = 100;

	while (wake < end) {
		/* Chunks produced by OBS until this wake-up; the event wakes the thread
		 * as soon as a chunk arrives. */
		while (next_chunk <= wake) {
			buffered += CHUNK;
			chunks++;
			next_chunk = t0 + chunks * CHUNK * NS / RATE;
		}
		uint64_t now = wake;
		size_t sent;
		unsigned long wait_ms;
		if (legacy) {
			/* Previous loop: at most one packet per wake-up, then sleep
			 * nbs * 1000 / rate ms (4 ms), no cap. */
			sent = buffered >= PACKET ? 1 : 0;
			buffered -= sent * PACKET;
			wait_ms = sent ? 4 : legacy_wait_ms;
		}
		else {
			struct vban_pacing_step s = vban_pacing_step(&p, now, buffered, 100);
			if (s.drop_frames && now > t0 + NS)
				r.drop_events++;
			buffered -= s.drop_frames;
			buffered -= s.send_packets * PACKET;
			sent = s.send_packets;
			wait_ms = s.wait_ms;
			if (!wait_ms && buffered >= PACKET)
				r.zero_waits++;
		}
		if (now > t0 + NS && sent > r.max_burst)
			r.max_burst = sent;
		if (now >= half)
			sent_late += sent * PACKET;
		if (now + 10 * NS >= end && buffered > r.max_late_buffered)
			r.max_late_buffered = buffered;

		uint64_t timeout = now + wait_ms * MS + wake_late_ns;
		uint64_t by_event = next_chunk + wake_late_ns;
		wake = timeout < by_event ? timeout : by_event;
		if (wake <= now)
			wake = now + 1000; // a 0 ms wait still costs a few us
		if (stall_ns && now < stall_at_ns + stall_ns && wake >= stall_at_ns)
			wake = wake > stall_at_ns + stall_ns ? wake : stall_at_ns + stall_ns;
	}
	r.rate = (double)sent_late * NS / (double)(end - half);
	r.final_buffered = buffered;
	return r;
}

static void test_real_time_with_late_wakeups(void)
{
	/* Measured on macOS: wake-ups ~1.46 ms late (5.46 ms for 4 ms requested). */
	struct sim_result fixed = simulate(0, 1460000, 120.0, 0, 0);
	struct sim_result legacy = simulate(1, 1460000, 120.0, 0, 0);
	printf("late wake-ups: fixed %.0f f/s buffered %zu | legacy %.0f f/s buffered %zu\n", fixed.rate,
	       fixed.final_buffered, legacy.rate, legacy.final_buffered);
	CHECK(fixed.rate > RATE * 0.999 && fixed.rate < RATE * 1.001, "fixed rate %.0f", fixed.rate);
	CHECK(fixed.final_buffered < CHUNK + PACKET, "fixed backlog stays below one chunk, got %zu",
	      fixed.final_buffered);
	CHECK(fixed.drop_events == 0, "no drop without a stall, got %zu", fixed.drop_events);
	CHECK(legacy.final_buffered > RATE, "legacy model builds a backlog of seconds (the bug), got %zu",
	      legacy.final_buffered);
}

static void test_no_spin_with_precise_timers(void)
{
	/* Linux / Windows with 1 ms timer resolution: wake-ups are on time. */
	struct sim_result r = simulate(0, 50000, 60.0, 0, 0);
	printf("precise timers: rate %.0f, zero-ms waits with data pending: %lu\n", r.rate, r.zero_waits);
	CHECK(r.zero_waits == 0, "the sender must never spin with a 0 ms wait, got %lu", r.zero_waits);
	CHECK(r.rate > RATE * 0.999 && r.rate < RATE * 1.001, "rate %.0f", r.rate);
}

static void test_stall_recovers_latency(unsigned long long stall_ms)
{
	uint64_t t0 = 1000 * NS;
	struct sim_result r = simulate(0, 1460000, 60.0, t0 + 5 * NS, stall_ms * MS);
	printf("stall %llu ms: burst %zu, drop events %zu, max buffered in last 10 s %zu, rate %.0f\n", stall_ms,
	       r.max_burst, r.drop_events, r.max_late_buffered, r.rate);
	CHECK(r.max_burst <= 4, "stall %llu ms: burst must stay bounded, got %zu", stall_ms, r.max_burst);
	CHECK(r.drop_events <= 1, "stall %llu ms: at most one drop event, got %zu", stall_ms, r.drop_events);
	/* A stall leaves at most the target latency (50 ms), not the 200 ms cap:
	 * trimming lower would also drop audio on ordinary OBS chunk jitter. */
	CHECK(r.max_late_buffered <= TARGET_FRAMES, "stall %llu ms: latency must stay at or below the target, got %zu",
	      stall_ms, r.max_late_buffered);
	CHECK(r.rate > RATE * 0.999 && r.rate < RATE * 1.001, "stall %llu ms: rate %.0f", stall_ms, r.rate);
}

static void test_no_burst_after_idle(void)
{
	struct vban_pacing p;
	vban_pacing_init(&p, PACKET, RATE, 2, MAX_FRAMES, TARGET_FRAMES);
	uint64_t now = 1000 * NS;
	CHECK(vban_pacing_step(&p, now, 10 * PACKET, 100).send_packets == 1, "first call sends one packet");
	now += 10 * NS; // 10 s without audio (idle browser source)
	CHECK(vban_pacing_step(&p, now, 100, 100).send_packets == 0, "partial packet: nothing to send");
	struct vban_pacing_step s = vban_pacing_step(&p, now, CHUNK, 100);
	CHECK(s.send_packets <= 3, "after idle the sender must not burst, got %zu", s.send_packets);
	CHECK(s.drop_frames == 0, "a single chunk after idle is not dropped, got %zu", s.drop_frames);
}

static void test_cap_trims_to_target(void)
{
	struct vban_pacing p;
	vban_pacing_init(&p, PACKET, RATE, 2, MAX_FRAMES, TARGET_FRAMES);
	struct vban_pacing_step s = vban_pacing_step(&p, 1000 * NS, 48000 * 30, 100);
	CHECK(s.drop_frames == 48000 * 30 - TARGET_FRAMES, "30 s backlog trimmed to the target, dropped %zu",
	      s.drop_frames);
	s = vban_pacing_step(&p, 1000 * NS, MAX_FRAMES, 100);
	CHECK(s.drop_frames == 0, "at the cap nothing is dropped, got %zu", s.drop_frames);
}

static void test_wait_never_zero_before_deadline(void)
{
	struct vban_pacing p;
	vban_pacing_init(&p, PACKET, RATE, 2, MAX_FRAMES, TARGET_FRAMES);
	uint64_t now = 1000 * NS;
	vban_pacing_step(&p, now, PACKET, 100); // next deadline: now + 4.979 ms
	for (uint64_t dt = 0; dt < 5 * MS; dt += 100000) {
		struct vban_pacing_step s = vban_pacing_step(&p, now + dt, 2 * PACKET, 100);
		if (!s.send_packets)
			CHECK(s.wait_ms >= 1, "dt=%llu us: wait must be >= 1 ms when nothing is due, got %lu",
			      (unsigned long long)(dt / 1000), s.wait_ms);
		if (s.send_packets)
			break;
	}
	struct vban_pacing_step s = vban_pacing_step(&p, now, 0, 100);
	CHECK(s.wait_ms == 100, "no data: idle wait, got %lu", s.wait_ms);
}

int main(void)
{
	test_real_time_with_late_wakeups();
	test_no_spin_with_precise_timers();
	test_stall_recovers_latency(150);
	test_stall_recovers_latency(300);
	test_stall_recovers_latency(3000);
	test_no_burst_after_idle();
	test_cap_trims_to_target();
	test_wait_never_zero_before_deadline();
	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("all pacing tests passed\n");
	return 0;
}
