// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The API resolves host name in a newly created thread.
 * If the resolution has succeeded, `resolved` will be called.
 * Otherwise, `failed` will be called.
 */

struct in_addr;
typedef struct resolve_thread_s resolve_thread_t;

typedef void (*resolve_thread_resolved_cb)(void *data, const struct in_addr *addr);
typedef void (*resolve_thread_failed_cb)(void *data);

/**
 * Create a host name resolution thread.
 * @param[in] name  Host name
 * @return          Context to resolve the name.
 *
 * The returned context should be released by `resolve_thread_release`.
 */
resolve_thread_t *resolve_thread_create(const char *name);

/**
 * Increment the reference count.
 * @param[in] ctx  The context.
 * @return         Context if successfully increment the reference count, otherwise NULL.
 */
resolve_thread_t *resolve_thread_get_ref(resolve_thread_t *ctx);

/**
 * Release the context.
 * @param[in] ctx  The context.
 */
void resolve_thread_release(resolve_thread_t *ctx);

/**
 * Set the callback functions.
 * @param[in] ctx          The context.
 * @param[in] cb_data      A parameter transparently passed to the callback functions.
 * @param[in] resolved_cb  A function called when the resolution has succeeded.
 * @param[in] failed_cb    A function called when the resolution has failed.
 *
 * Either the `resolved_cb` or `failed_cb` will be called only once.
 */
void resolve_thread_set_callbacks(resolve_thread_t *ctx, void *cb_data, resolve_thread_resolved_cb resolved_cb,
				  resolve_thread_failed_cb failed_cb);

/**
 * Start the resolution.
 * @param[in] ctx  The context.
 *
 * Inside this API, the thread will be created.
 */
bool resolve_thread_start(resolve_thread_t *ctx);

/**
 * Query if the resolution was done.
 * @param[in] ctx  The context.
 * @return         True if the resolution was done.
 *
 * This function will return true if the resolution has succeeded or failed.
 * If the resolution has not started or is in progress, return false.
 */
bool resolve_thread_done(const resolve_thread_t *ctx);

/**
 * Query the address.
 * @param[in] ctx    The context.
 * @param[out] addr  Pointer to store the address.
 * @return           True if succeeded.
 *
 * If the resolution has succeeded, the resolved address will be stored to `*addr` and will return `true`.
 * Otherwise, `false` will be returned and `*addr` won't be updated.
 */
bool resolve_thread_get_addr(const resolve_thread_t *ctx, struct in_addr *addr);

#ifdef __cplusplus
} // extern "C"
#endif
