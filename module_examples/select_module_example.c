#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <luaconf.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <stdatomic.h>
#include <pthread.h>
#include <msgpuck/msgpuck.h>

#include "module.h"
#include "libev/ev.h"
#include "queue.h"


#define SOCKET_BUF_MAX 4096
#define MODULE_CLIENT_MAX 30
#define STOP_MAGIC 0xAABBCCDD

struct request {
	/* link for responce queue */
	STAILQ_ENTRY(request) entry;
	/* Flag set, when we allocate memory for responce */
	int responce_used_mem;
	/* Request status, 1 means success */
	int status;
	/* Index of request client */
	int idx;
	/* Request data size */
	unsigned int request_size;
	/* Responce remaining data size */
	unsigned int responce_size;
	/* Pointer to responce data */
	unsigned char *responce;
	/* Currenct position in responce data */
	unsigned char *pos;
	/* Request data */
	unsigned char data[0];
};

STAILQ_HEAD(responce_queue, request);

struct client {
	/* Client socket descriptor */
	int fd;
	/* Client responce queue */
	struct responce_queue queue;
};

struct module_context {
	_Atomic int work;
	/* Pipe for module stop*/
	int fds[2];
	/* Pointer to tarantool module itself*/
	void *module;
	/* Listening port */
	unsigned short port;
	/* Current lua state */
	lua_State *L;
	/* Array of module clients */
	struct client clients[MODULE_CLIENT_MAX];
};

static int tarantool_module_init(void **ctx, void *module);
static void tarantool_module_start(void *ctx, void *loop, int loopfd);
static void tarantool_module_stop(void *ctx);
static void tarantool_module_destroy(void *ctx);

static struct tarantool_module_vtab vtab  = {
	.init = tarantool_module_init,
	.start = tarantool_module_start,
	.stop = tarantool_module_stop,
	.destroy = tarantool_module_destroy
};

static struct module_context context;

static int 
cfg(lua_State *L) 
{
	const char *error;
	if (lua_gettop(L) != 1 || !lua_istable(L, 1)) {
		lua_pushfstring(L, "error: invalid module params");
		lua_error(L);
	}	
	lua_pushstring(L, "port");
	lua_gettable(L, -2);
	context.port = lua_tointeger(L, -1);
	context.L = L;
	lua_pop(L, 1);
	if (context.port == 0) {
		lua_pushfstring(L, "error: invalid module params");
		lua_error(L);
	}
	if (tarantool_start_module(context.module, &error) < 0) {
		lua_pushfstring(L, error);
		lua_error(L);
	}
	return 0;
}

static int
stop(lua_State *L)
{
	const char *error;
	if (tarantool_stop_module(context.module, &error, 0) < 0) {
		lua_pushfstring(L, error);
		lua_error(L);
	}
	return 0;
}

static const struct luaL_Reg select_module_example_lib[] = {
	{"cfg", cfg},
	{"stop", stop},
	{NULL, NULL}
}; 

LUALIB_API int luaopen_select_module_example(lua_State *L) 
{
	const char *error;
	luaL_openlib(L, "select_module_example", select_module_example_lib, 0);
	if (tarantool_register_module("select_module_example", &vtab, &error) < 0) {
		lua_pushfstring(L, error);
		lua_error(L);
	}
	return 0;
}

static void
dump_simple_tuple(box_tuple_t *tuple)
{
	const char *str_value;
	uint32_t str_value_length, count = box_tuple_field_count(tuple);
	for (uint32_t field_no = 0; field_no < count; ++field_no) {
		fprintf(stderr, "field № %u:	", field_no);
		const char *field = box_tuple_field(tuple, field_no);
		enum field_type type = mp_typeof(*field);
		switch (type) {
		case MP_UINT:
			fprintf(stderr, "%u\n", mp_decode_uint(&field));
			break;
		case MP_STR:
			str_value = mp_decode_str(&field, &str_value_length);
			fprintf(stderr, "%.*s\n", str_value_length, str_value);
			break;
		default:
			fprintf(stderr, "undexpected type for simple tuple example\n");
		}
	}
}

static void
tx_process_request(void *data)
{
	struct request *request = (struct request *)data;
	lua_State *L = context.L;
	int pop, top = lua_gettop(L);
	unsigned int size;

	lua_getglobal(L, "box");
	if (!lua_istable(L, -1))
		goto finish;

	lua_pushfstring(L, "space");
	lua_gettable(L, -2);
	if (!lua_istable(L, -1))
		goto finish;

	/* Data is a name of space in this sample */
	lua_pushfstring(L, (char *)request->data);
	lua_gettable(L, -2);
	if (!lua_istable(L, -1))
		goto finish;

	lua_pushfstring(L, "select");
	lua_gettable(L, -2);
	if (!lua_isfunction(L, -1))
		goto finish;

	/* Select tuple with index 1 */
	lua_pushfstring(L, (char *)request->data);
	lua_gettable(L, -4);
	lua_pushinteger(L, 1);
	if (lua_pcall(L, 2, 1, 0) != LUA_OK || !lua_istable(L, -1))
		goto finish;

	lua_rawgeti(L, -1, 1);
	box_tuple_t *tuple = luaT_istuple(L, -1);
	if (!tuple || ((size  = box_tuple_to_buf(tuple, NULL, 0)) < 0))
		goto finish;

#if 0
	dump_simple_tuple(tuple);
#endif

	if (size > request->request_size) {
		request->responce = (unsigned char *)malloc(size);
		if (!request->responce)
			goto finish;
		request->responce_used_mem = 1;
	} else {
		request->responce = request->data;
	}

	if (box_tuple_to_buf(tuple, request->responce, size) < 0)
		goto finish;

	request->responce_size = size;
	request->pos = request->responce;
	request->status = 1;
finish:
	pop = lua_gettop(L) - top;
	lua_pop(L, pop);
}

static void
m_process_request(void *data, int failed)
{
	struct request *request = (struct request *)data;
	if (!failed && request->status) {
		STAILQ_INSERT_TAIL(&context.clients[request->idx].queue, request, entry);
	} else {
		if (request->responce_used_mem)
			free(request->responce);
		free(request);
	}
}

static int tarantool_module_init(void **ctx, void *module)
{
	context.module = module;
	*ctx = &context;
	return 0;
}

static int 
find_first_free(const struct client *clients, const unsigned int size)
{
	for (unsigned int i = 0; i < size; i++) {
		if (clients[i].fd == 0)
			return i;
	}
	return -1;
}

static int 
init_clients(struct client *clients, const unsigned int size)
{
	for (unsigned int i = 0; i < size; i++) {
		clients[i].fd = 0;
		STAILQ_INIT(&clients[i].queue);
	}
}

static void
close_client(struct client *client)
{
	struct request *request, *tmp;
	STAILQ_FOREACH_SAFE(request, &client->queue, entry, tmp) {
		if (request->responce_used_mem)
			free(request->responce);
		free(request);
	}
	STAILQ_INIT(&client->queue);
	close(client->fd);
	client->fd = 0;
}

static void tarantool_module_start(void *ctx, void *loop, int loopfd)
{
	struct sockaddr_in addr;
	char buf[SOCKET_BUF_MAX];	
	struct module_context *context = (struct module_context *)(ctx);
	init_clients(context->clients, MODULE_CLIENT_MAX);
	struct client *clients = context->clients;
	struct ev_loop *loop_ = (struct ev_loop *)loop; 
	int listener = socket(AF_INET, SOCK_STREAM, 0);
	if (listener < 0) {
		perror("socket");
		return;
	}
	if (fcntl(listener, F_SETFL, O_NONBLOCK) < 0) {
		perror("fcntl");
		goto close_listener;
	}

	int enable = 1;
	if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
		perror("setsockopt");
		goto close_listener;
	}

	if (setsockopt(listener, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0) {
		perror("setsockopt");
		goto close_listener;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(context->port);
	addr.sin_addr.s_addr = INADDR_ANY;
	if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		goto close_listener;
	}

	if (listen(listener, MODULE_CLIENT_MAX) < 0) {
		perror("listen");
		goto close_listener;
	}	
	atomic_store(&context->work, 1);
	if (pipe(context->fds) < 0) {
		perror("pipe");
		goto close_listener;
	}

	while (atomic_load(&context->work)) {
		fd_set readset;
		fd_set writeset;
		FD_ZERO(&readset);
		FD_ZERO(&writeset);
		FD_SET(listener, &readset);
		FD_SET(loopfd, &readset);
		FD_SET(context->fds[0], &readset);

		for (unsigned int i = 0; i < MODULE_CLIENT_MAX; i++) {
			if (clients[i].fd > 0) {
				FD_SET(clients[i].fd, &readset);
				FD_SET(clients[i].fd, &writeset);
			}
		}

		int mx = (listener > loopfd ? listener : loopfd);
		mx = (mx > context->fds[0] ? mx : context->fds[0]);
		for (unsigned int i = 0; i < MODULE_CLIENT_MAX; i++) {
			if (clients[i].fd > mx)
				mx = clients[i].fd;
		}

		struct timeval timeval, *wait;
		memset (&timeval, 0, sizeof(timeval));

		if (ev_prepare_extern_loop_wait(loop_)) {
			wait = NULL;
		} else {
			wait = &timeval;
		}

		if (select(mx + 1, &readset, NULL, NULL, wait) < 0) {
			perror("select");
			break;
		}

		if (FD_ISSET(context->fds[0], &readset)) {
			unsigned int magic = 0; 
			read(context->fds[0], &magic, sizeof(magic));
			assert(magic == STOP_MAGIC);
		}

		int revents = 0;
		if (FD_ISSET(loopfd, &readset))
			revents = EV_READ;
		ev_process_events(loop_, loopfd, revents);

		if (FD_ISSET(listener, &readset)) {
			int sock = accept(listener, NULL, NULL);
			if(sock > 0) {
				int free = find_first_free(clients, MODULE_CLIENT_MAX);
				if (fcntl(sock, F_SETFL, O_NONBLOCK) == 0 && free >= 0) {
					clients[free].fd = sock;
				} else {
					close(sock);
				}				
			}
		}		

		for (unsigned int i = 0; i < MODULE_CLIENT_MAX; i++) {
			if(clients[i].fd <= 0)
				continue;

			if (FD_ISSET(clients[i].fd, &readset)) {
				int bytes_read = recv(clients[i].fd, buf, SOCKET_BUF_MAX, 0);
				if (bytes_read <= 0) {
					close_client(&clients[i]);
					continue;
				}
				struct request *request = (struct request *)malloc(sizeof(struct request) + bytes_read);
				if (request) {
					memset(request, 0, sizeof(struct request));
					memcpy(request->data, buf, bytes_read);
					request->idx = i;
					request->request_size = bytes_read;
					tx_msg_send(context->module, request,
						tx_process_request, m_process_request);
				}
			}
			if (FD_ISSET(clients[i].fd, &writeset)) {
				struct request *request = STAILQ_FIRST(&clients[i].queue);
				if(!request)
					continue;

				size_t send_bytes = send(clients[i].fd, request->responce, request->responce_size, 0);
				if (send_bytes <= 0) {
					close_client(&clients[i]);
				} else {
					request->responce_size -= send_bytes;
					if (request->responce_size == 0) {
						STAILQ_REMOVE_HEAD(&clients[i].queue, entry);
						if (request->responce_used_mem)
							free(request->responce);
						free(request);
					}
				}
			} 
		}
	}
    
	for(unsigned int i = 0; i < MODULE_CLIENT_MAX; i++) {
		if (clients[i].fd > 0) {
			close_client(&clients[i]);		
		}
	}

	close(context->fds[0]);
	close(context->fds[1]);
close_listener:
	close(listener);
}

static void tarantool_module_stop(void *ctx)
{
	struct module_context *context = (struct module_context *)(ctx);
	unsigned int magic = STOP_MAGIC;
	atomic_store(&context->work, 0);
	write(context->fds[1], &magic, sizeof(magic));
}

static void tarantool_module_destroy(void *ctx)
{
}