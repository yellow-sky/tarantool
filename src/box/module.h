#ifndef INCLUDES_TARANTOOL_LUA_MODULE_H
#define INCLUDES_TARANTOOL_LUA_MODULE_H
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
#include "trivia/util.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

void
tarantool_init_modules(void);

void
tarantool_destroy_modules(void);

void
tarantool_tx_thread_ready(void);

/** \cond public */

struct tarantool_module_vtab {
	int  (*init)(void **ctx, void *module);
	void (*start)(void *ctx, void *loop, int loopfd);
	void (*stop)(void *ctx);
	void (*destroy)(void *ctx);
};

/* Prepare message with module request and send it to tx thread */
API_EXPORT void
tx_msg_send(void *m, void *data, void (*tx_process_request)(void *),
	void (*m_process_request)(void *, int));

/* Register new module in tarantool */
API_EXPORT int
tarantool_register_module(const char *name, struct tarantool_module_vtab *vtab, const char **error);

/* Start new module */
API_EXPORT int
tarantool_start_module(void *module, const char **error);

/* Stop module */
API_EXPORT int
tarantool_stop_module(void *module, const char **error, int restart);


/** \endcond public */

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_LUA_MODULE_H */