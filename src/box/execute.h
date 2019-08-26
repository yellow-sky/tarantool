#ifndef TARANTOOL_SQL_EXECUTE_H_INCLUDED
#define TARANTOOL_SQL_EXECUTE_H_INCLUDED
/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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
#include <stdbool.h>
#include "opt_def.h"
#include "port.h"

#if defined(__cplusplus)
extern "C" {
#endif

/** Keys of IPROTO_SQL_INFO map. */
enum sql_info_key {
	SQL_INFO_ROW_COUNT = 0,
	SQL_INFO_AUTOINCREMENT_IDS = 1,
	sql_info_key_MAX,
};

extern const char *sql_info_key_strs[];

/** Options which can be passed to :execute. */
struct sql_opts {
	/**
	 * In case of dry-run query is not really executed,
	 * but only prepared (i.e. compiled into byte-code).
	 * It allows to get query's metadata without execution
	 * or update prepared statement cache.
	 */
	bool dry_run;
};

extern const struct sql_opts sql_opts_default;
extern const struct opt_def sql_opts_reg[];

struct region;
struct sql_bind;

/**
 * Prepare and execute an SQL statement.
 * @param sql SQL statement.
 * @param len Length of @a sql.
 * @param bind Array of parameters.
 * @param bind_count Length of @a bind.
 * @param[out] port Port to store SQL response.
 * @param region Runtime allocator for temporary objects
 *        (columns, tuples ...).
 *
 * @retval  0 Success.
 * @retval -1 Client or memory error.
 */
int
sql_prepare_and_execute(const char *sql, int len, const struct sql_bind *bind,
			uint32_t bind_count, struct port *port,
			struct region *region);

/**
 * Prepare (compile into VDBE byte-code) statement.
 *
 * @param sql UTF-8 encoded SQL statement.
 * @param length Length of @param sql in bytes.
 * @param[out] stmt A pointer to the prepared statement.
 * @param[out] sql_tail End of parsed string.
 */
int
sql_prepare(const char *sql, int length, struct sql_stmt **stmt,
	    const char **sql_tail);

/**
 * Execute prepared SQL statement.
 *
 * This function uses region to allocate memory for temporary
 * objects. After this function, region will be in the same state
 * in which it was before this function.
 *
 * @param stmt Prepared statement.
 * @param port Port to store SQL response.
 * @param region Region to allocate temporary objects.
 *
 * @retval  0 Success.
 * @retval -1 Error.
 */
int
sql_execute(struct sql_stmt *stmt, struct port *port, struct region *region);

/**
 * Parse SQL execution options encoded in @param data and fill in
 * corresponding fields in output @param opts.
 */
int
sql_opts_decode(const char *data, struct sql_opts *opts);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif /* TARANTOOL_SQL_EXECUTE_H_INCLUDED */
