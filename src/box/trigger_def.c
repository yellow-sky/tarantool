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
#include "errcode.h"
#include "schema.h"
#include "space.h"
#include "tt_static.h"

const char *trigger_event_manipulation_strs[] = {"DELETE", "UPDATE", "INSERT"};

const char *trigger_action_timing_strs[] = {"BEFORE", "AFTER", "INSTEAD"};

const char *trigger_language_strs[] = {"SQL"};

const char *trigger_type_strs[] = {"REPLACE"};

struct trigger_def *
trigger_def_new(const char *name, uint32_t name_len, uint32_t space_id,
		enum trigger_language language,
		enum trigger_event_manipulation event_manipulation,
		enum trigger_action_timing action_timing, const char *code,
		uint32_t code_len)
{
	uint32_t code_offset;
	uint32_t trigger_def_sz = trigger_def_sizeof(name_len, code_len,
						     &code_offset);
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
	trigger_def->name_len = name_len;
	if (code_len != 0) {
		trigger_def->code = (char *) trigger_def + code_offset;
		memcpy(trigger_def->code, code, code_len);
		trigger_def->code[code_len] = '\0';
	} else {
		trigger_def->code = NULL;
	}
	return trigger_def;
}

void
trigger_def_delete(struct trigger_def *trigger_def)
{
	free(trigger_def);
}

int
trigger_def_check(struct trigger_def *def)
{
	struct space *space = space_by_id(def->space_id);
	if (space == NULL) {
		diag_set(ClientError, ER_CREATE_TRIGGER, def->name,
			 tt_sprintf("Space '%s' does not exist",
				    int2str(def->space_id)));
		return -1;
	}
	if (space_is_system(space)) {
		diag_set(ClientError, ER_CREATE_TRIGGER, def->name,
			 "cannot create trigger on system space");
		return -1;
	}
	switch (def->language) {
	case TRIGGER_LANGUAGE_SQL: {
		if (space->def->opts.is_view &&
		    def->action_timing != TRIGGER_ACTION_TIMING_INSTEAD) {
			const char *err_msg =
				tt_sprintf("cannot create %s trigger on "
					   "view: %s",
					   trigger_action_timing_strs[
						   def->action_timing],
					   space->def->name);
			diag_set(ClientError, ER_SQL_EXECUTE, err_msg);
			return -1;
		}
		if (!space->def->opts.is_view &&
		    def->action_timing == TRIGGER_ACTION_TIMING_INSTEAD) {
			diag_set(ClientError, ER_SQL_EXECUTE,
				tt_sprintf("cannot create INSTEAD OF trigger "
					   "on space: %s", space->def->name));
			return -1;
		}
		break;
	}
	default: {
		/*
		 * Only SQL triggers could define INSTEAD OF
		 * ACTION TIMING.
		 * */
		unreachable();
	}
	}
	return 0;
}
