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
#include "error.h"
#include <stdio.h>

#include "fiber.h"
#include "rmean.h"
#include "trigger.h"
#include "vclock.h"
#include "schema.h"

/* {{{ public API */

const char *
box_error_type(const box_error_t *e)
{
	return e->type->name;
}

uint32_t
box_error_code(const box_error_t *e)
{
	return ClientError::get_errcode(e);
}

const char *
box_error_message(const box_error_t *error)
{
	return error->errmsg;
}

box_error_t *
box_error_last(void)
{
	return diag_last_error(&fiber()->diag);
}

void
box_error_clear(void)
{
	diag_clear(&fiber()->diag);
}

int
box_error_set(const char *file, unsigned line, uint32_t code,
		const char *fmt, ...)
{
	struct error *e = BuildClientError(file, line, ER_UNKNOWN);
	ClientError *client_error = type_cast(ClientError, e);
	if (client_error) {
		client_error->m_errcode = code;
		va_list ap;
		va_start(ap, fmt);
		error_vformat_msg(e, fmt, ap);
		va_end(ap);
	}
	diag_add_error(&fiber()->diag, e);
	return -1;
}

/* }}} */

struct rmean *rmean_error = NULL;

const char *rmean_error_strings[RMEAN_ERROR_LAST] = {
	"ERROR"
};

union value get_code(struct error *e) {
	ClientError *ce = (ClientError *)e;
	union value retval = {
		._int = ce->errcode()
	};
	return retval;
}

static struct field client_fields[] = {
	make_field("code", CTYPE_INT, &get_code),
	FIELDS_SENTINEL
};

const struct type_info type_ClientError =
	make_type_with_fields("ClientError", &type_Exception, client_fields);

ClientError::ClientError(const type_info *type, const char *file, unsigned line,
			 uint32_t errcode)
	:Exception(type, file, line)
{
	m_errcode = errcode;
	if (rmean_error)
		rmean_collect(rmean_error, RMEAN_ERROR, 1);
}

ClientError::ClientError(const char *file, unsigned line,
			 uint32_t errcode, ...)
	:ClientError(&type_ClientError, file, line, errcode)
{
	va_list ap;
	va_start(ap, errcode);
	error_vformat_msg(this, tnt_errcode_desc(m_errcode), ap);
	va_end(ap);
}

struct error *
BuildClientError(const char *file, unsigned line, uint32_t errcode, ...)
{
	try {
		ClientError *e = new ClientError(file, line, ER_UNKNOWN);
		va_list ap;
		va_start(ap, errcode);
		error_vformat_msg(e, tnt_errcode_desc(errcode), ap);
		va_end(ap);
		e->m_errcode = errcode;
		return e;
	} catch (OutOfMemory *e) {
		return e;
	}
}

struct error *
BuildLoggedError(const char *file, unsigned line, uint32_t errcode, ...)
{
	try {
		LoggedError *e = new LoggedError(file, line, ER_UNKNOWN);
		va_list ap;
		va_start(ap, errcode);
		error_vformat_msg(e, tnt_errcode_desc(errcode), ap);
		va_end(ap);
		e->m_errcode = errcode;
		return e;
	} catch (OutOfMemory *e) {
		return e;
	}
}

void
ClientError::log() const
{
	say_file_line(S_ERROR, file, line, errmsg, "%s",
		      tnt_errcode_str(m_errcode));
}


uint32_t
ClientError::get_errcode(const struct error *e)
{
	ClientError *client_error = type_cast(ClientError, e);
	if (client_error)
		return client_error->errcode();
	if (type_cast(OutOfMemory, e))
		return ER_MEMORY_ISSUE;
	if (type_cast(SystemError, e))
		return ER_SYSTEM;
	if (type_cast(CollationError, e))
		return ER_CANT_CREATE_COLLATION;
	return ER_PROC_LUA;
}

const struct type_info type_XlogError = make_type("XlogError", &type_Exception);

struct error *
BuildXlogError(const char *file, unsigned line, const char *format, ...)
{
	try {
		va_list ap;
		va_start(ap, format);
		XlogError *e = new XlogError(file, line, format, ap);
		va_end(ap);
		return e;
	} catch (OutOfMemory *e) {
		return e;
	}
}

const struct type_info type_XlogGapError =
	make_type("XlogGapError", &type_XlogError);

XlogGapError::XlogGapError(const char *file, unsigned line,
			   const struct vclock *from, const struct vclock *to)
		: XlogError(&type_XlogGapError, file, line)
{
	const char *s_from = vclock_to_string(from);
	const char *s_to = vclock_to_string(to);
	snprintf(errmsg, sizeof(errmsg),
		 "Missing .xlog file between LSN %lld %s and %lld %s",
		 (long long) vclock_sum(from), s_from ? s_from : "",
		 (long long) vclock_sum(to), s_to ? s_to : "");
}

struct error *
BuildXlogGapError(const char *file, unsigned line,
		  const struct vclock *from, const struct vclock *to)
{
	try {
		return new XlogGapError(file, line, from, to);
	} catch (OutOfMemory *e) {
		return e;
	}
}

struct rlist on_access_denied = RLIST_HEAD_INITIALIZER(on_access_denied);

union value get_access_type(struct error *e) {
	AccessDeniedError *ade = (AccessDeniedError *)e;
	union value retval = {
		._char = ade->access_type()
	};
	return retval;
}

union value get_object_type(struct error *e) {
	AccessDeniedError *ade = (AccessDeniedError *)e;
	union value retval = {
		._char = ade->object_type()
	};
	return retval;
}

union value get_object_name(struct error *e) {
	AccessDeniedError *ade = (AccessDeniedError *)e;
	union value retval = {
		._char = ade->object_name()
	};
	return retval;
}

static struct field accessdeniederror_fields[] = {
	make_field("access_type", CTYPE_CONST_CHAR_PTR, &get_access_type),
	make_field("object_type", CTYPE_CONST_CHAR_PTR, &get_object_type),
	make_field("object_name", CTYPE_CONST_CHAR_PTR, &get_object_name),
	FIELDS_SENTINEL
};

const struct type_info type_AccessDeniedError =
	make_type_with_fields("AccessDeniedError", &type_ClientError,
		  accessdeniederror_fields);

AccessDeniedError::AccessDeniedError(const char *file, unsigned int line,
				     const char *access_type,
				     const char *object_type,
				     const char *object_name,
				     const char *user_name)
	:ClientError(&type_AccessDeniedError, file, line, ER_ACCESS_DENIED)
{
	error_format_msg(this, tnt_errcode_desc(m_errcode),
			 access_type, object_type, object_name, user_name);

	struct on_access_denied_ctx ctx = {access_type, object_type, object_name};
	trigger_run(&on_access_denied, (void *) &ctx);
	/*
	 * We want to use ctx parameters as error parameters
	 * later, so we have to alloc space for it.
	 * As m_access_type and m_object_type are constant
	 * literals they are statically  allocated. We must copy
	 * only m_object_name.
	 */
	m_object_type = object_type;
	m_access_type = access_type;
	m_object_name = strdup(object_name);
}

struct error *
BuildAccessDeniedError(const char *file, unsigned int line,
		       const char *access_type, const char *object_type,
		       const char *object_name,
		       const char *user_name)
{
	try {
		return new AccessDeniedError(file, line, access_type,
					     object_type, object_name,
					     user_name);
	} catch (OutOfMemory *e) {
		return e;
	}
}
