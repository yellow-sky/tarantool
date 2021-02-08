/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <unistd.h>
#include <string.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <lua.h>

#include "box/error.h"
#include "box/port.h"
#include "box/func_def.h"

#include "tt_static.h"

#include "assoc.h"
#include "cmod.h"
#include "fiber.h"
#include "module_cache.h"

#include "lua/utils.h"
#include "libeio/eio.h"

/**
 * Function descriptor.
 */
struct cmod_func {
	/**
	 * C function to call.
	 */
	struct module_func mf;
	/** Number of references. */
	int64_t refs;
	/** Length of functon name in @a key. */
	size_t sym_len;
	/** Length of @a key. */
	size_t len;
	/** Function hash key. */
	char key[0];
};

/** Function name to cmod_func hash. */
static struct mh_strnptr_t *cmod_func_hash = NULL;

/** A type to find a module from an object. */
static const char *uname_cmod = "tt_uname_cmod";

/** A type to find a function from an object. */
static const char *uname_func = "tt_uname_cmod_func";

/** Get data associated with an object. */
static void *
get_udata(struct lua_State *L, const char *uname)
{
	void **pptr = luaL_testudata(L, 1, uname);
	return pptr != NULL ? *pptr : NULL;
}

/** Set data to a new value. */
static void
set_udata(struct lua_State *L, const char *uname, void *ptr)
{
	void **pptr = luaL_testudata(L, 1, uname);
	assert(pptr != NULL);
	*pptr = ptr;
}

/** Setup a new data and associate it with an object. */
static void
new_udata(struct lua_State *L, const char *uname, void *ptr)
{
	*(void **)lua_newuserdata(L, sizeof(void *)) = ptr;
	luaL_getmetatable(L, uname);
	lua_setmetatable(L, -2);
}

/**
 * Helpers for function cache.
 */
static void *
cf_cache_find(const char *str, size_t len)
{
	mh_int_t e = mh_strnptr_find_inp(cmod_func_hash, str, len);
	if (e == mh_end(cmod_func_hash))
		return NULL;
	return mh_strnptr_node(cmod_func_hash, e)->val;
}

static int
cf_cache_add(struct cmod_func *cf)
{
	const struct mh_strnptr_node_t nd = {
		.str	= cf->key,
		.len	= cf->len,
		.hash	= mh_strn_hash(cf->key, cf->len),
		.val	= cf,
	};
	mh_int_t e = mh_strnptr_put(cmod_func_hash, &nd, NULL, NULL);
	if (e == mh_end(cmod_func_hash)) {
		diag_set(OutOfMemory, sizeof(nd), "malloc",
			 "cmod: hash node");
		return -1;
	}
	return 0;
}

static void
cache_del(struct cmod_func *cf)
{
	mh_int_t e = mh_strnptr_find_inp(cmod_func_hash,
					 cf->key, cf->len);
	if (e != mh_end(cmod_func_hash))
		mh_strnptr_del(cmod_func_hash, e, NULL);
}

/**
 * Load a module.
 *
 * This function takes a module path from the caller
 * stack @a L and returns cached module instance or
 * creates a new module object.
 *
 * Possible errors:
 *
 * - IllegalParams: module path is either not supplied
 *   or not a string.
 * - SystemError: unable to open a module due to a system error.
 * - ClientError: a module does not exist.
 * - OutOfMemory: unable to allocate a module.
 *
 * @returns module object on success or throws an error.
 */
static int
lcmod_load(struct lua_State *L)
{
	const char msg_noname[] = "Expects cmod.load(\'name\') "
		"but no name passed";

	if (lua_gettop(L) != 1 || !lua_isstring(L, 1)) {
		diag_set(IllegalParams, msg_noname);
		return luaT_error(L);
	}

	size_t name_len;
	const char *name = lua_tolstring(L, 1, &name_len);

	if (name_len < 1) {
		diag_set(IllegalParams, msg_noname);
		return luaT_error(L);
	}

	struct module *m = module_load(name, name_len);
	if (m == NULL)
		return luaT_error(L);

	new_udata(L, uname_cmod, m);
	return 1;
}

/**
 * Unload a module.
 *
 * Take a module object from the caller stack @a L and unload it.
 *
 * Possible errors:
 *
 * - IllegalParams: module is not supplied.
 * - IllegalParams: the module is unloaded.
 *
 * @returns true on success or throwns an error.
 */
static int
lcmod_unload(struct lua_State *L)
{
	if (lua_gettop(L) != 1) {
		diag_set(IllegalParams, "Expects module:unload()");
		return luaT_error(L);
	}

	struct module *m = get_udata(L, uname_cmod);
	if (m == NULL) {
		diag_set(IllegalParams, "The module is unloaded");
		return luaT_error(L);
	}

	set_udata(L, uname_cmod, NULL);
	module_unload(m);
	lua_pushboolean(L, true);
	return 1;
}

/** Handle __index request for a module object. */
static int
lcmod_index(struct lua_State *L)
{
	lua_getmetatable(L, 1);
	lua_pushvalue(L, 2);
	lua_rawget(L, -2);
	if (!lua_isnil(L, -1))
		return 1;

	struct module *m = get_udata(L, uname_cmod);
	if (m == NULL) {
		lua_pushnil(L);
		return 1;
	}

	const char *key = lua_tostring(L, 2);
	if (key == NULL || lua_type(L, 2) != LUA_TSTRING) {
		diag_set(IllegalParams,
			 "Bad params, use __index(obj, <string>)");
		return luaT_error(L);
	}

	if (strcmp(key, "path") == 0) {
		lua_pushstring(L, m->package);
		return 1;
	}

	/*
	 * Internal keys for debug only, not API.
	 */
	if (strncmp(key, "tt_dev.", 7) == 0) {
		const char *subkey = &key[7];
		if (strcmp(subkey, "refs") == 0) {
			lua_pushnumber(L, m->refs);
			return 1;
		} else if (strcmp(subkey, "ptr") == 0) {
			const char *s = tt_sprintf("%p", m);
			lua_pushstring(L, s);
			return 1;
		}
	}
	return 0;
}

/** Module representation for REPL (console). */
static int
lcmod_serialize(struct lua_State *L)
{
	struct module *m = get_udata(L, uname_cmod);
	if (m == NULL) {
		lua_pushnil(L);
		return 1;
	}

	lua_createtable(L, 0, 1);
	lua_pushstring(L, m->package);
	lua_setfield(L, -2, "path");
	return 1;
}

/** Collect a module. */
static int
lcmod_gc(struct lua_State *L)
{
	struct module *m = get_udata(L, uname_cmod);
	if (m != NULL) {
		set_udata(L, uname_cmod, NULL);
		module_unload(m);
	}
	return 0;
}

/** Increase reference to a function. */
static void
cmod_func_ref(struct cmod_func *cf)
{
	assert(cf->refs >= 0);
	++cf->refs;
}

/** Free function memory. */
static void
cmod_func_delete(struct cmod_func *cf)
{
	TRASH(cf);
	free(cf);
}

/** Unreference a function and free if last one. */
static void
cmod_func_unref(struct cmod_func *cf)
{
	assert(cf->refs > 0);
	if (--cf->refs == 0) {
		module_func_unload(&cf->mf);
		cache_del(cf);
		cmod_func_delete(cf);
	}
}

/** Function name from a hash key. */
static char *
cmod_func_name(struct cmod_func *cf)
{
	return &cf->key[cf->len - cf->sym_len];
}

/**
 * Allocate a new function instance and resolve its address.
 *
 * @param m a module the function should be loaded from.
 * @param key function hash key, ie "addr.module.foo".
 * @param len length of @a key.
 * @param sym_len function symbol name length, ie 3 for "foo".
 *
 * @returns function instance on success, NULL otherwise (diag is set).
 */
static struct cmod_func *
cmod_func_new(struct module *m, const char *key, size_t len, size_t sym_len)
{
	size_t size = sizeof(struct cmod_func) + len + 1;
	struct cmod_func *cf = malloc(size);
	if (cf == NULL) {
		diag_set(OutOfMemory, size, "malloc", "cf");
		return NULL;
	}

	cf->len = len;
	cf->sym_len = sym_len;
	cf->refs = 0;

	module_func_create(&cf->mf);
	memcpy(cf->key, key, len);
	cf->key[len] = '\0';

	if (module_func_load(m, cmod_func_name(cf), &cf->mf) != 0) {
		diag_set(ClientError, ER_LOAD_FUNCTION,
			 cmod_func_name(cf), dlerror());
		cmod_func_delete(cf);
		return NULL;
	}

	if (cf_cache_add(cf) != 0) {
		cmod_func_delete(cf);
		return NULL;
	}

	/*
	 * Each new function depends on module presence.
	 * Module will reside even if been unload
	 * explicitly after the function creation.
	 */
	cmod_func_ref(cf);
	return cf;
}

/**
 * Load a function.
 *
 * This function takes a function name from the caller
 * stack @a L and either returns a cached function or
 * creates a new function object.
 *
 * Possible errors:
 *
 * - IllegalParams: function name is either not supplied
 *   or not a string.
 * - SystemError: unable to open a module due to a system error.
 * - ClientError: a module does not exist.
 * - OutOfMemory: unable to allocate a module.
 *
 * @returns module object on success or throws an error.
 */
static int
lcmod_load_func(struct lua_State *L)
{
	const char *method = "function = module:load";
	const char fmt_noname[] = "Expects %s(\'name\') but no name passed";

	if (lua_gettop(L) != 2 || !lua_isstring(L, 2)) {
		diag_set(IllegalParams, fmt_noname, method);
		return luaT_error(L);
	}

	struct module *m = get_udata(L, uname_cmod);
	if (m == NULL) {
		const char *fmt =
			"Expects %s(\'name\') but not module object passed";
		diag_set(IllegalParams, fmt, method);
		return luaT_error(L);
	}

	size_t sym_len;
	const char *sym = lua_tolstring(L, 2, &sym_len);

	if (sym_len < 1) {
		diag_set(IllegalParams, fmt_noname, method);
		return luaT_error(L);
	}

	/*
	 * Functions are bound to a module symbols, thus
	 * since the hash is global it should be unique
	 * per module. The symbol (function name) is the
	 * last part of the hash key.
	 */
	const char *key = tt_sprintf("%p.%s.%s", (void *)m,
				     m->package, sym);
	size_t len = strlen(key);

	struct cmod_func *cf = cf_cache_find(key, len);
	if (cf == NULL) {
		cf = cmod_func_new(m, key, len, sym_len);
		if (cf == NULL)
			return luaT_error(L);
	} else {
		cmod_func_ref(cf);
	}

	new_udata(L, uname_func, cf);
	return 1;
}

/**
 * Unload a function.
 *
 * Take a function object from the caller stack @a L and unload it.
 *
 * Possible errors:
 *
 * - IllegalParams: the function is not supplied.
 * - IllegalParams: the function already unloaded.
 *
 * @returns true on success or throwns an error.
 */
static int
lfunc_unload(struct lua_State *L)
{
	if (lua_gettop(L) != 1) {
		diag_set(IllegalParams, "Expects function:unload()");
		return luaT_error(L);
	}

	struct cmod_func *cf = get_udata(L, uname_func);
	if (cf == NULL) {
		diag_set(IllegalParams, "The function is unloaded");
		return luaT_error(L);
	}

	set_udata(L, uname_func, NULL);
	cmod_func_unref(cf);

	lua_pushboolean(L, true);
	return 1;
}

/** Handle __index request for a function object. */
static int
lfunc_index(struct lua_State *L)
{
	lua_getmetatable(L, 1);
	lua_pushvalue(L, 2);
	lua_rawget(L, -2);
	if (!lua_isnil(L, -1))
		return 1;

	struct cmod_func *cf = get_udata(L, uname_func);
	if (cf == NULL) {
		lua_pushnil(L);
		return 1;
	}

	const char *key = lua_tostring(L, 2);
	if (key == NULL || lua_type(L, 2) != LUA_TSTRING) {
		diag_set(IllegalParams,
			 "Bad params, use __index(obj, <string>)");
		return luaT_error(L);
	}

	if (strcmp(key, "name") == 0) {
		lua_pushstring(L, cmod_func_name(cf));
		return 1;
	}

	/*
	 * Internal keys for debug only, not API.
	 */
	if (strncmp(key, "tt_dev.", 7) == 0) {
		const char *subkey = &key[7];
		if (strcmp(subkey, "refs") == 0) {
			lua_pushnumber(L, cf->refs);
			return 1;
		} else if (strcmp(subkey, "key") == 0) {
			lua_pushstring(L, cf->key);
			return 1;
		} else if (strcmp(subkey, "module.ptr") == 0) {
			const char *s = tt_sprintf("%p", cf->mf.module);
			lua_pushstring(L, s);
			return 1;
		} else if (strcmp(subkey, "module.refs") == 0) {
			lua_pushnumber(L, cf->mf.module->refs);
			return 1;
		}
	}
	return 0;
}

/** Function representation for REPL (console). */
static int
lfunc_serialize(struct lua_State *L)
{
	struct cmod_func *cf = get_udata(L, uname_func);
	if (cf == NULL) {
		lua_pushnil(L);
		return 1;
	}

	lua_createtable(L, 0, 1);
	lua_pushstring(L, cmod_func_name(cf));
	lua_setfield(L, -2, "name");
	return 1;
}

/** Collect a function. */
static int
lfunc_gc(struct lua_State *L)
{
	struct cmod_func *cf = get_udata(L, uname_func);
	if (cf != NULL) {
		set_udata(L, uname_func, NULL);
		cmod_func_unref(cf);
	}
	return 0;
}


/** Call a function by its name from the Lua code. */
static int
lfunc_call(struct lua_State *L)
{
	struct cmod_func *cf = get_udata(L, uname_func);
	if (cf == NULL) {
		diag_set(IllegalParams, "The function is unloaded");
		return luaT_error(L);
	}

	lua_State *args_L = luaT_newthread(tarantool_L);
	if (args_L == NULL)
		return luaT_error(L);

	int coro_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);
	lua_xmove(L, args_L, lua_gettop(L) - 1);

	struct port args;
	port_lua_create(&args, args_L);
	((struct port_lua *)&args)->ref = coro_ref;

	struct port ret;

	if (module_func_call(&cf->mf, &args, &ret) != 0) {
		port_destroy(&args);
		return luaT_error(L);
	}

	int top = lua_gettop(L);
	port_dump_lua(&ret, L, true);
	int cnt = lua_gettop(L) - top;

	port_destroy(&ret);
	port_destroy(&args);

	return cnt;
}

/** Initialize cmod. */
void
box_lua_cmod_init(struct lua_State *L)
{
	cmod_func_hash = mh_strnptr_new();
	if (cmod_func_hash == NULL)
		panic("cmod: Can't allocate func hash table");

	(void)L;
	static const struct luaL_Reg top_methods[] = {
		{ "load",		lcmod_load		},
		{ NULL, NULL },
	};
	luaL_register_module(L, "cmod", top_methods);
	lua_pop(L, 1);

	static const struct luaL_Reg lcmod_methods[] = {
		{ "unload",		lcmod_unload		},
		{ "load",		lcmod_load_func		},
		{ "__index",		lcmod_index		},
		{ "__serialize",	lcmod_serialize		},
		{ "__gc",		lcmod_gc		},
		{ NULL, NULL },
	};
	luaL_register_type(L, uname_cmod, lcmod_methods);

	static const struct luaL_Reg lfunc_methods[] = {
		{ "unload",		lfunc_unload		},
		{ "__index",		lfunc_index		},
		{ "__serialize",	lfunc_serialize		},
		{ "__gc",		lfunc_gc		},
		{ "__call",		lfunc_call		},
		{ NULL, NULL },
	};
	luaL_register_type(L, uname_func, lfunc_methods);
}
