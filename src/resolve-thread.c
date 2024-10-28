// SPDX-License-Identifier: GPL-2.0-or-later

#include <obs-module.h>
#include <util/threading.h>
#include "socket.h"
#ifndef _WIN32
#include <sys/types.h>
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
	volatile long refcnt;

	char *name;
	struct in_addr addr;
	bool succeeded;
	volatile bool done;

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

resolve_thread_t *resolve_thread_create(const char *name)
{
	if (!is_valid_hostname(name))
		return NULL;

	pthread_mutex_lock(&mutex);
	n_resolving++;
	pthread_mutex_unlock(&mutex);

	resolve_thread_t *ctx = bzalloc(sizeof(struct resolve_thread_s));
	ctx->name = bstrdup(name);

	return ctx;
}

static void resolve_thread_destroy(resolve_thread_t *ctx)
{
	bfree(ctx->name);
	bfree(ctx);

	pthread_mutex_lock(&mutex);
	if (!--n_resolving)
		pthread_cond_signal(&cond);
	pthread_mutex_unlock(&mutex);
}

resolve_thread_t *resolve_thread_get_ref(resolve_thread_t *ctx)
{
	long owners = os_atomic_load_long(&ctx->refcnt);
	while (owners > -1) {
		if (os_atomic_compare_exchange_long(&ctx->refcnt, &owners, owners + 1))
			return ctx;
	}
	return NULL;
}

void resolve_thread_release(resolve_thread_t *ctx)
{
	if (os_atomic_dec_long(&ctx->refcnt) == -1)
		resolve_thread_destroy(ctx);
}

void resolve_thread_set_callbacks(resolve_thread_t *ctx, void *cb_data, resolve_thread_resolved_cb resolved_cb,
				  resolve_thread_failed_cb failed_cb)
{
	ctx->resolved_cb = resolved_cb;
	ctx->failed_cb = failed_cb;
	ctx->cb_data = cb_data;
}

bool resolve_thread_start(resolve_thread_t *ctx)
{
	ctx = resolve_thread_get_ref(ctx);
	if (!ctx)
		return false;

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
		ctx->addr = addr;

		if (ctx->resolved_cb)
			ctx->resolved_cb(ctx->cb_data, &addr);

		ctx->succeeded = true;
	}
	else {
		if (ctx->failed_cb)
			ctx->failed_cb(ctx->cb_data);
	}

	os_atomic_store_bool(&ctx->done, true);

	if (ctx->succeeded)
		blog(LOG_DEBUG, "Succeeded to resolve '%s' to %s", ctx->name, inet_ntoa(ctx->addr));
	else
		blog(LOG_DEBUG, "Failed to resolve '%s'", ctx->name);

	resolve_thread_release(ctx);
	return NULL;
}

bool resolve_thread_done(const resolve_thread_t *ctx)
{
	return os_atomic_load_bool(&ctx->done);
}

bool resolve_thread_get_addr(const resolve_thread_t *ctx, struct in_addr *addr)
{
	if (os_atomic_load_bool(&ctx->done) && ctx->succeeded) {
		*addr = ctx->addr;
		return true;
	}
	else {
		return false;
	}
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
