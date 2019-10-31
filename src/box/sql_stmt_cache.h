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

#include "small/rlist.h"

#if defined(__cplusplus)
extern "C" {
#endif

struct sql_stmt;
struct mh_strnptr_t;

struct stmt_cache_entry {
	/** Prepared statement itself. */
	struct sql_stmt *stmt;
	/**
	 * Link to the next entry. Head is the newest, tail is
	 * a candidate to be evicted.
	 */
	struct rlist in_lru;
};

/**
 * Global prepared statement cache which follows LRU
 * eviction policy. Implemented as hash <str : stmt>
 * and double linked list.
 */
struct sql_stmt_cache {
	/** Size of memory currently occupied by prepared statements. */
	size_t mem_used;
	/**
	 * Max memory size that can be used for cache.
	 */
	size_t mem_quota;
	/** Query str -> struct sql_stmt hash.*/
	struct mh_strnptr_t *hash;
	/**
	 * LRU list containing the same records as in @hash.
	 * It is maintained to implement eviction policy.
	 */
	struct rlist cache_lru;
	/**
	 * Last result of sql_stmt_cache_find() invocation.
	 * Since during processing prepared statement it
	 * may require to find the same statement several
	 * times.
	 */
	struct stmt_cache_entry *last_found;
};

/**
 * Initialize global cache for prepared statements. Called once
 * in sql_init().
 */
void
sql_stmt_cache_init();

/**
 * Account LRU cache entry as the newest one (i.e. move to the HEAD
 * of LRU list).
 */
void
sql_stmt_cache_refresh(struct sql_stmt *stmt);

int
sql_stmt_cache_update(struct sql_stmt *old_stmt, struct sql_stmt *new_stmt);

/**
 * Save prepared statement to the prepared statement cache.
 * Account cache size change. If the cache is full (i.e. memory
 * quota is exceeded) diag error is raised. In case of success
 * return id of prepared statement via output parameter @id.
 */
int
sql_stmt_cache_insert(struct sql_stmt *stmt);

/** Find entry by SQL string. In case of search fails it returns NULL. */
struct sql_stmt *
sql_stmt_cache_find(const char *sql_str, uint32_t len);

/** Set prepared cache size limit. */
void
sql_stmt_cache_set_size(size_t size);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif
