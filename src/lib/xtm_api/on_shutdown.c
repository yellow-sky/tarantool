#include "xtm_api.h"

#include <trigger.h>
#include <stdlib.h>
#include <small/rlist.h>
#include <box/box.h>
#include <pthread.h>
#include <trivia/util.h>

struct on_shutdown_trigger {
	/*
	 * rlist entry
	 */
	struct rlist entry;
	/*
	 * trigger for box_on_shutdown
	 */
	struct trigger trigger;
	/*
	 * Shutdown handler arg
	 */
	void *arg;
	/*
	 * Shutdown handler
	 */
	void (*handler)(void *);
};

/*
 * Shutdown trigger list, not need mutex, because access is
 * avaliable only from tx thread
 */
static RLIST_HEAD(shutdown_trigger_list);

/*
 * Common function for shutdown triggers
 */
static int
on_shutdown_trigger_common(struct trigger *trigger, MAYBE_UNUSED void *event)
{
	struct on_shutdown_trigger *shutdown_trigger = trigger->data;
	shutdown_trigger->handler(shutdown_trigger->arg);
	rlist_del_entry(shutdown_trigger, entry);
	free(shutdown_trigger);
	return 0;
}

static int
create_new_shutdown_trigger(void *arg, void (*handler)(void *))
{
	struct on_shutdown_trigger *trigger = (struct on_shutdown_trigger *)
		malloc(sizeof(struct on_shutdown_trigger));
	if (trigger == NULL)
		return -1;
	trigger->arg = arg;
	trigger->handler = handler;
	trigger_create(&trigger->trigger, on_shutdown_trigger_common, trigger, NULL);
	trigger_add(&box_on_shutdown, &trigger->trigger);
	rlist_add_entry(&shutdown_trigger_list, trigger, entry);
	return 0;
}

int
on_shutdown(void *arg, void (*new_hadler)(void *), void (*old_handler)(void *))
{
	struct on_shutdown_trigger *shutdown_trigger, *tmp;
	if (old_handler == NULL)
		return create_new_shutdown_trigger(arg, new_hadler);

	bool is_shutdown_find = false;
	rlist_foreach_entry_safe(shutdown_trigger, &shutdown_trigger_list, entry, tmp) {
		if (shutdown_trigger->handler == old_handler) {
			if (new_hadler != NULL) {
				/*
				 * Change shutdown trigger handler, and arg
				 */
				shutdown_trigger->handler = new_hadler;
				shutdown_trigger->arg = arg;
			} else {
				/*
				 * In case new_handler == NULL
				 * Remove old shutdown trigger and destroy it
				 */
				rlist_del_entry(shutdown_trigger, entry);
				trigger_clear(&shutdown_trigger->trigger);
				free(shutdown_trigger);
			}
			is_shutdown_find = true;
		}
	}
	if (!is_shutdown_find && new_hadler) {
		/*
		 * If we not find shutdown trigger but new handler is set
		 * create new shutdown trigger
		 */
		return create_new_shutdown_trigger(arg, new_hadler);
	}
	/*
	 * Return 0 (success) if shutdown trigger find, othrewise return -1
	 */
	return (is_shutdown_find ? 0 : -1);
}

