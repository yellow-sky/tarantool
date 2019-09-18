#ifndef INCLUDES_BOX_TRIGGER_DEF_H
#define INCLUDES_BOX_TRIGGER_DEF_H
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

/**
 * The trigger event. This is the type of operation
 * on the associated space for which the trigger
 * activates. The value is `DELETE` (a row was deleted), or
 * `UPDATE` (a row was modified), `INSERT` (a row was inserted).
 */
enum trigger_event_manipulation {
	TRIGGER_EVENT_MANIPULATION_DELETE,
	TRIGGER_EVENT_MANIPULATION_UPDATE,
	TRIGGER_EVENT_MANIPULATION_INSERT,
	trigger_event_manipulation_MAX
};

extern const char *trigger_event_manipulation_strs[];

/**
 * Whether the trigger activates before or after the triggering
 * event. The value is `BEFORE` or `AFTER`.
 */
enum trigger_action_timing {
	TRIGGER_ACTION_TIMING_BEFORE,
	TRIGGER_ACTION_TIMING_AFTER,
	/*
	 * INSTEAD of triggers are only for views and
	 * views only support INSTEAD of triggers.
	 */
	TRIGGER_ACTION_TIMING_INSTEAD,
	trigger_action_timing_MAX
};

extern const char *trigger_action_timing_strs[];

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_BOX_TRIGGER_DEF_H */
