#ifndef TARANTOOL_LIB_CORE_REFLECTION_H_INCLUDED
#define TARANTOOL_LIB_CORE_REFLECTION_H_INCLUDED
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

#include <stddef.h>
#include <assert.h>
#include <stdbool.h>
#include "diag.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct type_info;

/**
 * Primitive C types
 */
enum ctype {
	CTYPE_VOID = 0,
	CTYPE_INT,
	CTYPE_CONST_CHAR_PTR
};

union value {
	int _int;
	const char *_char;
};

struct field {
	const char *name;
	enum ctype type;
	union value (*getter)(struct error *);
};

struct type_info {
	const char *name;
	const struct type_info *parent;
	const struct field *fields;
};

inline bool
type_assignable(const struct type_info *type, const struct type_info *object)
{
	assert(object != NULL);
	do {
		if (object == type)
			return true;
		assert(object->parent != object);
		object = object->parent;
	} while (object != NULL);
	return false;
}

/**
 * Determine if the specified object is assignment-compatible with
 * the object represented by type.
 */
#define type_cast(T, obj) ({						\
		T *r = NULL;						\
		if (type_assignable(&type_ ## T, (obj->type)))		\
			r = (T *) obj;					\
		(r);							\
	})

extern const struct field FIELDS_SENTINEL;

inline struct field
make_field(const char *name, enum ctype type,
	   union value (*getter)(struct error* e))
{
        struct field field;
	field.name = name;
	field.type = type;
	field.getter = getter;
	return field;
}

/*
 * Initializer for struct type_info without methods
 */
inline struct type_info
make_type(const char *name, const struct type_info *parent)
{
	struct type_info t;
	t.name = name;
	t.parent = parent;
	t.fields = &FIELDS_SENTINEL;
	return t;
}

/*
 * Initializer for struct type_info with methods
 */
inline struct type_info
make_type_with_fields(const char *name, const struct type_info *parent,
                      const struct field *fields)
{
	struct type_info t;
	t.name = name;
	t.parent = parent;
	t.fields = fields;
	return t;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* TARANTOOL_LIB_CORE_REFLECTION_H_INCLUDED */
