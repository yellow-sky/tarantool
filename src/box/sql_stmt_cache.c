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
#include "sql_stmt_cache.h"

#include "assoc.h"
#include "error.h"
#include "execute.h"
#include "session.h"
#include "info/info.h"

static struct sql_stmt_cache sql_stmt_cache;

void
sql_stmt_cache_init()
{
	sql_stmt_cache.hash = mh_strnptr_new();
	if (sql_stmt_cache.hash == NULL)
		panic("out of memory");
	sql_stmt_cache.mem_quota = 0;
	sql_stmt_cache.mem_used = 0;
	rlist_create(&sql_stmt_cache.cache_lru);
}

void
sql_stmt_cache_stat(struct info_handler *h)
{
	info_begin(h);
	info_table_begin(h, "cache");
	info_append_int(h, "size", sql_stmt_cache.mem_used);
	uint32_t entry_count = 0;
	struct stmt_cache_entry *entry;
	rlist_foreach_entry(entry, &sql_stmt_cache.cache_lru, in_lru)
		entry_count++;
	info_append_int(h, "stmt_count", entry_count);
	info_table_end(h);
	info_end(h);
}

static size_t
sql_cache_entry_sizeof(struct sql_stmt *stmt)
{
	return sql_stmt_est_size(stmt) + sizeof(struct stmt_cache_entry);
}

static void
sql_cache_entry_delete(struct stmt_cache_entry *entry)
{
	sql_stmt_finalize(entry->stmt);
	TRASH(entry);
	free(entry);
}

/**
 * Remove statement entry from cache: firstly delete from hash,
 * than remove from LRU list and account cache size changes,
 * finally release occupied memory.
 */
static void
sql_stmt_cache_delete(struct stmt_cache_entry *entry)
{
	struct sql_stmt_cache *cache = &sql_stmt_cache;
	const char *sql_str = sql_stmt_query_str(entry->stmt);
	mh_int_t hash_id =
		mh_strnptr_find_inp(cache->hash, sql_str, strlen(sql_str));
	assert(hash_id != mh_end(cache->hash));
	mh_strnptr_del(cache->hash, hash_id, NULL);
	if (sql_stmt_cache.last_found == entry)
		sql_stmt_cache.last_found = NULL;
	rlist_del(&entry->in_lru);
	cache->mem_used -= sql_cache_entry_sizeof(entry->stmt);
	sql_cache_entry_delete(entry);
}

static struct stmt_cache_entry *
stmt_cache_find_entry(const char *sql_str, uint32_t len)
{
	if (sql_stmt_cache.last_found != NULL) {
		const char *sql_str_last =
			sql_stmt_query_str(sql_stmt_cache.last_found->stmt);
		if (strncmp(sql_str, sql_str_last, len) == 0)
			return sql_stmt_cache.last_found;
		/* Fallthrough to slow hash search. */
	}
	struct mh_strnptr_t *hash = sql_stmt_cache.hash;
	mh_int_t stmt = mh_strnptr_find_inp(hash, sql_str, len);
	if (stmt == mh_end(hash))
		return NULL;
	struct stmt_cache_entry *entry = mh_strnptr_node(hash, stmt)->val;
	if (entry == NULL)
		return NULL;
	sql_stmt_cache.last_found = entry;
	return entry;
}

void
sql_stmt_cache_refresh(struct sql_stmt *stmt)
{
	const char *sql_str = sql_stmt_query_str(stmt);
	struct stmt_cache_entry *entry =
		stmt_cache_find_entry(sql_str, strlen(sql_str));
	rlist_move_entry(&sql_stmt_cache.cache_lru, entry, in_lru);
}

int
sql_stmt_cache_update(struct sql_stmt *old_stmt, struct sql_stmt *new_stmt)
{
	const char *sql_str = sql_stmt_query_str(old_stmt);
	struct stmt_cache_entry *entry =
		stmt_cache_find_entry(sql_str, strlen(sql_str));
	sql_stmt_cache_delete(entry);
	if (sql_stmt_cache_insert(new_stmt) != 0) {
		sql_stmt_finalize(new_stmt);
		return -1;
	}
	return 0;
}

static void
sql_stmt_cache_gc()
{
	if (rlist_empty(&sql_stmt_cache.cache_lru)) {
		assert(sql_stmt_cache.mem_used == 0);
		return;
	}
	struct stmt_cache_entry *entry =
		rlist_last_entry(&sql_stmt_cache.cache_lru,
				 struct stmt_cache_entry, in_lru);
	while (sql_stmt_busy(entry->stmt))
		entry = rlist_next_entry(entry, in_lru);
	if (entry == NULL)
		return;
	/*
	 * TODO: instead of following simple LRU rule it could turn
	 * out to be reasonable to also account value of reference
	 * counters.
	 */
	sql_stmt_cache_delete(entry);
}

/**
 * Allocate new cache entry containing given prepared statement.
 * Add it to the LRU cache list. Account cache size enlargement.
 */
static struct stmt_cache_entry *
sql_cache_entry_new(struct sql_stmt *stmt)
{
	struct stmt_cache_entry *entry = malloc(sizeof(*entry));
	if (entry == NULL) {
		diag_set(OutOfMemory, sizeof(*entry), "malloc",
			 "struct stmt_cache_entry");
		return NULL;
	}
	entry->stmt = stmt;
	return entry;
}

/**
 * Return true if used memory (accounting new entry) for SQL
 * prepared statement cache does not exceed the limit.
 */
static bool
sql_cache_check_new_entry_size(size_t size)
{
	return sql_stmt_cache.mem_used + size <= sql_stmt_cache.mem_quota;
}

int
sql_stmt_cache_insert(struct sql_stmt *stmt)
{
	assert(stmt != NULL);
	struct sql_stmt_cache *cache = &sql_stmt_cache;
	size_t new_entry_size = sql_cache_entry_sizeof(stmt);
	if (new_entry_size > sql_stmt_cache.mem_quota) {
		diag_set(ClientError, ER_SQL_PREPARE, "size of statement "\
			"exceeds cache memory limit. Please, increase SQL "\
			"cache size");
		return -1;
	}
	while (! sql_cache_check_new_entry_size(new_entry_size))
		sql_stmt_cache_gc();
	struct mh_strnptr_t *hash = cache->hash;
	const char *sql_str = sql_stmt_query_str(stmt);
	assert(sql_stmt_cache_find(sql_str, strlen(sql_str)) == NULL);
	struct stmt_cache_entry *entry = sql_cache_entry_new(stmt);
	if (entry == NULL)
		return -1;
	uint32_t str_hash = mh_strn_hash(sql_str, strlen(sql_str));
	const struct mh_strnptr_node_t hash_node = { sql_str, strlen(sql_str),
						     str_hash, entry };
	struct mh_strnptr_node_t *old_node = NULL;
	mh_int_t i = mh_strnptr_put(hash, &hash_node, &old_node, NULL);
	if (i == mh_end(hash)) {
		sql_cache_entry_delete(entry);
		diag_set(OutOfMemory, 0, "mh_strnptr_put", "mh_strnptr_node");
		return -1;
	}
	assert(old_node == NULL);
	rlist_add(&sql_stmt_cache.cache_lru, &entry->in_lru);
	sql_stmt_cache.mem_used += sql_cache_entry_sizeof(stmt);
	return 0;
}

struct sql_stmt *
sql_stmt_cache_find(const char *sql_str, uint32_t len)
{
	struct stmt_cache_entry *entry = stmt_cache_find_entry(sql_str, len);
	if (entry == NULL)
		return NULL;
	return entry->stmt;
}

void
sql_stmt_cache_set_size(size_t size)
{
	sql_stmt_cache.mem_quota = size;
	while (sql_stmt_cache.mem_used > size)
		sql_stmt_cache_gc();
}
