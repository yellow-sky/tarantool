/**
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
#include "trigger.h"

#include <stdbool.h>
#include "space.h"
#include "sql.h"
#include "trigger_def.h"
#include "trivia/util.h"

struct trigger *
trigger_new(struct trigger_def *def)
{
	struct trigger *trigger = NULL;
	switch (def->language) {
	case TRIGGER_LANGUAGE_SQL: {
		trigger = (struct trigger *) sql_trigger_new(def, NULL, false);
		break;
	}
	default: {
		unreachable();
	}
	}
	return trigger;
}

void
trigger_delete(struct trigger *base)
{
	struct trigger_def *def = base->def;
	base->vtab->destroy(base);
	trigger_def_delete(def);
}

struct trigger *
space_trigger_by_name(struct space *space, const char *name, uint32_t name_len)
{
	struct trigger *trigger = NULL;
	rlist_foreach_entry(trigger, &space->trigger_list, link) {
		if (trigger->def->name_len == name_len &&
		    memcmp(trigger->def->name, name, name_len) == 0)
			return trigger;
	}
	return NULL;
}
