#pragma once
/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
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
#include <pmatomic.h>

template<class T>
struct xtm_scsp_queue {
	/*
	 * Next position to be written
	 */
	unsigned write;
	/*
	 * Next position to be read
	 */
	unsigned read;
	/*
	 * Circular buffer length
	 */
	unsigned len;
	/*
	 * Buffer contains pointers
	 */
	T buffer[];
};

template <class T>
static inline int
xtm_scsp_queue_init(struct xtm_scsp_queue<T> *queue, unsigned size)
{
	/* Ensure size is power of 2 */
	if (size & (size - 1))
		return -1;

	queue->write = 0;
	queue->read = 0;
	queue->len = size;
	return 0;
}

/*
 * Adds num elements into the queue in case when we have one writing thread.
 * Return the number actually written.
 */
template <class T>
static inline unsigned
xtm_scsp_queue_put(struct xtm_scsp_queue<T> *queue, T *data, unsigned num)
{
	unsigned i = 0;
	unsigned queue_write = queue->write;
	unsigned new_write = queue_write;
	unsigned queue_read = pm_atomic_load_explicit(&queue->read, pm_memory_order_acquire);

	for (i = 0; i < num; i++) {
		new_write = (new_write + 1) & (queue->len - 1);
		if (new_write == queue_read)
			break;
		queue->buffer[queue_write] = data[i];
		queue_write = new_write;
	}
	pm_atomic_store_explicit(&queue->write, queue_write, pm_memory_order_release);
	return i;
}

/*
 * Get up to num elements from the queue. Return the number actually read
 */
template <class T>
static inline unsigned
xtm_scsp_queue_get(struct xtm_scsp_queue<T> *queue, T *data, unsigned num)
{
	unsigned i = 0;
	unsigned new_read = queue->read;
	unsigned queue_write = pm_atomic_load_explicit(&queue->write, pm_memory_order_acquire);

	for (i = 0; i < num; i++) {
		if (new_read == queue_write)
			break;
		data[i] = queue->buffer[new_read];
		new_read = (new_read + 1) & (queue->len - 1);
	}
	pm_atomic_store_explicit(&queue->read, new_read, pm_memory_order_release);
	return i;
}

/*
 * If T is a type that contains a function and a pointer to a function,
 * then this function for all queue elements of type T calls the functions
 * contained in the corresponding element with their arguments
 */
template <class T>
static inline unsigned
xtm_scsp_queue_execute(struct xtm_scsp_queue<T> *queue)
{
	unsigned i = 0;
	unsigned new_read = queue->read;
	unsigned queue_write = pm_atomic_load_explicit(&queue->write, pm_memory_order_acquire);

	for(;;) {
		if (new_read == queue_write)
			break;
		queue->buffer[new_read].fun(queue->buffer[new_read].fun_arg);
		new_read = (new_read + 1) & (queue->len - 1);
		i++;
	}
	pm_atomic_store_explicit(&queue->read, new_read, pm_memory_order_release);
	return i;
}

/*
 * Get the num of elements in the queue
 */
template <class T>
static inline unsigned
xtm_scsp_queue_count(struct xtm_scsp_queue<T> *queue)
{
	unsigned queue_write = pm_atomic_load_explicit(&queue->write, pm_memory_order_acquire);
	unsigned queue_read = pm_atomic_load_explicit(&queue->read, pm_memory_order_acquire);
	return (queue->len + queue_write - queue_read) & (queue->len - 1);
}

/*
 * Get the num of available elements in the queue
 */
template <class T>
static inline unsigned
xtm_scsp_queue_free_count(struct xtm_scsp_queue<T> *queue)
{
	unsigned queue_write = pm_atomic_load_explicit(&queue->write, pm_memory_order_acquire);
	unsigned queue_read = pm_atomic_load_explicit(&queue->read, pm_memory_order_acquire);
	return (queue_read - queue_write - 1) & (queue->len - 1);
}

