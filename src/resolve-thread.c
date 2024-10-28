// SPDX-License-Identifier: GPL-2.0-or-later

#include <obs-module.h>
#include <util/threading.h>
#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ctype.h>
#else
#include <ws2tcpip.h>
#endif
#include "plugin-macros.generated.h"
#include "resolve-thread.h"

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static volatile long long n_resolving = 0;

struct resolve_thread_s
{
	char *name;
	resolve_thread_resolved_cb resolved_cb;
	resolve_thread_failed_cb failed_cb;
	void *cb_data;
};

static void *resolve_thread_main(void *data);

static bool is_valid_hostname(const char *host)
{
	if (!host)
		return false;

	bool mid = false;
	bool prev_was_alnum = false;

	while (*host) {
		char c = *host++;
		if (c == '.') {
			if (!prev_was_alnum)
				return false;
			mid = false;
			prev_was_alnum = false;
		}
		else if (c == '-') {
			if (!mid)
				return false;
			prev_was_alnum = false;
		}
		else if (isalnum((unsigned char)c)) {
			mid = true;
			prev_was_alnum = true;
		}
	}

	return prev_was_alnum;
}

bool resolve_thread_start(const char *name, void *cb_data, resolve_thread_resolved_cb resolved_cb,
			  resolve_thread_failed_cb failed_cb)
{
	if (!is_valid_hostname(name)) {
		if (failed_cb)
			failed_cb(cb_data);
		return false;
	}

	pthread_mutex_lock(&mutex);
	n_resolving++;
	pthread_mutex_unlock(&mutex);

	struct resolve_thread_s *ctx = bzalloc(sizeof(struct resolve_thread_s));
	ctx->name = bstrdup(name);
	ctx->resolved_cb = resolved_cb;
	ctx->failed_cb = failed_cb;
	ctx->cb_data = cb_data;

	pthread_t thread;
	if (pthread_create(&thread, NULL, resolve_thread_main, ctx)) {
		blog(LOG_ERROR, "Failed to create resolving thread for '%s'", ctx->name);
		return false;
	}

	pthread_detach(thread);

	return true;
}

static void *resolve_thread_main(void *data)
{
	struct resolve_thread_s *ctx = data;

	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_DGRAM,
		.ai_protocol = IPPROTO_UDP,
		.ai_flags = 0,
	};

	struct addrinfo *res;

	if (getaddrinfo(ctx->name, NULL, &hints, &res) == 0) {
		struct in_addr addr = {0};
		addr.s_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
		freeaddrinfo(res);

		if (ctx->resolved_cb)
			ctx->resolved_cb(ctx->cb_data, &addr);
	}
	else {
		if (ctx->failed_cb)
			ctx->failed_cb(ctx->cb_data);
	}

	bfree(ctx->name);
	bfree(ctx);

	pthread_mutex_lock(&mutex);
	if (!--n_resolving)
		pthread_cond_signal(&cond);
	pthread_mutex_unlock(&mutex);

	return NULL;
}

void resolve_thread_wait_all()
{
	pthread_mutex_lock(&mutex);
	while (n_resolving > 0) {
		blog(LOG_INFO, "resolve_thread_wait_all: There are %lld resolving instances. Waiting...", n_resolving);
		pthread_cond_wait(&cond, &mutex);
	}
	pthread_mutex_unlock(&mutex);
}
