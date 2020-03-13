/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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

#include "space_def.h"
#include "diag.h"
#include "error.h"
#include "msgpuck.h"
#include "tt_static.h"

const struct space_opts space_opts_default = {
	/* .group_id = */ 0,
	/* .is_temporary = */ false,
	/* .is_ephemeral = */ false,
};

const struct opt_def space_opts_reg[] = {
	OPT_DEF("group_id", OPT_UINT32, struct space_opts, group_id),
	OPT_DEF("temporary", OPT_BOOL, struct space_opts, is_temporary),
	OPT_DEF_LEGACY("checks"),
	OPT_END,
};

size_t
space_def_sizeof(uint32_t name_len, const struct field_def *fields,
		 uint32_t field_count, uint32_t *names_offset,
		 uint32_t *fields_offset, uint32_t *def_expr_offset)
{
	uint32_t field_strs_size = 0;
	uint32_t def_exprs_size = 0;
	for (uint32_t i = 0; i < field_count; ++i) {
		field_strs_size += strlen(fields[i].name) + 1;
	}

	*fields_offset = sizeof(struct space_def) + name_len + 1;
	*names_offset = *fields_offset + field_count * sizeof(struct field_def);
	*def_expr_offset = *names_offset + field_strs_size;
	return *def_expr_offset + def_exprs_size;
}

/**
 * Initialize def->opts with opts duplicate.
 * @param def  Def to initialize.
 * @param opts Opts to duplicate.
 * @retval 0 on success.
 * @retval not 0 on error.
 */
static int
space_def_dup_opts(struct space_def *def, const struct space_opts *opts)
{
	def->opts = *opts;
	return 0;
}

struct space_def *
space_def_dup(const struct space_def *src)
{
	uint32_t strs_offset, fields_offset, def_expr_offset;
	size_t size = space_def_sizeof(strlen(src->name), src->fields,
				       src->field_count, &strs_offset,
				       &fields_offset, &def_expr_offset);
	struct space_def *ret = (struct space_def *) malloc(size);
	if (ret == NULL) {
		diag_set(OutOfMemory, size, "malloc", "ret");
		return NULL;
	}
	memcpy(ret, src, size);
	memset(&ret->opts, 0, sizeof(ret->opts));
	char *strs_pos = (char *)ret + strs_offset;
	if (src->field_count > 0) {
		ret->fields = (struct field_def *)((char *)ret + fields_offset);
		for (uint32_t i = 0; i < src->field_count; ++i) {
			ret->fields[i].name = strs_pos;
			strs_pos += strlen(strs_pos) + 1;
		}
	}
	tuple_dictionary_ref(ret->dict);
	if (space_def_dup_opts(ret, &src->opts) != 0) {
		space_def_delete(ret);
		return NULL;
	}
	return ret;
}

struct space_def *
space_def_new(uint32_t id, uint32_t uid, uint32_t exact_field_count,
	      const char *name, uint32_t name_len,
	      const char *engine_name, uint32_t engine_len,
	      const struct space_opts *opts, const struct field_def *fields,
	      uint32_t field_count)
{
	uint32_t strs_offset, fields_offset, def_expr_offset;
	size_t size = space_def_sizeof(name_len, fields, field_count,
				       &strs_offset, &fields_offset,
				       &def_expr_offset);
	struct space_def *def = (struct space_def *) malloc(size);
	if (def == NULL) {
		diag_set(OutOfMemory, size, "malloc", "def");
		return NULL;
	}
	assert(name_len <= BOX_NAME_MAX);
	assert(engine_len <= ENGINE_NAME_MAX);
	def->dict = tuple_dictionary_new(fields, field_count);
	if (def->dict == NULL) {
		free(def);
		return NULL;
	}
	def->id = id;
	def->uid = uid;
	def->exact_field_count = exact_field_count;
	memcpy(def->name, name, name_len);
	def->name[name_len] = 0;
	memcpy(def->engine_name, engine_name, engine_len);
	def->engine_name[engine_len] = 0;

	def->field_count = field_count;
	if (field_count == 0) {
		def->fields = NULL;
	} else {
		char *strs_pos = (char *)def + strs_offset;
		def->fields = (struct field_def *)((char *)def + fields_offset);
		for (uint32_t i = 0; i < field_count; ++i) {
			def->fields[i] = fields[i];
			def->fields[i].name = strs_pos;
			uint32_t len = strlen(fields[i].name);
			memcpy(def->fields[i].name, fields[i].name, len);
			def->fields[i].name[len] = 0;
			strs_pos += len + 1;

		}
	}
	if (space_def_dup_opts(def, opts) != 0) {
		space_def_delete(def);
		return NULL;
	}
	return def;
}

struct space_def*
space_def_new_ephemeral(uint32_t field_count)
{
	struct space_opts opts = space_opts_default;
	opts.is_temporary = true;
	opts.is_ephemeral = true;
	struct space_def *space_def = space_def_new(0, 0, field_count,
						    "ephemeral",
						    strlen("ephemeral"),
						    "memtx", strlen("memtx"),
						    &opts, &field_def_default,
						    0);
	return space_def;
}

void
space_def_delete(struct space_def *def)
{
	space_opts_destroy(&def->opts);
	tuple_dictionary_unref(def->dict);
	TRASH(def);
	free(def);
}

void
space_opts_destroy(struct space_opts *opts)
{
	TRASH(opts);
}
