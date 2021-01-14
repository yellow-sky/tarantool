#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <luaconf.h>

#include <pthread.h>
#include <module.h>
#include <uv.h>
#include <stdlib.h>

#define XTM_MODULE_SIZE 16

struct module {
	/*
	 * Module thread id
	 */
	pthread_t thread;
	/*
	 * Tx thread id, need only for test purpose
	 */
	pthread_t tx_thread;
	/*
	 * Tarantool lua state, not used in this simple sample
	 */
	lua_State *L;
	/*
	 * Message queue from tx thread to module thread,
	 * in other words, tx thread puts messages in this queue,
	 * and module thread reads and executes them
	 */
	struct xtm_queue *in;
	/*
	 * Message queue from module thread to tx thread,
	 * in other words, module thread puts messages in this queue,
	 * and tx thread reads and executes them
	 */
	struct xtm_queue *out;
	/*
	 * Flag of the module state, May be -1, 0, 1
	 * -1 means that module thread failed to start
	 * 0 means that module currently stop
	 * 1 menas that module currently running
	 */
	int is_running;
	/*
	 * Pointer to module thread event loop
	 */
	uv_loop_t *uv_loop;
	/*
	 * Fiber in tx thread, which read and execute module msg
	 */
	struct fiber *tx_fiber;
};

/*
 * Simple module msg
 */
struct sample_module_msg {
	/*
	 * Thread id of sender thread
	 */
	pthread_t self;
	/*
	 * Msg counter
	 */
	unsigned long long counter;
};

static struct module module;

/*
 * Function pass from tx thread to xtm_fun_dispatch
 * Called in module thread
 */
static void
tx_msg_func(void *arg)
{
	struct sample_module_msg *msg = (struct sample_module_msg *)arg;
	/*
	 * Msg from tx thread and function called in module thread context
	 * Also here you can print msg and make sure of this
	 */
	assert(msg->self == module.tx_thread && pthread_self() == module.thread);
	free(msg);
}

/*
 * Function pass from module thread to xtm_fun_dispatch
 * Called in tx thread
 */
static void
module_msg_func(void *arg)
{
	static unsigned long long counter;
	struct sample_module_msg *msg = (struct sample_module_msg *)arg;
	/*
	 * Msg from module thread and function called in tx thread context
	 * Also here you can print msg and make sure of this
	 */
	assert(msg->self == module.thread && pthread_self() == module.tx_thread);
	msg->self = pthread_self();
	msg->counter = counter++;
	xtm_fun_dispatch(module.in, tx_msg_func, msg, 0);
}

/*
 * Function to stop module thread
 */
static void
module_thread_stop(void)
{
	uv_stop(module.uv_loop);
	pthread_join(module.thread, NULL);
	__atomic_store_n(&module.is_running, 0, __ATOMIC_SEQ_CST);
}

/*
 * Timer function, called in module thread
 * Allocate msg and send it to tx thread
 */
static void
uv_enqueue_message(uv_timer_t* handle)
{
	static unsigned long long counter;
	if (module.out == NULL)
		return;

	struct sample_module_msg *msg = (struct sample_module_msg *)
		malloc(sizeof(struct sample_module_msg));
	if (msg == NULL)
		return;
	msg->self = pthread_self();
	assert(msg->self == module.thread);
	msg->counter = counter++;
	xtm_fun_dispatch(module.out, module_msg_func, msg, 0);
}

static void
alloc_buffer_cb(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
	buf->base = (char*)malloc(suggested_size);
	buf->len = suggested_size;
}

/*
 * Function, which read module pipe of queue from tx thread to module thread
 * Read and execute msg from tx to module thread
 */
static void
uv_read_pipe(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf)
{
	if (nread > 0) {
		struct xtm_queue *queue = (struct xtm_queue *)((uv_handle_t *)client)->data;
		while (xtm_fun_invoke(queue))
			;
	}
	if (buf->base)
		free(buf->base);
}

/*
 * Tx fiber function, received msg from module
 * Wait pipe of queue from module to tx
 * Read and execute msg from module to tx thread
 */
static int
tx_fiber_func(va_list arg)
{
	module.out = xtm_create(XTM_MODULE_SIZE);
	if (module.out == NULL) {
		module_thread_stop();
		return 0;
	}
	int pipe_fd = xtm_fd(module.out);
	while(__atomic_load_n(&module.is_running, __ATOMIC_SEQ_CST) == 1) {
		if (coio_wait(pipe_fd, COIO_READ, ~0) & COIO_READ) {
			while (xtm_fun_invoke(module.out))
			;
		}
	}
	xtm_delete(module.out);
	module.out = NULL;
	return 0;
}

/*
 * Main module thread function.
 */
static void *
main_module_func(void *arg)
{
	uv_loop_t uv_loop;
	uv_pipe_t pipe;
	int pipe_fd;
	uv_timer_t timer;

	module.in = xtm_create(XTM_MODULE_SIZE);
	if (module.in == NULL) {
		__atomic_store_n(&module.is_running, -1, __ATOMIC_SEQ_CST);
		goto finish;
	}
	pipe_fd = xtm_fd(module.in);
	module.uv_loop = &uv_loop;
	uv_loop_init(&uv_loop);
	uv_pipe_init(&uv_loop, &pipe, 0);
	uv_pipe_open(&pipe, pipe_fd);
	((uv_handle_t *)&pipe)->data = module.in;
	uv_read_start((uv_stream_t*)&pipe, alloc_buffer_cb, uv_read_pipe);
	uv_timer_init(&uv_loop, &timer);
	uv_timer_start(&timer, uv_enqueue_message, 0, 1000);
	__atomic_store_n(&module.is_running, 1, __ATOMIC_SEQ_CST);
	uv_run(&uv_loop, UV_RUN_DEFAULT);
	uv_timer_stop(&timer);
	uv_loop_close(&uv_loop);
	xtm_delete(module.in);
	module.in = NULL;
finish:
	return (void *)NULL;
}

static int
stop(lua_State *L)
{
	if (__atomic_load_n(&module.is_running, __ATOMIC_SEQ_CST) == 1) {
		module_thread_stop();
		fiber_join(module.tx_fiber);
	}
	return 0;
}

static int
cfg(lua_State *L)
{
	module.L = L;
	module.tx_thread = pthread_self();
	/*
	 * In case module already running, stop it
	 */
	if (__atomic_load_n(&module.is_running, __ATOMIC_SEQ_CST) == 1) {
		module_thread_stop();
		fiber_join(module.tx_fiber);
	}
	if (pthread_create(&module.thread, NULL, main_module_func, NULL) < 0) {
		lua_pushfstring(L, "error: failed to create module thread");
		lua_error(L);
	}
	/*
	 * Wait until module thread main function start event loop or failed
	 */
	while (__atomic_load_n(&module.is_running, __ATOMIC_SEQ_CST) == 0)
		;
	/*
	 * In case it failed join too module thread, set module
	 * state to 0 (no running) and print error
	 */
	if (__atomic_load_n(&module.is_running, __ATOMIC_SEQ_CST) == -1) {
		pthread_join(module.thread, NULL);
		__atomic_store_n(&module.is_running, 0, __ATOMIC_SEQ_CST);
		lua_pushfstring(L, "error: error in module thread");
		lua_error(L);
	}
	module.tx_fiber = fiber_new("tx_fiber", tx_fiber_func);
	if (module.tx_fiber == NULL) {
		/*
		 * In case we can't create fiber, stop module thread
		 * and print error
		 */
		module_thread_stop();
		lua_pushfstring(L, "error: failed to create tx fiber");
		lua_error(L);
	}
	fiber_set_joinable(module.tx_fiber, true);
	fiber_start(module.tx_fiber);
	return 0;
}

static const struct luaL_Reg example_lib[] = {
	{"cfg", cfg},
	{"stop", stop},
	{NULL, NULL}
};

LUALIB_API int
luaopen_example(lua_State *L)
{
	luaL_openlib(L, "example", example_lib, 0);
	return 0;
}