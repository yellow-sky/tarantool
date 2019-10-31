/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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
#include "prep_stmt.h"

#include "assoc.h"
#include "error.h"
#include "execute.h"
#include "session.h"

/** Default cache size is 5 Mb. */
size_t prep_stmt_cache_size = 5 * 1024 * 1024;

int
sql_prepared_stmt_cache_insert(struct sql_stmt *stmt, uint32_t *id)
{
	assert(stmt != NULL);
	struct session *session = current_session();
	if (session->prepared_stmt_cache.mem_used + sql_stmt_sizeof(stmt) >
	    session->prepared_stmt_cache.mem_quota) {
		diag_set(ClientError, ER_SQL_PREPARE,
			 "prepared statement cache is full");
		return -1;
	}
	*id = session->prepared_stmt_cache.current_id;
	const struct mh_i32ptr_node_t node = { *id, stmt } ;
	struct mh_i32ptr_node_t *old_node = NULL;
	struct mh_i32ptr_t *hash = session->prepared_stmt_cache.hash;
	mh_int_t i = mh_i32ptr_put(hash, &node, &old_node, NULL);
	if (i == mh_end(hash)) {
		diag_set(OutOfMemory, 0, "mh_i32ptr_put", "mh_i32ptr_node_t");
		return -1;
	}
	assert(old_node == NULL);
	session->prepared_stmt_cache.current_id++;
	session->prepared_stmt_cache.mem_used += sql_stmt_sizeof(stmt);
	return 0;
}

void
sql_prepared_stmt_cache_delete(struct sql_stmt *stmt, uint32_t id)
{
	struct session *session = current_session();
	struct mh_i32ptr_t *hash = session->prepared_stmt_cache.hash;
	mh_int_t id_i = mh_i32ptr_find(hash, id, NULL);
	mh_i32ptr_del(hash, id_i, NULL);
	session->prepared_stmt_cache.mem_used -= sql_stmt_sizeof(stmt);
	sql_finalize(stmt);
}

struct sql_stmt *
sql_prepared_stmt_cache_find(uint32_t id)
{
	struct session *session = current_session();
	struct mh_i32ptr_t *hash = session->prepared_stmt_cache.hash;
	mh_int_t stmt = mh_i32ptr_find(hash, id, NULL);
	if (stmt == mh_end(hash))
		return NULL;
	return mh_i32ptr_node(hash, stmt)->val;;
}

void
sql_stmt_cache_erase(struct prep_stmt_cache *stmts)
{
	assert(stmts != NULL);
	mh_int_t i;
	mh_foreach(stmts->hash, i) {
		struct sql_stmt *stmt =
			(struct sql_stmt *) mh_i32ptr_node(stmts->hash, i)->val;
		sql_finalize(stmt);
		mh_i32ptr_del(stmts->hash, i, NULL);
	}
	/* Reset size to the default state. */
	stmts->mem_used = sizeof(*stmts);
}

void
sql_prepared_stmt_cache_set_size(size_t size)
{
	prep_stmt_cache_size = size;
}
