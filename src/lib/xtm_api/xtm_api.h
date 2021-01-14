#pragma once

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** \cond public */

struct xtm_queue;

struct xtm_queue *
xtm_create(unsigned size);

int
xtm_delete(struct xtm_queue *queue);

int
xtm_msg_notify(struct xtm_queue *queue);

int
xtm_msg_probe(struct xtm_queue *queue);

int
xtm_fun_dispatch(struct xtm_queue *queue, void (*fun)(void *),
		 void *fun_arg, int delayed);

int
xtm_fd(const struct xtm_queue *queue);

unsigned
xtm_fun_invoke(struct xtm_queue *queue);

/** \endcond public */

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

