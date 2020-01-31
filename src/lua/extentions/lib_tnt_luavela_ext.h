#ifndef TARANTOOL_LUA_EXTENTIONS_TNT_LJ_EXT_H_INCLUDED
#define TARANTOOL_LUA_EXTENTIONS_TNT_LJ_EXT_H_INCLUDED

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#include "lua.h"

#include "stdint.h"
#include "ffi/lj_ctype.h"
#include "lj_obj.h"
#include "lextlib.h"

typedef enum MMS MMS;

#define luaM_lua_total_memory(L) (luaE_metrics(L).gc_total)

#define LUAMISC_API LUA_API
/**
 * @brief Push cdata of given \a ctypeid onto the stack.
 * CTypeID must be used from FFI at least once. Allocated memory returned
 * uninitialized. Only numbers and pointers are supported.
 * @param L Lua State
 * @param ctypeid FFI's CTypeID of this cdata
 * @sa tnt_ext_checkcdata
 * @return memory associated with this cdata
 */
LUAMISC_API void *
luaM_pushcdata(struct lua_State *L, uint32_t ctypeid);

/**
 * @brief Sets finalizer function on a cdata object.
 * Equivalent to call ffi.gc(obj, function).
 * Finalizer function must be on the top of the stack.
 * @param L Lua State
 * @param idx object
 */
LUAMISC_API void
luaM_setcdatagc(struct lua_State *L, int idx);

/**
 * @brief Return 1 if cdata has specified metamethod else 0
 * @param L Lua State
 * @param idx object
 * @param metamethod type of metamethod
 */
LUAMISC_API int
luaM_cdata_hasmm(struct lua_State *L, int idx, MMS metamethod);

/**
 * @brief Returns 1 if pushed thread is the main thread of its state
 * Push L1 on top of L Lua state
 * @param L Lua State
 * @param L1 other Lua State
 */
LUAMISC_API int
luaM_pushthread1(lua_State *L, lua_State *L1);

LUAMISC_API uint32_t lua_hashstring(lua_State *L, int idx);

/*
 * Calculate a hash for a specified string. Hash is the same as
 * for luajit string objects (see lj_str_new()).
 */
LUAMISC_API uint32_t (lua_hash) (const char *str, uint32_t len);

#undef LUAMISC_API

static LJ_AINLINE void *
luaM_getcdataptr(struct lua_State *L, int idx)
{
	if (lua_type(L, idx) == LUA_TCDATA) {
		GCcdata *cd = cdataV(L->base + idx - 1);
		return cdataptr(cd);
	}
	return NULL;
}

static LJ_AINLINE CTypeID
luaM_getcdatatype(struct lua_State *L, int idx)
{
	if (lua_type(L, idx) == LUA_TCDATA)
		return cdataV(L->base + idx - 1)->ctypeid;
	return CTID_NONE;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LUA_EXTENTIONS_TNT_LJ_EXT_H_INCLUDED */
