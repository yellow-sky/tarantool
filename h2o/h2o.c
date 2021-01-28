#define H2O_USE_EPOLL 1 /* FIXME */

#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <float.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <h2o.h>
#include <h2o/http1.h>
#include <h2o/http2.h>
#include <h2o/memcached.h>
#include <h2o/socket/evloop.h>

#include <lua.h>
#include <lauxlib.h>
#include <fiber.h>
#include <coio.h>
#include <box/box.h>
//#include <module.h> //Problematic
#include <msgpuck/msgpuck.h>

#include "../../xtm/xtm_api.h"

/* FIXME: Stopgaps until we solve include problems */
#define BOX_ID_NIL 2147483647
extern int
box_index_get(uint32_t space_id, uint32_t index_id, const char *key,
	      const char *key_end, box_tuple_t **result);
extern const char *
box_tuple_field(box_tuple_t *tuple, uint32_t fieldno);
/* End stopgaps */

#ifdef TCP_FASTOPEN
#define H2O_DEFAULT_LENGTH_TCP_FASTOPEN_QUEUE 4096
#else
#define H2O_DEFAULT_LENGTH_TCP_FASTOPEN_QUEUE 0
#endif /* TCP_FASTOPEN */

/* Failing HTTP requests is fine, but failing to respond from TX thread
 * is not so queue size must be larger */
#define QUEUE_TO_TX_ITEMS (1 << 12) /* Must be power of 2 */
#define QUEUE_FROM_TX_ITEMS (QUEUE_TO_TX_ITEMS << 1) /* Must be power of 2 */

#define USE_HTTPS 1
//#define USE_HTTPS 0

#define H2O_DEFAULT_PORT_FOR_PROTOCOL_USED 65535

struct thread_ctx;
typedef struct {
	h2o_accept_ctx_t accept_ctx;
	h2o_socket_t *sock;
	struct thread_ctx *thread_ctx;
	int fd;
} listener_ctx_t;

typedef struct thread_ctx {
	h2o_context_t ctx;
	listener_ctx_t *listener_ctxs;
	struct xtm_queue *queue_to_tx;
	struct xtm_queue *queue_from_tx;
	h2o_socket_t *sock_from_tx;
	unsigned num_connections;
	unsigned idx;
	pthread_t tid;
} thread_ctx_t;

static struct {
	uint32_t space_id;
	uint32_t index_id;
} our_userdata; /* Site/path-specific */

typedef struct {
	int fd;
} listener_cfg_t;

static struct {
	h2o_globalconf_t globalconf;
	listener_cfg_t *listener_cfgs;
	thread_ctx_t *thread_ctxs;
	struct fiber **tx_fiber_ptrs;
	SSL_CTX *ssl_ctx;
	unsigned num_listeners;
	unsigned num_accepts;
        unsigned max_conn_per_thread;
	unsigned num_threads;
	int tfo_queues;
	volatile bool tx_fiber_should_work;
} conf = {
	.tfo_queues = H2O_DEFAULT_LENGTH_TCP_FASTOPEN_QUEUE,
	.num_threads = 4, /* Stub */
};

static __thread thread_ctx_t *curr_thread_ctx;

static h2o_pathconf_t *register_handler(h2o_hostconf_t *hostconf, const char *path, int (*on_req)(h2o_handler_t *, h2o_req_t *))
{
	/* These functions never return NULL, dying instead */
	h2o_pathconf_t *pathconf = h2o_config_register_path(hostconf, path, 0);
	h2o_handler_t *handler = h2o_create_handler(pathconf, sizeof(*handler));
	handler->on_req = on_req;
	return pathconf;
}

struct anchor;
#define SHUTTLE_SIZE 256
#define SHUTTLE_PAYLOAD_SIZE (SHUTTLE_SIZE - 3 * sizeof(void *))
typedef struct {
	char payload[SHUTTLE_PAYLOAD_SIZE];
	h2o_req_t *never_access_this_req_from_tx_thread;
	struct anchor *anchor;
	uint16_t thread_idx;
	bool disposed; /* never_access_this_req_from_tx_thread can only be used if disposed is false */
	char unused[sizeof(void *) - sizeof(uint16_t) - sizeof(bool)];
} shuttle_t;

typedef struct anchor {
	shuttle_t *shuttle;
	bool should_free_shuttle;
} anchor_t;

static inline shuttle_t *alloc_shuttle(thread_ctx_t *thread_ctx)
{
	/* FIXME: Use per-thread pools */
	(void)thread_ctx;
	shuttle_t *shuttle = (shuttle_t *)malloc(sizeof(shuttle_t));
	if (shuttle == NULL)
		h2o_fatal("no memory");
	return shuttle;
}

static inline void free_shuttle(shuttle_t *shuttle, thread_ctx_t *thread_ctx)
{
	(void)thread_ctx;
	free(shuttle);
}

static void anchor_dispose(void *param)
{
	static_assert(sizeof(shuttle_t) == SHUTTLE_SIZE);
	anchor_t *const anchor = param;
	shuttle_t *const shuttle = anchor->shuttle;
	if (anchor->should_free_shuttle)
		free_shuttle(shuttle, &conf.thread_ctxs[shuttle->thread_idx]);
	else
		shuttle->disposed = true;

	//x x x;//should notify tx thread IF it is waiting for proceedSending() which would not happen
	/* probably should implemented support for "stubborn" anchors - 
	optionally wait for tx processing to finish so TX thread can access h2o_req_t directly
	thus avoiding copying LARGE buffers, it only makes sense
	in very specific cases because it stalls the whole thread if such
	request is gone */
}

#pragma pack(push, 1)
typedef struct {
	unsigned len;
	char data[SHUTTLE_PAYLOAD_SIZE - sizeof(unsigned)];
} simple_response_t;
#pragma pack(pop)

/* Launched in HTTP server thread */
static void post_process_req(shuttle_t *shuttle)
{
	if (shuttle->disposed) {
		free_shuttle(shuttle, &conf.thread_ctxs[shuttle->thread_idx]);
		return;
	}
	h2o_req_t *req = shuttle->never_access_this_req_from_tx_thread;
	static h2o_generator_t generator = {NULL, NULL};
	req->res.status = 200;
	req->res.reason = "OK";
	h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CONTENT_TYPE, NULL, H2O_STRLIT("text/plain; charset=utf-8"));
	h2o_start_response(req, &generator);

	h2o_iovec_t buf;
	simple_response_t *const response = (simple_response_t *)(&shuttle->payload);
	buf.base = response->data;
	buf.len = response->len;
	shuttle->anchor->should_free_shuttle = true;
	h2o_send(req, &buf, 1, H2O_SEND_STATE_FINAL);
}

/* Launched in TX thread; note that queue_from_tx is not created yet */
static bool init_userdata_in_tx(void)
{
	static const char space_name[] = "tester";
	if ((our_userdata.space_id = box_space_id_by_name(space_name, sizeof(space_name) - 1)) == BOX_ID_NIL)
		return false;
	static const char index_name[] = "primary";
	if ((our_userdata.index_id = box_index_id_by_name(our_userdata.space_id, index_name, sizeof(index_name) - 1)) == BOX_ID_NIL)
		return false;

	return true;
}

/* Launched in TX thread */
static void process_req_in_tx(shuttle_t *shuttle)
{
	static_assert(sizeof(simple_response_t) <= SHUTTLE_PAYLOAD_SIZE);
	simple_response_t *const response = (simple_response_t *)&shuttle->payload;

	const unsigned entry_index = 2; //FIXME: extract it from HTTP request
	char entry_index_msgpack[16];
	char *key_end = mp_encode_array(entry_index_msgpack, 1);
	key_end = mp_encode_uint(key_end, entry_index);
	assert(key_end < &entry_index_msgpack[0] + sizeof(entry_index_msgpack));
	box_tuple_t *tuple;
	if (box_index_get(our_userdata.space_id, our_userdata.index_id, entry_index_msgpack, key_end, &tuple)) {
		static const char error_str[] = "Query error";
		memcpy(&response->data, error_str, sizeof(error_str) - 1);
		response->len = sizeof(error_str) - 1;
	} else if (tuple == NULL) {
		static const char error_str[] = "Entry not found";
		memcpy(&response->data, error_str, sizeof(error_str) - 1);
		response->len = sizeof(error_str) - 1;
	} else {
		const char *name_msgpack = box_tuple_field(tuple, 1);
		if (name_msgpack == NULL) {
			static const char error_str[] = "Invalid entry format";
			memcpy(&response->data, error_str, sizeof(error_str) - 1);
			response->len = sizeof(error_str) - 1;
		} else {
			uint32_t len;
			const char *const name = mp_decode_str(&name_msgpack, &len);
			if (len > sizeof(response->data)) {
				/* Real implementation should probably report error
				 * or use complex response logic to allocate and pass large buffers
				 * to send via HTTP(S) */
				len = sizeof(response->data);
			}
			memcpy(&response->data, name, len);
			response->len = len;
		}
	}

	thread_ctx_t *const thread_ctx = &conf.thread_ctxs[shuttle->thread_idx];
	struct xtm_queue *const queue_from_tx = thread_ctx->queue_from_tx;
	while (xtm_fun_dispatch(queue_from_tx, (void(*)(void *))&post_process_req, shuttle, 0)) {
		/* Error; we must not fail so retry a little later */
		fiber_sleep(0);
	}
}

static int req_handler(h2o_handler_t *self, h2o_req_t *req)
{
	(void)self;
	if (!(h2o_memis(req->method.base, req->method.len, H2O_STRLIT("GET")) &&
	    h2o_memis(req->path_normalized.base, req->path_normalized.len, H2O_STRLIT("/"))))
		return -1;

	anchor_t *anchor = h2o_mem_alloc_shared(&req->pool, sizeof(anchor_t), &anchor_dispose);
	anchor->should_free_shuttle = false;
	thread_ctx_t *thread_ctx = curr_thread_ctx;
	shuttle_t *shuttle = alloc_shuttle(thread_ctx);
	shuttle->anchor = anchor;
	anchor->shuttle = shuttle;
	shuttle->never_access_this_req_from_tx_thread = req;
	shuttle->thread_idx = thread_ctx->idx;
	shuttle->disposed = false;

	if (xtm_fun_dispatch(thread_ctx->queue_to_tx, (void(*)(void *))&process_req_in_tx, shuttle, 0)) {
		/* Error */
		free_shuttle(shuttle, thread_ctx);
		req->res.status = 500;
		req->res.reason = "Queue overflow";
		h2o_send_inline(req, H2O_STRLIT("Queue overflow\n"));
		return 0;
	}

	return 0;
}

static void on_socketclose(void *data)
{
	thread_ctx_t *const thread_ctx = (thread_ctx_t *)data;
	--thread_ctx->num_connections;
}

static void on_call_from_tx(h2o_socket_t *listener, const char *err)
{
	if (err != NULL)
		return;

	thread_ctx_t *const thread_ctx = (thread_ctx_t *)listener->data;
	xtm_fun_invoke_all(thread_ctx->queue_from_tx);
}

static void on_accept(h2o_socket_t *listener, const char *err)
{
	if (err != NULL)
		return;

	listener_ctx_t *const listener_ctx = (listener_ctx_t *)listener->data;
	thread_ctx_t *const thread_ctx = listener_ctx->thread_ctx;
	unsigned remain = conf.num_accepts;

	do {
		if (thread_ctx->num_connections >= conf.max_conn_per_thread)
			break;
		h2o_socket_t *sock = h2o_evloop_socket_accept(listener);
		if (sock == NULL)
			return;

		++thread_ctx->num_connections;

		sock->on_close.cb = on_socketclose;
		sock->on_close.data = thread_ctx;

		h2o_accept(&listener_ctx->accept_ctx, sock);
	} while (--remain);
}

static inline void set_cloexec(int fd)
{
	/* For performance reasons do not check result in production builds
	 * (should not fail anyway).
	 * TODO: Remove this call completely? Do we plan to create
	 * child processes ? */
	int result = fcntl(fd, F_SETFD, FD_CLOEXEC);
	assert(result != -1);
}

/** Returns file descriptor or -1 on error */
static int open_listener_ipv4(const char *addr_str, uint16_t port)
{
	struct sockaddr_in addr;
	int fd;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	if (!inet_aton(addr_str, &addr.sin_addr)) {
		return -1;
	}
	addr.sin_port = htons(port);

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		return -1;
	}
	set_cloexec(fd);

	int reuseaddr_flag = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_flag, sizeof(reuseaddr_flag)) != 0 ||
            bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
	    listen(fd, SOMAXCONN) != 0) {
		close(fd);
		/* TODO: Log error */
		return -1;
	}

#ifdef TCP_DEFER_ACCEPT
	{
		/* We are only interested in connections when actual data is received */
		int flag = 1;
		if (setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &flag, sizeof(flag)) != 0) {
			close(fd);
			/* TODO: Log error */
			return -1;
		}
	}
#endif /* TCP_DEFER_ACCEPT */

	if (conf.tfo_queues > 0) {
		/* TCP extension to do not wait for SYN/ACK for "known" clients */
#ifdef TCP_FASTOPEN
		int tfo_queues;
#ifdef __APPLE__
		/* In OS X, the option value for TCP_FASTOPEN must be 1 if is's enabled */
		tfo_queues = 1;
#else
		tfo_queues = conf.tfo_queues;
#endif /* __APPLE__ */
		if (setsockopt(fd, IPPROTO_TCP, TCP_FASTOPEN, (const void *)&tfo_queues, sizeof(tfo_queues)) != 0) {
			/* TODO: Log warning */
		}
#else
		assert(!"conf.tfo_queues not zero on platform without TCP_FASTOPEN");
#endif /* TCP_FASTOPEN */
	}

	return fd;
}

static SSL_CTX *setup_ssl(const char *cert_file, const char *key_file)
{
	if (!SSL_load_error_strings())
		return NULL;
	SSL_library_init(); /* Always succeeds */
	OpenSSL_add_all_algorithms();

	SSL_CTX *ssl_ctx = SSL_CTX_new(SSLv23_server_method());
	if (ssl_ctx == NULL)
		return NULL;
	SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_SSLv2);//x x x: //make configurable;

	if (SSL_CTX_use_certificate_file(ssl_ctx, cert_file, SSL_FILETYPE_PEM) != 1)
		return NULL;
	if (SSL_CTX_use_PrivateKey_file(ssl_ctx, key_file, SSL_FILETYPE_PEM) != 1)
		return NULL;

/* setup protocol negotiation methods */
#if H2O_USE_NPN
	h2o_ssl_register_npn_protocols(ssl_ctx, h2o_http2_npn_protocols);
#endif
#if H2O_USE_ALPN
	h2o_ssl_register_alpn_protocols(ssl_ctx, h2o_http2_alpn_protocols);
#endif

	return ssl_ctx;
}

static void *worker_func(void *param)
{
	const unsigned thread_idx = (unsigned)(uintptr_t)param;
	thread_ctx_t *const thread_ctx = &conf.thread_ctxs[thread_idx];
	curr_thread_ctx = thread_ctx;
	thread_ctx->idx = thread_idx;
	thread_ctx->tid = pthread_self();
	if ((thread_ctx->queue_from_tx = xtm_create(QUEUE_FROM_TX_ITEMS)) == NULL) {
		//TODO: Report
		return NULL;
	}

	if ((thread_ctx->listener_ctxs = (listener_ctx_t *)malloc(conf.num_listeners * sizeof(listener_ctx_t))) == NULL) {
		//TODO: Report
		xtm_delete(thread_ctx->queue_from_tx);
		return NULL;
	}

	memset(&thread_ctx->ctx, 0, sizeof(thread_ctx->ctx));
	h2o_context_init(&thread_ctx->ctx, h2o_evloop_create(), &conf.globalconf);

	listener_ctx_t *listener_ctx = &thread_ctx->listener_ctxs[0];//x x x: More than one
	listener_cfg_t *listener_cfg = &conf.listener_cfgs[0];//x x x: More than one
	memset(listener_ctx, 0, sizeof(*listener_ctx));
	listener_ctx->thread_ctx = thread_ctx;
	listener_ctx->accept_ctx.ssl_ctx = conf.ssl_ctx;
	listener_ctx->accept_ctx.ctx = &thread_ctx->ctx;
	listener_ctx->accept_ctx.hosts = conf.globalconf.hosts;
	if (thread_idx) {
		if ((listener_ctx->fd = dup(listener_cfg->fd)) == -1) {
			//TODO: Report
			free(thread_ctx->listener_ctxs);
			xtm_delete(thread_ctx->queue_from_tx);
			return NULL;
		}
		set_cloexec(listener_ctx->fd);
	} else {
		listener_ctx->fd = listener_cfg->fd;
	}
	listener_ctx->sock = h2o_evloop_socket_create(thread_ctx->ctx.loop, listener_ctx->fd, H2O_SOCKET_FLAG_DONT_READ);
        listener_ctx->sock->data = listener_ctx;

	thread_ctx->sock_from_tx = h2o_evloop_socket_create(thread_ctx->ctx.loop, xtm_fd(thread_ctx->queue_from_tx), H2O_SOCKET_FLAG_DONT_READ);
        thread_ctx->sock_from_tx->data = thread_ctx;

	h2o_socket_read_start(thread_ctx->sock_from_tx, on_call_from_tx);
	h2o_socket_read_start(listener_ctx->sock, on_accept);

	__sync_synchronize(); /* For the fiber in TX thread to see everything we have initialized */

	//x x x;//SIGTERM should terminate loop
	h2o_evloop_t *loop = thread_ctx->ctx.loop;
	while (h2o_evloop_run(loop, INT32_MAX) == 0)
		;

	//void h2o_socket_read_stop(h2o_socket_t *sock);//x x x;
	//x x x;//should flush these queues first
	xtm_delete(thread_ctx->queue_from_tx);
	free(thread_ctx->listener_ctxs);
	return NULL;
}

static int
tx_fiber_func(va_list ap)
{
	const unsigned fiber_idx = va_arg(ap, unsigned);
	/* This fiber processes requests from particular thread */
	thread_ctx_t *const thread_ctx = &conf.thread_ctxs[fiber_idx];
	struct xtm_queue *const queue_to_tx = thread_ctx->queue_to_tx;
	const int pipe_fd = xtm_fd(queue_to_tx);
	/* conf.tx_fiber_should_work is read non-atomically for performance
	 * reasons so it should be changed in this thread by queueing
	 * corresponding function call */
	while (conf.tx_fiber_should_work) {
		if (coio_wait(pipe_fd, COIO_READ, DBL_MAX) & COIO_READ) {
			xtm_fun_invoke_all(queue_to_tx);
		}
	}
	return 0;
}

int init(lua_State *L)
{
	(void)L;
	memset(&conf.globalconf, 0, sizeof(conf.globalconf));

	conf.tx_fiber_should_work = 1;
	conf.max_conn_per_thread = 64; /* Stub */
	conf.num_accepts = conf.max_conn_per_thread / 16;
	if (conf.num_accepts < 8)
		conf.num_accepts = 8;

	if (conf.num_threads > (1ULL << (8 * sizeof(((shuttle_t *)0)->thread_idx))))
		goto Error;
	if ((conf.thread_ctxs = (thread_ctx_t *)malloc(conf.num_threads * sizeof(thread_ctx_t))) == NULL)
		goto Error;

	h2o_config_init(&conf.globalconf);
	h2o_hostconf_t *hostconf = h2o_config_register_host(&conf.globalconf, h2o_iovec_init(H2O_STRLIT("default")), H2O_DEFAULT_PORT_FOR_PROTOCOL_USED); //x x x: customizable
	if (hostconf == NULL)
		goto Error;
	register_handler(hostconf, "/", req_handler);

	SSL_CTX *ssl_ctx;
	if (USE_HTTPS && (ssl_ctx = setup_ssl("cert.pem", "key.pem")) == NULL) //x x x: customizable file names
		goto Error;

#if 0
	/* Never returns NULL */
	h2o_logger_t *logger = h2o_access_log_register(&config.default_host, "/dev/stdout", NULL); //x x x: customizable
#endif

	conf.ssl_ctx = ssl_ctx;

	/* TODO: Implement more than one listener (HTTP/HTTPS, IPv4/IPv6, several IPs etc) */
	conf.num_listeners = 1; //x x x: customizable
	if ((conf.listener_cfgs = (listener_cfg_t *)malloc(conf.num_listeners * sizeof(listener_cfg_t))) == NULL)
		goto Error;

	{
		unsigned listener_idx;

		for (listener_idx = 0; listener_idx < conf.num_listeners; ++listener_idx)
			if ((conf.listener_cfgs[listener_idx].fd = open_listener_ipv4("127.0.0.1", 7890)) == -1) //x x x //customizable
				goto Error;
	}

	if ((conf.tx_fiber_ptrs = (struct fiber **)malloc(sizeof(struct fiber *) * conf.num_threads)) == NULL)
		goto Error;

	{
		unsigned i;
		for (i = 0; i < conf.num_threads; ++i) {
			if ((conf.thread_ctxs[i].queue_to_tx = xtm_create(QUEUE_TO_TX_ITEMS)) == NULL)
				goto Error;
			conf.thread_ctxs[i].num_connections = 0;

			char name[32];
			sprintf(name, "tx_h2o_fiber_%u", i);
			if ((conf.tx_fiber_ptrs[i] = fiber_new(name, tx_fiber_func)) == NULL)
				goto Error;
			fiber_set_joinable(conf.tx_fiber_ptrs[i], true);
			fiber_start(conf.tx_fiber_ptrs[i], i);
		}
	}

	if (!init_userdata_in_tx())
		goto Error;

	/* Start processing HTTP requests and requests from TX thread */
	{
		unsigned i;
		for (i = 0; i < conf.num_threads; ++i) {
			pthread_t tid;
			if (pthread_create(&tid, NULL, worker_func, (void *)(uintptr_t)i))
				goto Error;
		}
	}

	return 0;

Error:
	//FIXME: Release resources, report errors details
	return 0;//x x x;
}

int deinit(lua_State *L)
{
	(void)L;
	//x x x; //terminate workers
	free(conf.listener_cfgs);
	free(conf.thread_ctxs);
	return 0;
}

static const struct luaL_Reg mylib[] = {
	{"init", init},
	{NULL, NULL}
};

int luaopen_h2o(lua_State *L)
{
	luaL_newlib(L, mylib);
	return 1;
}
