// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef __cplusplus
extern "C" {
#endif

/* The API resolves host name in a newly created thread.
 * If the resolution has succeeded, `resolved` will be called.
 * Otherwise, `failed` will be called.
 */

typedef void (*resolve_thread_resolved_cb)(void *data, const struct in_addr *addr);
typedef void (*resolve_thread_failed_cb)(void *data);

bool resolve_thread_start(const char *name, void *cb_data, resolve_thread_resolved_cb resolved_cb,
			  resolve_thread_failed_cb failed_cb);

#ifdef __cplusplus
} // extern "C"
#endif
