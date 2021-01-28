#define H2O_USE_EPOLL 1 /* FIXME */

#include <module.h>
#include <msgpuck/msgpuck.h>
#include <lauxlib.h>
#include "../../xtm/xtm_api.h"

#include "h2o_module.h"

static const char users_path[] = "/users";
static const char stats_path[] = "/stats";

static struct {
	uint32_t space_id;
	uint32_t index_id;
} our_userdata; /* Site/path-specific */

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
		free_shuttle(shuttle, shuttle->thread_ctx);
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
static int init_userdata_in_tx(void *param)
{
	(void)param;

	static const char space_name[] = "tester";
	if ((our_userdata.space_id = box_space_id_by_name(space_name, sizeof(space_name) - 1)) == BOX_ID_NIL)
		return -1;
	static const char index_name[] = "primary";
	if ((our_userdata.index_id = box_index_id_by_name(our_userdata.space_id, index_name, sizeof(index_name) - 1)) == BOX_ID_NIL)
		return -1;

	return 0;
}

/* Launched in TX thread */
static void process_users_req_in_tx(shuttle_t *shuttle)
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

	struct xtm_queue *const queue_from_tx = shuttle->thread_ctx->queue_from_tx;
	while (xtm_fun_dispatch(queue_from_tx, (void(*)(void *))&post_process_req, shuttle, 0)) {
		/* Error; we must not fail so retry a little later */
		fiber_sleep(0);
	}
}

/* Launched in TX thread */
static void process_stats_req_in_tx(shuttle_t *shuttle)
{
	return process_users_req_in_tx(shuttle); //Stopgap
}

/* Launched in HTTP thread */
static int users_req_handler(h2o_handler_t *self, h2o_req_t *req)
{
	(void)self;
	if (!(h2o_memis(req->method.base, req->method.len, H2O_STRLIT("GET")) &&
	    h2o_memis(req->path_normalized.base, req->path_normalized.len, H2O_STRLIT(users_path))))
		return -1;

	shuttle_t *const shuttle = prepare_shuttle(req);
	/* Can fill in shuttle->payload here */

	thread_ctx_t *const thread_ctx = get_curr_thread_ctx();
	if (xtm_fun_dispatch(thread_ctx->queue_to_tx, (void(*)(void *))&process_users_req_in_tx, shuttle, 0)) {
		/* Error */
		free_shuttle(shuttle, thread_ctx);
		req->res.status = 500;
		req->res.reason = "Queue overflow";
		h2o_send_inline(req, H2O_STRLIT("Queue overflow\n"));
		return 0;
	}

	return 0;
}

/* Launched in HTTP thread */
static int stats_req_handler(h2o_handler_t *self, h2o_req_t *req)
{
	(void)self;
	if (!(h2o_memis(req->method.base, req->method.len, H2O_STRLIT("GET")) &&
	    h2o_memis(req->path_normalized.base, req->path_normalized.len, H2O_STRLIT(stats_path)))) {
		return -1;
	}

	shuttle_t *const shuttle = prepare_shuttle(req);
	/* Can fill in shuttle->payload here */

	thread_ctx_t *const thread_ctx = get_curr_thread_ctx();
	if (xtm_fun_dispatch(thread_ctx->queue_to_tx, (void(*)(void *))&process_stats_req_in_tx, shuttle, 0)) {
		/* Error */
		free_shuttle(shuttle, thread_ctx);
		req->res.status = 500;
		req->res.reason = "Queue overflow";
		h2o_send_inline(req, H2O_STRLIT("Queue overflow\n"));
		return 0;
	}

	return 0;
}

static const path_desc_t our_path_descs[] = {
	{ .path = users_path, .handler = users_req_handler, .init_userdata_in_tx = init_userdata_in_tx, },
	{ .path = stats_path, .handler = stats_req_handler, },
	{ }, /* Terminator */
};

static int get_path_descs(lua_State *L)
{
	/* We are passed one Lua parameter we can use to configure descs etc */
	lua_pop(L, 1);

	lua_pushinteger(L, (uintptr_t)&our_path_descs);
	return 1;
}

static const struct luaL_Reg mylib[] = {
	{"get_path_descs", get_path_descs},
	{NULL, NULL}
};

int luaopen_sample_site(lua_State *L)
{
	luaL_newlib(L, mylib);
	return 1;
}
