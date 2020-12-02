/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "module.h"
#include "lua/utils.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <dlfcn.h>
#include <small/small.h>

#include "version.h"
#include "fiber.h"
#include "cbus.h"
#include "say.h"
#include "sio.h"
#include "evio.h"
#include "coio.h"
#include "tt_static.h"

#define MODULE_MSG_SIZE_MIN 2
#define SZR(arr) sizeof(arr) / sizeof(arr[0])
#define DECLARE_WAKEUP_FUNCTION(name, evdata, fiber)			\
static void								\
name##_process_wakeup(ev_loop *loop, ev_async *ev, int revents)		\
{									\
	(void)loop;							\
	(void)revents;							\
	struct evdata *data= (struct evdata *)ev->data;			\
	assert(data->fiber != NULL);					\
	/* Wait until fiber sleep */					\
	while (data->fiber->flags & FIBER_IS_READY)			\
		;							\
	fiber_wakeup(data->fiber);					\
}

/** Module life cycle stages. */
enum {
	/* Module was loaded but not used */
	MODULE_LOADED = 0,
	/* Module thread prepare to start */
	MODULE_PENDING_STARTED,
	/* Module main loop is worked now */
	MODULE_STARTED,
	/* Module pending main loop completion */
	MODULE_STOPPED,
};

struct module {
	/* Module state */
	int state;
	/* Module cbus endpoint */
	struct cbus_endpoint endpoint;
	/* Thread of main module loop */
	struct cord module_cord;
	/* Pipe to tx thread */
	struct cpipe tx_pipe;
	/* Pipe to module */
	struct cpipe module_pipe;
	/* Module name */
	char *name;
	/* Dlopen library handle */
	void *handle;
	/* Module opaque context */
	void *ctx;
	/* Link to module list */
	struct rlist link;
	/* Module event loop */
	struct ev_loop *loop;
	/* Tx thread main loop */
	struct ev_loop *tx_loop;
	/* Async event for set when module_cord thread finished */
	struct ev_async module_cord_async;
	/* Fiber waited for module stop */
	struct fiber *stop_fiber;
	/* Flag set that module_cord finished so fast without async send */
	int module_cord_finished;
	/* Route for this module */
	struct cmsg_hop route[2];
	/* Mempool for all requests */
	struct mempool request_pool;
	/* Virtual function module table */
	struct tarantool_module_vtab *vtab;
	/* Flag that we need restart module */
	int restart;
	/* Flag signaled that thread finished its main func and soon stop */
	int soon_stoped;
	/* Convar mutex, wait for tx thread started */
	pthread_mutex_t mutex;
	/* Convar, wait for tx thread ready */
	pthread_cond_t cond;
	/* Owner of mutex */
	pthread_t mutex_owner;
};

/**
 * Request - contains information about tarantool module request
 */
struct m_request {
	struct cmsg base;
	/* Tx thread fiber processed message */
	struct fiber *tx_fiber;
	/* Module thread fiber processed message */
	struct fiber *m_fiber;
	/* Module loop waiting event */
	struct ev_loop *loop;
	/* Async event for module loop */
	struct ev_async async;
	/* Set true in case critical error during processing */
	bool failed;
	/* Flag set that message finished so fast without async send */
	int finished;
	/* Pointer to function called in tx thread to process request */
	void (*tx_process_request)(void *);
	/* Pointer to function calles in module thread to process request */
	void (*m_process_request)(void *, int);
	/* Pointer to processed data */
	void *data;
	/* Request module */
	struct module *module;
};

DECLARE_WAKEUP_FUNCTION(msg, m_request, m_fiber)
DECLARE_WAKEUP_FUNCTION(module_stop, module, stop_fiber)

/* List of all loaded modules */
static struct rlist module_list = RLIST_HEAD_INITIALIZER(module_list);
static void *avoid_optimization[3];
static int tx_ready;

static void
async_wait_event(struct ev_loop *loop, struct ev_async *async, int *already)
{
	ev_async_start(loop, async);
	bool cancellable = fiber_set_cancellable(false);
	if (pm_atomic_load(already) == 0)
		fiber_yield();
	fiber_set_cancellable(cancellable);
	ev_async_stop(loop, async);
}

static int
tx_fiber_f(va_list ap)
{
	struct m_request *request = va_arg(ap, struct m_request *);
	fiber_sleep(0);
	request->tx_process_request(request->data);
	pm_atomic_store(&request->finished, 1);
	ev_async_send(request->loop, &request->async);
	return 0;
}

/* Process all module requests */
static void
tx_process(struct cmsg *m)
{
	struct m_request *request = (struct m_request *)m;
	request->tx_fiber = fiber_new("tmp", tx_fiber_f);
	if (!request->tx_fiber) {
		request->failed = true;
		return;
	}
	fiber_set_joinable(request->tx_fiber, true);
	fiber_start(request->tx_fiber, request);
}

static inline void
_m_fiber_f(struct m_request *request)
{
	async_wait_event(request->loop, &request->async, &request->finished);
	fiber_join(request->tx_fiber);
	request->m_process_request(request->data, request->failed);
}

static int
m_fiber_f(va_list ap)
{
	struct m_request *request = va_arg(ap, struct m_request *);
	_m_fiber_f(request);
	mempool_free(&request->module->request_pool, request);
	return 0;
}

/* Send tx reply to module and delete msg */
static void
m_process(struct cmsg *m)
{
	struct m_request *request = (struct m_request *) m;
	if (!request->failed) {
		request->m_fiber = fiber_new("tmp", m_fiber_f);
		if (request->m_fiber) {
			fiber_start(request->m_fiber, request);
			return;
		} else {
			request->m_fiber = fiber();
			(void)_m_fiber_f(request);
		}
	} else {
		request->m_process_request(request->data, request->failed);
	}
	mempool_free(&request->module->request_pool, request);
}

void
tx_msg_send(void *m, void *data, void (*tx_process_request)(void *),
	void (*m_process_request)(void *, int))
{
	struct module *module = (struct module *)m;
	struct m_request *request = (struct m_request *)mempool_alloc(&module->request_pool);
	if (!request)
		return;
	memset(request, 0, sizeof(struct m_request));
	request->module = module;
	request->data = data;
	request->tx_process_request = tx_process_request;
	request->m_process_request = m_process_request;
	cmsg_init(&request->base, module->route);
	request->loop = module->loop;
	ev_async_init(&request->async, msg_process_wakeup);
	request->async.data = request;
	cpipe_push_input(&module->tx_pipe, &request->base);
	cpipe_flush_input(&module->tx_pipe);
}

static void
process_endpoint_cb(ev_loop *loop, ev_watcher *watcher, int revents)
{
	(void)loop;
	(void)revents;
	struct cbus_endpoint *endpoint= (struct cbus_endpoint *)watcher->data;
	assert(endpoint != NULL);
	cbus_process(endpoint);
}

static void
module_cleanup_handler(void *arg)
{
	struct module *module = (struct module *)arg;
	/* Unlock mutex if we was cancelled during condvar witing */
	if (pthread_equal(pm_atomic_load(&module->mutex_owner), pthread_self())) {
		pthread_mutex_trylock(&module->mutex);
		pthread_mutex_unlock(&module->mutex);
	}
	if (pm_atomic_load(&module->state) == MODULE_STARTED ||
	    pm_atomic_load(&module->state) == MODULE_STOPPED)
		cpipe_destroy(&module->tx_pipe);
	cpipe_destroy(&module->module_pipe);
	cbus_endpoint_destroy(&module->endpoint, cbus_process);
	mempool_destroy(&module->request_pool);
	pm_atomic_store(&module->module_cord_finished, 1);
	ev_async_send(module->tx_loop, &module->module_cord_async);
	/* Unexpected stop because something failed in start function */
	if (!module->stop_fiber)
		pm_atomic_store(&module->state, MODULE_LOADED);
}

static void *
module_cord_f(void *arg)
{
	struct module *module = (struct module *)arg;
	pm_atomic_store(&module->soon_stoped, 0);
	pthread_cleanup_push(module_cleanup_handler, module);
	module->loop = loop();
	mempool_create(&module->request_pool, &cord()->slabc, sizeof(struct m_request));
	/* Create "net" endpoint. */
	cbus_endpoint_create(&module->endpoint, module->name, process_endpoint_cb, &module->endpoint);
	pthread_mutex_lock(&module->mutex);
	pm_atomic_store(&module->mutex_owner, pthread_self());
	while (!tx_ready)
		pthread_cond_wait(&module->cond, &module->mutex);
	pthread_mutex_unlock(&module->mutex);
	pm_atomic_store(&module->mutex_owner, 0);
	int expected;
	do {
		expected = MODULE_PENDING_STARTED;
	} while (!pm_atomic_compare_exchange_strong(&module->state, &expected, MODULE_STARTED));
	cpipe_create(&module->tx_pipe, "tx");
	cpipe_set_max_input(&module->tx_pipe, MODULE_MSG_SIZE_MIN / 2);
	module->vtab->start(module->ctx, loop(), ev_get_pipew(loop()));
	pm_atomic_store(&module->soon_stoped, 1);
	module_cleanup_handler(module);
	pthread_cleanup_pop(0);
	return (void *)0;
}

static int
module_stop_f(va_list ap)
{
	struct module *module = va_arg(ap, struct module *);
	module->vtab->stop(module->ctx);
	async_wait_event(module->tx_loop, &module->module_cord_async,
		&module->module_cord_finished);
	cord_join(&module->module_cord);
	/* When we yield some one set module unload */
	pm_atomic_store(&module->state, MODULE_LOADED);
	module->module_cord_finished = 0;
	module->stop_fiber = NULL;
	if (pm_atomic_load(&module->restart)) {
		const char *error;
		tarantool_start_module(module, &error);
	}
	return 0;
}

int
tarantool_register_module(const char *name, struct tarantool_module_vtab *vtab, const char **error)
{
	struct module *module = (struct module *)malloc(sizeof(struct module));
	if (module == NULL) {
		*error = tt_sprintf("error: failed to allocate '%u' bytes for struct module", sizeof(*module));
		goto finish_bad;
	}
	memset(module, 0, sizeof(struct module));
	module->name = (char *)malloc(strlen(name) + 1);
	if (module->name == NULL) {
		*error = tt_sprintf("error: failed to allocate '%u' bytes for module name", strlen(name) + 1);
		goto free_module;
	}
	strcpy(module->name, name);
	if (pthread_cond_init(&module->cond, NULL) < 0) {
		*error = tt_sprintf("error: failed to init module condvar");
		goto free_module_name;
	}
	if (pthread_mutex_init(&module->mutex, NULL) < 0) {
		*error = tt_sprintf("error: failed to init condvar mutex");
		goto destroy_module_cond;
	}
	module->state = MODULE_LOADED;
	module->ctx = NULL;
	module->tx_loop = loop();
	ev_async_init(&module->module_cord_async, module_stop_process_wakeup);
	module->module_cord_async.data = module;
	module->vtab = vtab;
	if (module->vtab->init(&module->ctx, module) < 0) {
		*error = tt_sprintf("error: failed to init '%s' module", name);
		goto destroy_module_mutex;
	}

	rlist_add_entry(&module_list, module, link);
	return 0;

destroy_module_mutex:
	pthread_mutex_destroy(&module->mutex);
destroy_module_cond:
	pthread_cond_destroy(&module->cond);
free_module_name:
	free(module->name);
free_module:
	free(module);
finish_bad:
	return -1;
}

int
tarantool_start_module(void *module, const char **error)
{
	struct module *module_ = (struct module *)module;
	if (pm_atomic_load(&module_->state) != MODULE_LOADED) {
		/* Try to stop it and restart later */
		return tarantool_stop_module(module_, error, 1);
	}
	if (cord_start(&module_->module_cord, module_->name, module_cord_f, module_)) {
		*error = tt_sprintf("error: unable to start '%s' module thread", module_->name);
		return -1;
	}
	cpipe_create(&module_->module_pipe, module_->name);
	cpipe_set_max_input(&module_->module_pipe, MODULE_MSG_SIZE_MIN / 2);
	module_->route[0].f = tx_process;
	module_->route[0].pipe = &module_->module_pipe;
	module_->route[1].f = m_process;
	module_->route[1].pipe = NULL;
	pm_atomic_store(&module_->state, MODULE_PENDING_STARTED);
	return 0;
}

int
tarantool_stop_module(void *module, const char **error, int restart)
{
	struct module *module_ = (struct module *)module;
	int expected = MODULE_STARTED;
	if (pm_atomic_compare_exchange_strong(&module_->state, &expected, MODULE_STOPPED)) {
		module_->stop_fiber = fiber_new("stop", module_stop_f);
		if (!module_->stop_fiber) {
			*error = tt_sprintf("error: unable to create fiber to stop '%s' module", module_->name);
			return -1;
		}
		pm_atomic_store(&module_->restart, restart);
		fiber_start(module_->stop_fiber, module_);
	} else if (pm_atomic_load(&module_->state) == MODULE_PENDING_STARTED) { /* Case when we wait for tx thread in box.cfg{}*/
		if (restart)
			return 0;
		if (pm_atomic_load(&module_->soon_stoped) == 0)
			tt_pthread_cancel(module_->module_cord.id);
		cord_join(&module_->module_cord);
	}
	return 0;
}

void
tarantool_init_modules(void)
{
	avoid_optimization[0] = (void *)tarantool_register_module;
	avoid_optimization[1] = (void *)tarantool_start_module;
	avoid_optimization[2] = (void *)tarantool_stop_module;
}

void
tarantool_destroy_modules()
{
	struct module *module, *tmp;
	rlist_foreach_entry_safe(module, &module_list, link, tmp) {
		if (pm_atomic_load(&module->state) == MODULE_STARTED) {
			module->vtab->stop(module->ctx);
			cord_join(&module->module_cord);
		} else if (pm_atomic_load(&module->state) == MODULE_PENDING_STARTED) {
			if (pm_atomic_load(&module->soon_stoped) == 0)
				tt_pthread_cancel(module->module_cord.id);
			cord_join(&module->module_cord);
		} else if (pm_atomic_load(&module->state) == MODULE_STOPPED) {
			cord_join(&module->module_cord);
		}
		module->vtab->destroy(module->ctx);
		pthread_mutex_destroy(&module->mutex);
		pthread_cond_destroy(&module->cond);
		free(module->name);
		free(module);
	}
}

void
tarantool_tx_thread_ready(void)
{
	struct module *module;
	rlist_foreach_entry(module, &module_list, link) {
		pthread_mutex_lock(&module->mutex);
		pm_atomic_store(&module->mutex_owner, pthread_self());
		pthread_cond_broadcast(&module->cond);
		tx_ready = 1;
		pthread_mutex_unlock(&module->mutex);
		pm_atomic_store(&module->mutex_owner, 0);
	}
	/* In case no module started we not need mutex and condvar*/
	tx_ready = 1;
}