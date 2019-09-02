/*
 * Copyright 2010-2018, Tarantool AUTHORS, please see AUTHORS file.
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
#include "box/lua/txn.h"
#include "box/txn.h"

#include <tarantool_ev.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "lua/utils.h"
#include "lua/trigger.h"

#include "box/box.h"
#include "box/schema.h"

struct txn_savepoint*
luaT_check_txn_savepoint(struct lua_State *L, int idx)
{
	if (lua_type(L, idx) != LUA_TTABLE) 
		return NULL;

        lua_getfield(L, idx, "csavepoint");
        
	uint32_t cdata_type;
	struct txn_savepoint **sp_ptr = luaL_checkcdata(L, idx + 1, &cdata_type);

        lua_pop(L, 1);

	if (sp_ptr == NULL)
		return NULL;

	return *sp_ptr;
}

static int 
lbox_txn_rollback_to_savepoint(struct lua_State *L)
{
        struct txn_savepoint *point;
        if (lua_gettop(L) != 1 ||
            (point = luaT_check_txn_savepoint(L, 1)) == NULL)
                return luaL_error(L, "Usage: txn:rollback_to_savepoint(savepoint)");

        int rc = box_txn_rollback_to_savepoint(point);
        if (rc != 0)
            return luaT_push_nil_and_error(L);

        return 0;
}

static const struct luaL_Reg lbox_txn_lib[] = {
	{"rollback_to_savepoint", lbox_txn_rollback_to_savepoint},
	{NULL, NULL}
};

void
box_lua_txn_init(struct lua_State *L)
{
	luaL_register_module(L, "box.txn", lbox_txn_lib);
	lua_pop(L, 1);
}
