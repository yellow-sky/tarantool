#include "xtm_api.h"

#include <stdlib.h>
#include <small/rlist.h>
#include <trivia/util.h>
#include <fiber.h>
#include <libev/ev.h>

struct on_shutdown_trigger {
	/*
	 * rlist entry
	 */
	struct rlist entry;
	/*
	 * Shutdown handler arg
	 */
	void *arg;
	/*
	 * Shutdown handler
	 */
	void (*handler)(void *);
	/*
	 * Async event set when module main thread finished
	 */
	struct ev_async on_shutdown_async;
	/*
	 * Module shutdown fiber
	 */
	struct fiber *on_shutdown_fiber;
	/*
	 * Main loop
	 */
	struct ev_loop *loop;
};

/*
 * Shutdown trigger list, not need mutex, because access is
 * avaliable only from tx thread
 */
static RLIST_HEAD(on_shutdown_trigger_list);

static void
on_shutdown_fiber_wakeup(MAYBE_UNUSED struct ev_loop *loop, ev_async *ev, MAYBE_UNUSED int revents)
{
	struct on_shutdown_trigger *on_shutdown_trigger = ev->data;
	fiber_wakeup(on_shutdown_trigger->on_shutdown_fiber);
}

static int
create_new_on_shutdown_trigger(void *arg, void (*handler)(void *), void **opaque)
{
	struct on_shutdown_trigger *trigger = (struct on_shutdown_trigger *)
		malloc(sizeof(struct on_shutdown_trigger));
	if (trigger == NULL)
		return -1;
	trigger->arg = arg;
	trigger->handler = handler;
	ev_async_init(&trigger->on_shutdown_async, on_shutdown_fiber_wakeup);
	trigger->on_shutdown_async.data = trigger;
	*opaque = trigger;
	trigger->loop = loop();
	rlist_add_entry(&on_shutdown_trigger_list, trigger, entry);
	return 0;
}

int
on_shutdown_register(void *arg, void (*new_hadler)(void *), void (*old_handler)(void *),
	void **opaque)
{
	struct on_shutdown_trigger *on_shutdown_trigger, *tmp;
	if (old_handler == NULL)
		return create_new_on_shutdown_trigger(arg, new_hadler, opaque);

	bool is_on_shutdown_trigger_find = false;
	rlist_foreach_entry_safe(on_shutdown_trigger, &on_shutdown_trigger_list, entry, tmp) {
		if (on_shutdown_trigger->handler == old_handler) {
			if (new_hadler != NULL) {
				/*
				 * Change on_shutdown trigger handler, and arg
				 */
				on_shutdown_trigger->handler = new_hadler;
				on_shutdown_trigger->arg = arg;
			} else {
				/*
				 * In case new_handler == NULL
				 * Remove old on_shutdown trigger and destroy it
				 */
				*opaque = NULL;
				rlist_del_entry(on_shutdown_trigger, entry);
				free(on_shutdown_trigger);
			}
			is_on_shutdown_trigger_find = true;
		}
	}
	if (!is_on_shutdown_trigger_find && new_hadler) {
		/*
		 * If we not findn on_shutdown trigger but new handler is set
		 * create new on_shutdown trigger
		 */
		return create_new_on_shutdown_trigger(arg, new_hadler, opaque);
	}
	/*
	 * Return 0 (success) if on_shutdown trigger find, othrewise return -1
	 */
	return (is_on_shutdown_trigger_find ? 0 : -1);
}

static int
on_shutdown_f(va_list ap)
{
	struct on_shutdown_trigger *on_shutdown_trigger = va_arg(ap, struct on_shutdown_trigger *);
	on_shutdown_trigger->handler(on_shutdown_trigger->arg);
	return 0;
}

/*
 * Common function called all module shutdown triggers
 */
int
on_shutdown_trigger_common(MAYBE_UNUSED struct trigger *trigger, MAYBE_UNUSED void *event)
{
	struct on_shutdown_trigger *on_shutdown_trigger, *tmp;
	rlist_foreach_entry_safe(on_shutdown_trigger, &on_shutdown_trigger_list, entry, tmp) {
		on_shutdown_trigger->on_shutdown_fiber = fiber_new("shutdown", on_shutdown_f);
		if (on_shutdown_trigger->on_shutdown_fiber != NULL) {
			fiber_set_joinable(on_shutdown_trigger->on_shutdown_fiber, true);
			fiber_start(on_shutdown_trigger->on_shutdown_fiber, on_shutdown_trigger);
		} else {
			/*
			 * Unable to create fiber, wait here
			 */
			on_shutdown_trigger->handler(on_shutdown_trigger->arg);
			rlist_del_entry(on_shutdown_trigger, entry);
			free(on_shutdown_trigger);
		}
	}

	rlist_foreach_entry_safe(on_shutdown_trigger, &on_shutdown_trigger_list, entry, tmp) {
		fiber_join(on_shutdown_trigger->on_shutdown_fiber);
		rlist_del_entry(on_shutdown_trigger, entry);
		free(on_shutdown_trigger);
	}
	return 0;
}

void
on_shutdown_notify(void *opaque)
{
	struct on_shutdown_trigger *on_shutdown_trigger = opaque;
	if (on_shutdown_trigger && on_shutdown_trigger->on_shutdown_fiber)
		ev_async_send(on_shutdown_trigger->loop, &on_shutdown_trigger->on_shutdown_async);
}

void
on_shutdown_wait(void *opaque)
{
	struct on_shutdown_trigger *on_shutdown_trigger = opaque;
	/*
	 * In case this function called in special fiber we wait
	 * otherwise do nothing
	 */
	if (on_shutdown_trigger && on_shutdown_trigger->on_shutdown_fiber) {
		ev_async_start(on_shutdown_trigger->loop, &on_shutdown_trigger->on_shutdown_async);
		bool cancellable = fiber_set_cancellable(false);
		fiber_yield();
		fiber_set_cancellable(cancellable);
		ev_async_stop(on_shutdown_trigger->loop, &on_shutdown_trigger->on_shutdown_async);
	}
}

