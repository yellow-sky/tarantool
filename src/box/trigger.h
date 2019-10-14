#ifndef INCLUDES_BOX_TRIGGER_H
#define INCLUDES_BOX_TRIGGER_H
/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#include <inttypes.h>
#include "small/rlist.h"

struct space;
struct trigger;
struct trigger_def;

/** Virtual method table for trigger object. */
struct trigger_vtab {
	/** Release implementation-specific trigger context. */
	void (*destroy)(struct trigger *func);
};

/**
 * Structure representing trigger.
 */
struct trigger {
	/** The trigger definition. */
	struct trigger_def *def;
	/** Virtual method table. */
	const struct trigger_vtab *vtab;
	/**
	 * Organize sql_trigger structs into linked list
	 * with space::trigger_list.
	 */
	struct rlist link;
};

struct trigger *
trigger_new(struct trigger_def *def);

void
trigger_delete(struct trigger *trigger);

/** Find trigger object in space by given name and name_len. */
struct trigger *
space_trigger_by_name(struct space *space, const char *name, uint32_t name_len);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_BOX_TRIGGER_H */
