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
#include "swim_test_ev.h"
#include "trivia/util.h"
#include "swim/swim_ev.h"
#include "tarantool_ev.h"
#define HEAP_FORWARD_DECLARATION
#include "salad/heap.h"
#include "assoc.h"
#include "say.h"
#include <stdbool.h>

/** Global watch, propagated by new events. */
static double watch = 0;

/**
 * Increasing event identifiers are used to preserve order of
 * events with the same deadline.
 */
static int event_id = 0;

/**
 * Fake event loop has two event types - natural libev events like timer, and
 * artificial like fake socket blocking.
 */
enum fakeev_event_type {
	FAKEEV_EVENT_TIMER,
	FAKEEV_EVENT_BRK,
};

struct fakeev_event;

typedef void (*fakeev_event_process_f)(struct fakeev_event *, struct ev_loop *);
typedef void (*fakeev_event_delete_f)(struct fakeev_event *);

/**
 * An isolated event loop not visible to the fiber scheduler,
 * where it is safe to use fake file descriptors, manually invoke
 * callbacks etc.
 */
static struct ev_loop *test_loop;

struct ev_loop *
fakeev_loop(void)
{
	return test_loop;
}

/**
 * Base event. It is stored in the event heap and virtualizes
 * other events.
 */
struct fakeev_event {
	/** Type, for assertions only. */
	enum fakeev_event_type type;
	/**
	 * When that event should be invoked according to the fake
	 * watch.
	 */
	double deadline;
	/** A link in the event heap. */
	struct heap_node in_event_heap;
	/** ID to sort events with the same deadline. */
	int id;
	/**
	 * Process the event. Usually the event is deleted right
	 * after that.
	 */
	fakeev_event_process_f process;
	/** Just delete the event. Called on event heap reset. */
	fakeev_event_delete_f delete;
};

/**
 * Heap comparator. Heap's top stores an event with the nearest
 * deadline and the smallest ID in that deadline.
 */
static inline bool
fakeev_event_less(const struct fakeev_event *e1, const struct fakeev_event *e2)
{
	if (e1->deadline == e2->deadline)
		return e1->id < e2->id;
	return e1->deadline < e2->deadline;
}

#define HEAP_NAME event_heap
#define HEAP_LESS(h, e1, e2) fakeev_event_less(e1, e2)
#define heap_value_t struct fakeev_event
#define heap_value_attr in_event_heap
#include "salad/heap.h"

/** Event heap. Event loop pops them from here. */
static heap_t event_heap;

/** Libev watcher is matched to exactly one event here. */
static struct mh_i64ptr_t *events_hash;

/**
 * Create a new event which should call @a process after @a delay
 * fake seconds. @A delete is called to free the event when it is
 * done, and when the event heap is reset.
 */
static void
fakeev_event_create(struct fakeev_event *e, enum fakeev_event_type type,
		    double delay, fakeev_event_process_f process,
		    fakeev_event_delete_f delete)
{
	e->deadline = fakeev_time() + delay;
	e->id = event_id++;
	e->process = process;
	e->delete = delete;
	e->type = type;
	event_heap_insert(&event_heap, e);
}

/** Destroy a basic event. */
static inline void
fakeev_event_destroy(struct fakeev_event *e)
{
	event_heap_delete(&event_heap, e);
}

/** Destroy a event and free its resources. */
static inline void
fakeev_event_delete(struct fakeev_event *e)
{
	e->delete(e);
}

/** Find an event by @a watcher. */
static struct fakeev_event *
fakeev_event_by_ev(struct ev_watcher *watcher)
{
	mh_int_t rc = mh_i64ptr_find(events_hash, (uint64_t) watcher, NULL);
	if (rc == mh_end(events_hash))
		return NULL;
	return mh_i64ptr_node(events_hash, rc)->val;
}

/** Timer event generated by libev. */
struct fakeev_timer_event {
	struct fakeev_event base;
	/**
	 * Libev watcher. Used to store callback and to find the
	 * event by watcher pointer. It is necessary because real code
	 * operates by libev watchers.
	 */
	struct ev_watcher *watcher;
};

/** Destroy a timer event and free its resources. */
static void
fakeev_timer_event_delete(struct fakeev_event *e)
{
	assert(e->type == FAKEEV_EVENT_TIMER);
	struct fakeev_timer_event *te = (struct fakeev_timer_event *)e;
	mh_int_t rc = mh_i64ptr_find(events_hash, (uint64_t) te->watcher, NULL);
	assert(rc != mh_end(events_hash));
	mh_i64ptr_del(events_hash, rc, NULL);
	fakeev_event_destroy(e);
	free(te);
}

/** Create a new timer event. */
static void
fakeev_timer_event_new(struct ev_watcher *watcher, double delay);

/** Process a timer event and delete it. */
static void
fakeev_timer_event_process(struct fakeev_event *e, struct ev_loop *loop)
{
	assert(e->type == FAKEEV_EVENT_TIMER);
	struct ev_watcher *w = ((struct fakeev_timer_event *)e)->watcher;
	struct ev_timer *t = (struct ev_timer *) w;
	fakeev_timer_event_delete(e);
	t->at = 0;
	if (t->repeat > 0)
		fakeev_timer_event_new(w, t->repeat);
	ev_invoke(loop, w, EV_TIMER);
}

static void
fakeev_timer_event_new(struct ev_watcher *watcher, double delay)
{
	struct fakeev_timer_event *e = malloc(sizeof(*e));
	assert(e != NULL);
	fakeev_event_create(&e->base, FAKEEV_EVENT_TIMER, delay,
			    fakeev_timer_event_process,
			    fakeev_timer_event_delete);
	e->watcher = watcher;
	assert(fakeev_event_by_ev(watcher) == NULL);
	struct mh_i64ptr_node_t node = {(uint64_t) watcher, e};
	mh_int_t rc = mh_i64ptr_put(events_hash, &node, NULL, NULL);
	(void) rc;
	assert(rc != mh_end(events_hash));
}

/**
 * Breakpoint event for debug. It does nothing but stops the event
 * loop after a timeout to allow highlevel API to check some
 * cases. The main feature is that a test can choose that timeout,
 * while natural events usually are out of control. That
 * events allows to check conditions between natural events.
 */
struct fakeev_brk_event {
	struct fakeev_event base;
};

/** Delete a breakpoint event. */
static void
fakeev_brk_event_delete(struct fakeev_event *e)
{
	assert(e->type == FAKEEV_EVENT_BRK);
	fakeev_event_destroy(e);
	free(e);
}

/**
 * Breakpoint event processing is nothing but the event deletion.
 */
static void
fakeev_brk_event_process(struct fakeev_event *e, struct ev_loop *loop)
{
	(void) loop;
	assert(e->type == FAKEEV_EVENT_BRK);
	fakeev_brk_event_delete(e);
}

void
fakeev_set_brk(double delay)
{
	struct fakeev_brk_event *e = malloc(sizeof(*e));
	assert(e != NULL);
	fakeev_event_create(&e->base, FAKEEV_EVENT_BRK, delay,
			    fakeev_brk_event_process, fakeev_brk_event_delete);
}

double
fakeev_time(void)
{
	return watch;
}

/**
 * Start of a timer generates a delayed event. If a timer is
 * already started - nothing happens.
 */
void
fakeev_timer_start(struct ev_loop *loop, struct ev_timer *base)
{
	(void) loop;
	if (fakeev_event_by_ev((struct ev_watcher *)base) != NULL)
		return;
	/* Create the periodic watcher and one event. */
	fakeev_timer_event_new((struct ev_watcher *)base, base->at);
}

void
fakeev_timer_again(struct ev_loop *loop, struct ev_timer *base)
{
	(void) loop;
	if (fakeev_event_by_ev((struct ev_watcher *)base) != NULL)
		return;
	/* Create the periodic watcher and one event. */
	fakeev_timer_event_new((struct ev_watcher *)base, base->repeat);
}

/** Time stop cancels the event if the timer is active. */
void
fakeev_timer_stop(struct ev_loop *loop, struct ev_timer *base)
{
	(void) loop;
	/*
	 * Delete the watcher and its events. Should be only one.
	 */
	struct fakeev_event *e = fakeev_event_by_ev((struct ev_watcher *)base);
	if (e == NULL)
		return;
	fakeev_event_delete(e);
}

/** Process all the events with the next nearest deadline. */
void
fakeev_loop_update(struct ev_loop *loop)
{
	struct fakeev_event *e = event_heap_top(&event_heap);
	if (e != NULL) {
		assert(e->deadline >= watch);
		/* Multiple events can have the same deadline. */
		watch = e->deadline;
		say_verbose("Loop watch %f", watch);
		do {
			e->process(e, loop);
			e = event_heap_top(&event_heap);
		} while (e != NULL && e->deadline == watch);
	}
}

void
fakeev_reset(void)
{
	struct fakeev_event *e;
	while ((e = event_heap_top(&event_heap)) != NULL)
		fakeev_event_delete(e);
	assert(mh_size(events_hash) == 0);
	event_id = 0;
	watch = 0;
}

void
fakeev_init(void)
{
	events_hash = mh_i64ptr_new();
	assert(events_hash != NULL);
	event_heap_create(&event_heap);
	test_loop = ev_loop_new(0);
	assert(test_loop != NULL);
}

void
fakeev_free(void)
{
	fakeev_reset();
	event_heap_destroy(&event_heap);
	mh_i64ptr_delete(events_hash);
	ev_loop_destroy(test_loop);
}

double
swim_time(void)
{
	return fakeev_time();
}

void
swim_ev_timer_start(struct ev_loop *loop, struct ev_timer *watcher)
{
	return fakeev_timer_start(loop, watcher);
}

void
swim_ev_timer_again(struct ev_loop *loop, struct ev_timer *watcher)
{
	return fakeev_timer_again(loop, watcher);
}

void
swim_ev_timer_stop(struct ev_loop *loop, struct ev_timer *watcher)
{
	return fakeev_timer_stop(loop, watcher);
}

struct ev_loop *
swim_loop(void)
{
	return fakeev_loop();
}
