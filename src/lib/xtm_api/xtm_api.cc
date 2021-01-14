#include "xtm_api.h"
#include "xtm_scsp_queue.h"

#include <sys/eventfd.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>

#define XTM_PIPE_SIZE 4096

struct msg {
	/*
	 * Function for call
	 */
	void (*fun)(void *);
	/*
	 * Function argument
	 */
	void *fun_arg;
};

struct xtm_queue {
	/*
	 * Current element in queue.
	 * We notify reader only if current n_input value == 1
	 * This prevents unnecessary writing to pipe
	 */
	int n_input;
	/*
	 * Pipe file descriptors
	 */
	int filedes[2];
	/*
	 * Message queue, size must be power of 2
	 */
	struct xtm_scsp_queue<struct msg> *queue;
};

struct xtm_queue *
xtm_create(unsigned size)
{
	struct xtm_queue *queue = (struct xtm_queue *)
		malloc(sizeof(struct xtm_queue));
	if (queue == NULL)
		goto fail_alloc_queue;
	queue->queue = (struct xtm_scsp_queue<struct msg> *)
		malloc(sizeof(struct xtm_scsp_queue<struct msg>) +
		       size * sizeof(struct msg));
	if (queue->queue == NULL)
		goto fail_alloc_scsp_queue;
	if (pipe(queue->filedes) < 0)
		goto fail_alloc_fd;
	/*
	 * Make pipe read/write nonblock, and decrease
	 * pipe size to minimum (pipe size >= PAGE_SIZE)
	 */
	if (fcntl(queue->filedes[0], F_SETFL, O_NONBLOCK) < 0 ||
	    fcntl(queue->filedes[1], F_SETFL, O_NONBLOCK) < 0 ||
	    fcntl(queue->filedes[0], F_SETPIPE_SZ, XTM_PIPE_SIZE) < 0 ||
	    fcntl(queue->filedes[1], F_SETPIPE_SZ, XTM_PIPE_SIZE) < 0)
		goto fail_alloc_fd;
	if (xtm_scsp_queue_init(queue->queue, size) < 0) {
		errno = EINVAL;
		goto fail_scsp_queue_init;
	}
	queue->n_input = 0;
	return queue;

fail_scsp_queue_init:
	close(queue->filedes[0]);
	close(queue->filedes[1]);
fail_alloc_fd:
	free(queue->queue);
fail_alloc_scsp_queue:
	free(queue);
fail_alloc_queue:
	return NULL;
};

int
xtm_delete(struct xtm_queue *queue)
{
	int err = 0;
	if (close(queue->filedes[0]) < 0)
		err = errno;
	if (close(queue->filedes[1]) == 0)
		errno = err;
	free(queue->queue);
	free(queue);
	return (errno == 0 ? 0 : -1);
}

int
xtm_msg_probe(struct xtm_queue *queue)
{
	if (xtm_scsp_queue_free_count(queue->queue) == 0) {
		errno = ENOBUFS;
		return -1;
	}
	return 0;
}

int
xtm_msg_notify(struct xtm_queue *queue)
{
	uint64_t tmp = 0;
	ssize_t cnt;
	/*
	 * We must write 8 byte value, because later for linux we
	 * used eventfd, which require to write >= 8 byte at once.
	 * Also in case of EINTR we retry to write
	 */
	while ((cnt = write(queue->filedes[1], &tmp, sizeof(tmp))) < 0 && errno == EINTR)
		;
	return (cnt == sizeof(tmp) ? 0 : -1);
}

int
xtm_fun_dispatch(struct xtm_queue *queue, void (*fun)(void *),
		 void *fun_arg, int delayed)
{
	struct msg msg;
	msg.fun = fun;
	msg.fun_arg = fun_arg;
	if (xtm_scsp_queue_put(queue->queue, &msg, 1) != 1) {
		errno = ENOBUFS;
		return -1;
	}
	if (pm_atomic_fetch_add(&queue->n_input, 1) == 0 && delayed == 0)
		return xtm_msg_notify(queue);
	return 0;
}

int
xtm_fd(const struct xtm_queue *queue)
{
	return queue->filedes[0];
}

unsigned
xtm_fun_invoke(struct xtm_queue *queue)
{
	unsigned char tmp[XTM_PIPE_SIZE];
	ssize_t read_bytes;
	int save_errno = errno;
	while ((read_bytes = read(queue->filedes[0], tmp, sizeof(tmp))) < 0 && errno == EINTR)
		;
	assert(read_bytes > 0 || errno == EAGAIN);
	errno = save_errno;
	unsigned cnt = xtm_scsp_queue_execute(queue->queue);
	pm_atomic_fetch_sub(&queue->n_input, cnt);
	return cnt;
}
