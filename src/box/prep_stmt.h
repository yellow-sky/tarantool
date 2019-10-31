#ifndef INCLUDES_PREP_STMT_H
#define INCLUDES_PREP_STMT_H
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
#include <stdint.h>
#include <stdio.h>

#if defined(__cplusplus)
extern "C" {
#endif

extern size_t prep_stmt_cache_size;

struct prep_stmt_cache {
	/** Size of memory currently occupied by prepared statements. */
	size_t mem_used;
	/**
	 * Cache is created per session, so cache can't be re-sized
	 * after @prep_stmt_cache is changed. Max memory size that
	 * can be used for cache.
	 */
	size_t mem_quota;
	/** Counter of sequential ids. */
	uint32_t current_id;
	/** Query ID -> struct prepared_stmt hash.*/
	struct mh_i32ptr_t *hash;
};

struct sql_stmt;

/**
 * Save prepared statement to the prepared statement cache.
 * Account cache size change. If the cache is full (i.e. memory
 * quota is exceeded) diag error is raised. In case of success
 * return id of prepared statement via output parameter @id.
 */
int
sql_prepared_stmt_cache_insert(struct sql_stmt *stmt, uint32_t *id);

/** Find entry by id. In case of search fail it returns NULL. */
struct sql_stmt *
sql_prepared_stmt_cache_find(uint32_t id);

/** Remove entry from cache. Account cache size change. */
void
sql_prepared_stmt_cache_delete(struct sql_stmt *stmt, uint32_t id);

/** Remove all elements from cache and deallocate them. */
void
sql_stmt_cache_erase(struct prep_stmt_cache *stmts);

/** Set @prep_stmt_cache_size value. */
void
sql_prepared_stmt_cache_set_size(size_t size);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif
