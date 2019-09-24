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
#include "trigger_def.h"
#include "diag.h"

const char *trigger_event_manipulation_strs[] = {"DELETE", "UPDATE", "INSERT"};

const char *trigger_action_timing_strs[] = {"BEFORE", "AFTER", "INSTEAD"};

const char *trigger_language_strs[] = {"SQL"};

struct trigger_def *
trigger_def_new(const char *name, uint32_t name_len, uint32_t space_id,
		enum trigger_language language,
		enum trigger_event_manipulation event_manipulation,
		enum trigger_action_timing action_timing)
{
	uint32_t trigger_def_sz = trigger_def_sizeof(name_len);
	struct trigger_def *trigger_def =
		(struct trigger_def *) malloc(trigger_def_sz);
	if (trigger_def == NULL) {
		diag_set(OutOfMemory, trigger_def_sz, "malloc", "trigger_def");
		return NULL;
	}
	trigger_def->space_id = space_id;
	trigger_def->language = language;
	trigger_def->event_manipulation = event_manipulation;
	trigger_def->action_timing = action_timing;
	memcpy(trigger_def->name, name, name_len);
	trigger_def->name[name_len] = '\0';
	return trigger_def;
}

void
trigger_def_delete(struct trigger_def *trigger_def)
{
	free(trigger_def);
}
