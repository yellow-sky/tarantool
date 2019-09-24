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

#include <inttypes.h>

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

/**
 * The supported language of the stored function.
 */
enum trigger_language {
	TRIGGER_LANGUAGE_SQL,
	trigger_language_MAX,
};

extern const char *trigger_language_strs[];

/**
 * Trigger object definition.
 * See trigger_def_sizeof() definition for implementation
 * details and memory layout.
 */
struct trigger_def {
	/**
	 * The trigger event. This is the type of operation
	 * on the associated space for which the trigger
	 * activates. The value is `INSERT` (a row was inserted),
	 * `DELETE` (a row was deleted), or `UPDATE` (a row was
	 * modified).
	 */
	enum trigger_event_manipulation event_manipulation;
	/**
	 * Whether the trigger activates before or after the
	 * triggering event. The value is `BEFORE` or `AFTER`.
	 */
	enum trigger_action_timing action_timing;
	/** The ID of space the trigger refers to. */
	uint32_t space_id;
	/** The language of the stored trigger. */
	enum trigger_language language;
	/**
	 * The 0-terminated string, a name of the check
	 * constraint. Must be unique for a given space.
	 */
	char name[0];
};

/**
 * Calculate trigger definition memory size and fields
 * offsets for given arguments.
 *
 * Alongside with struct trigger_def itself, we reserve
 * memory for the name string.
 *
 * Memory layout:
 * +-----------------------------+ <- Allocated memory starts here
 * |      struct trigger_def     |
 * |-----------------------------|
 * |          name + \0          |
 * +-----------------------------+
 *
 * @param name_len The length of the name.
 * @return The size of the trigger definition object for
 *         given parameters.
 */
static inline uint32_t
trigger_def_sizeof(uint32_t name_len)
{
	return sizeof(struct trigger_def) + name_len + 1;
}

/**
 * Create a new trigger definition object with given fields.
 *
 * @param name The name string of a new trigger definition.
 * @param name_len The length of @a name string.
 * @param space_id The identifier of the target space.
 * @param language The language of the @a trigger object.
 * @param event_manipulation The type of operation on the
 *                           associated space for which the
 *                           trigger activates.
 * @param action_timing Whether the trigger activates before or
 *                      after the triggering event.
 * @retval not NULL Trigger definition object pointer on success.
 * @retval NULL Otherwise. The diag message is set.
*/
struct trigger_def *
trigger_def_new(const char *name, uint32_t name_len, uint32_t space_id,
		enum trigger_language language,
		enum trigger_event_manipulation event_manipulation,
		enum trigger_action_timing action_timing);

/**
 * Destroy trigger definition memory, release acquired resources.
 * @param trigger_def The trigger definition object to destroy.
 */
void
trigger_def_delete(struct trigger_def *trigger_def);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_BOX_TRIGGER_DEF_H */
