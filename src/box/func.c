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
#include "func.h"
#include "fiber.h"
#include "assoc.h"
#include "lua/call.h"
#include "errinj.h"
#include "diag.h"
#include "schema.h"
#include "session.h"

/**
 * Parsed symbol and package names.
 */
struct func_name {
	/** Null-terminated symbol name, e.g. "func" for "mod.submod.func" */
	const char *sym;
	/** Package name, e.g. "mod.submod" for "mod.submod.func" */
	const char *package;
	/** A pointer to the last character in ->package + 1 */
	const char *package_end;
};

/**
 * Dynamic shared module.
 */
struct box_module {
	/** Low level module instance. */
	struct module *module;
	/** List of imported functions. */
	struct rlist funcs;
	/** Number of active references. */
	int64_t refs;
};

struct func_c {
	/** Function object base class. */
	struct func base;
	/**
	 * Anchor for module membership.
	 */
	struct rlist item;
	/**
	 * Back reference to a cached module.
	 */
	struct box_module *bxmod;
	/**
	 * C function to call.
	 */
	struct module_func mf;
};

/** Hash from module name to its box_module instance. */
static struct mh_strnptr_t *box_module_hash = NULL;

/***
 * Split function name to symbol and package names.
 * For example, str = foo.bar.baz => sym = baz, package = foo.bar
 * @param str function name, e.g. "module.submodule.function".
 * @param[out] name parsed symbol and package names.
 */
static void
func_split_name(const char *str, struct func_name *name)
{
	name->package = str;
	name->package_end = strrchr(str, '.');
	if (name->package_end != NULL) {
		/* module.submodule.function => module.submodule, function */
		name->sym = name->package_end + 1; /* skip '.' */
	} else {
		/* package == function => function, function */
		name->sym = name->package;
		name->package_end = str + strlen(str);
	}
}

/** Initialize box module subsystem. */
int
box_module_init(void)
{
	box_module_hash = mh_strnptr_new();
	if (box_module_hash == NULL) {
		diag_set(OutOfMemory, sizeof(*box_module_hash),
			 "malloc", "box modules hash table");
		return -1;
	}
	return 0;
}

/**
 * Look up a module in the modules cache.
 */
static struct box_module *
cache_find(const char *name, const char *name_end)
{
	mh_int_t i = mh_strnptr_find_inp(box_module_hash, name,
					 name_end - name);
	if (i == mh_end(box_module_hash))
		return NULL;
	return mh_strnptr_node(box_module_hash, i)->val;
}

/**
 * Save module to the module cache.
 */
static int
cache_put(struct box_module *bxmod)
{
	const char *package = bxmod->module->package;
	size_t package_len = bxmod->module->package_len;

	const struct mh_strnptr_node_t strnode = {
		.str = package,
		.len = package_len,
		.hash = mh_strn_hash(package, package_len),
		.val = bxmod
	};

	mh_int_t i = mh_strnptr_put(box_module_hash, &strnode, NULL, NULL);
	if (i == mh_end(box_module_hash)) {
		diag_set(OutOfMemory, sizeof(strnode),
			 "malloc", "box_module_hash");
		return -1;
	}
	return 0;
}

/**
 * Update module in module cache.
 */
static void
cache_update(struct box_module *bxmod)
{
	const char *str = bxmod->module->package;
	size_t len = bxmod->module->package_len;

	mh_int_t e = mh_strnptr_find_inp(box_module_hash, str, len);
	if (e == mh_end(box_module_hash))
		panic("func: failed to update cache: %s", str);

	mh_strnptr_node(box_module_hash, e)->str = str;
	mh_strnptr_node(box_module_hash, e)->val = bxmod;
}

/**
 * Delete a module from the module cache
 */
static void
cache_del(struct box_module *bxmod)
{
	const char *str = bxmod->module->package;
	size_t len = bxmod->module->package_len;

	mh_int_t e = mh_strnptr_find_inp(box_module_hash, str, len);
	if (e != mh_end(box_module_hash)) {
		struct box_module *v;
		v = mh_strnptr_node(box_module_hash, e)->val;
		if (v == bxmod)
			mh_strnptr_del(box_module_hash, e, NULL);
	}
}

/** Increment reference to a module. */
static inline void
box_module_ref(struct box_module *bxmod)
{
	assert(bxmod->refs >= 0);
	++bxmod->refs;
}

/** Low-level module loader. */
static struct box_module *
box_module_ld(const char *package, size_t package_len,
	      struct module *(ld)(const char *package,
				  size_t package_len))
{
	struct module *m = ld(package, package_len);
	if (m == NULL)
		return NULL;

	struct box_module *bxmod = malloc(sizeof(*bxmod));
	if (bxmod == NULL) {
		module_unload(m);
		diag_set(OutOfMemory, sizeof(*bxmod) + package_len + 1,
			 "malloc", "struct box_module");
		return NULL;
	}

	bxmod->refs = 0;
	bxmod->module = m;
	rlist_create(&bxmod->funcs);

	box_module_ref(bxmod);
	return bxmod;
}

/** Load a new module. */
static struct box_module *
box_module_load(const char *package, size_t package_len)
{
	return box_module_ld(package, package_len,
			     module_load);
}

/** Load a new module with force cache invalidation. */
static struct box_module *
box_module_load_force(const char *package, size_t package_len)
{
	return box_module_ld(package, package_len,
			     module_load_force);
}

/** Delete a module. */
static void
box_module_delete(struct box_module *bxmod)
{
	struct errinj *e = errinj(ERRINJ_DYN_MODULE_COUNT, ERRINJ_INT);
	if (e != NULL)
		--e->iparam;
	module_unload(bxmod->module);
	TRASH(bxmod);
	free(bxmod);
}

/** Decrement reference to a module and delete it if last one. */
static inline void
box_module_unref(struct box_module *bxmod)
{
	assert(bxmod->refs > 0);
	if (--bxmod->refs == 0) {
		cache_del(bxmod);
		box_module_delete(bxmod);
	}
}

/** Free box modules subsystem. */
void
box_module_free(void)
{
	while (mh_size(box_module_hash) > 0) {
		struct box_module *bxmod;

		mh_int_t i = mh_first(box_module_hash);
		bxmod = mh_strnptr_node(box_module_hash, i)->val;
		/*
		 * Module won't be deleted if it has
		 * active functions bound.
		 */
		box_module_unref(bxmod);
	}
	mh_strnptr_delete(box_module_hash);
}

static struct func_vtab func_c_vtab;

/** Create new box function. */
static inline void
func_c_create(struct func_c *func_c)
{
	func_c->bxmod = NULL;
	func_c->base.vtab = &func_c_vtab;
	rlist_create(&func_c->item);
	module_func_create(&func_c->mf);
}

/** Test if function is not resolved. */
static inline bool
is_func_c_emtpy(struct func_c *func_c)
{
	return is_module_func_empty(&func_c->mf);
}

/** Load a new function. */
static inline int
func_c_load(struct box_module *bxmod, const char *func_name,
	      struct func_c *func_c)
{
	int rc = module_func_load(bxmod->module, func_name, &func_c->mf);
	if (rc == 0) {
		rlist_move(&bxmod->funcs, &func_c->item);
		func_c->bxmod = bxmod;
		box_module_ref(bxmod);
	}
	return rc;
}

/** Unload a function. */
static inline void
func_c_unload(struct func_c *func_c)
{
	module_func_unload(&func_c->mf);
	rlist_del(&func_c->item);
	box_module_unref(func_c->bxmod);
	func_c_create(func_c);
}

/** Reload module in a force way. */
int
box_module_reload(const char *package, const char *package_end)
{
	struct box_module *bxmod_old = cache_find(package, package_end);
	if (bxmod_old == NULL) {
		/* Module wasn't loaded - do nothing. */
		diag_set(ClientError, ER_NO_SUCH_MODULE, package);
		return -1;
	}

	size_t len = package_end - package;
	struct box_module *bxmod = box_module_load_force(package, len);
	if (bxmod == NULL)
		return -1;

	struct func_c *func, *tmp;
	rlist_foreach_entry_safe(func, &bxmod_old->funcs, item, tmp) {
		struct func_name name;
		func_split_name(func->base.def->name, &name);
		func_c_unload(func);
		if (func_c_load(bxmod, name.sym, func) != 0)
			goto restore;
	}

	cache_update(bxmod);
	box_module_unref(bxmod_old);
	return 0;

restore:
	/*
	 * Some old-dso func can't be load from new module, restore old
	 * functions.
	 */
	do {
		struct func_name name;
		func_split_name(func->base.def->name, &name);

		if (func_c_load(bxmod_old, name.sym, func) != 0) {
			/*
			 * Something strange was happen, an early loaden
			 * function was not found in an old dso.
			 */
			panic("Can't restore module function, "
			      "server state is inconsistent");
		}
	} while (func != rlist_first_entry(&bxmod_old->funcs,
					   struct func_c, item));
	assert(rlist_empty(&bxmod->funcs));
	box_module_unref(bxmod);
	return -1;
}

static struct func *
func_c_new(struct func_def *def);

/** Construct a SQL builtin function object. */
extern struct func *
func_sql_builtin_new(struct func_def *def);

/** Allocate a new function. */
struct func *
func_new(struct func_def *def)
{
	struct func *func;
	switch (def->language) {
	case FUNC_LANGUAGE_C:
		func = func_c_new(def);
		break;
	case FUNC_LANGUAGE_LUA:
		func = func_lua_new(def);
		break;
	case FUNC_LANGUAGE_SQL_BUILTIN:
		func = func_sql_builtin_new(def);
		break;
	default:
		unreachable();
	}
	if (func == NULL)
		return NULL;
	func->def = def;
	/** Nobody has access to the function but the owner. */
	memset(func->access, 0, sizeof(func->access));
	/*
	 * Do not initialize the privilege cache right away since
	 * when loading up a function definition during recovery,
	 * user cache may not be filled up yet (space _user is
	 * recovered after space _func), so no user cache entry
	 * may exist yet for such user.  The cache will be filled
	 * up on demand upon first access.
	 *
	 * Later on consistency of the cache is ensured by DDL
	 * checks (see user_has_data()).
	 */
	credentials_create_empty(&func->owner_credentials);
	return func;
}

/** Create new C function. */
static struct func *
func_c_new(MAYBE_UNUSED struct func_def *def)
{
	assert(def->language == FUNC_LANGUAGE_C);
	assert(def->body == NULL && !def->is_sandboxed);
	struct func_c *func_c = malloc(sizeof(struct func_c));
	if (func_c == NULL) {
		diag_set(OutOfMemory, sizeof(*func_c), "malloc", "func_c");
		return NULL;
	}
	func_c_create(func_c);
	return &func_c->base;
}

/** Destroy C function. */
static void
func_c_destroy(struct func *base)
{
	assert(base->vtab == &func_c_vtab);
	assert(base != NULL && base->def->language == FUNC_LANGUAGE_C);
	struct func_c *func_c = (struct func_c *) base;
	box_module_unref(func_c->bxmod);
	func_c_unload(func_c);
	TRASH(base);
	free(func_c);
}

/**
 * Resolve func->func (find the respective share library and fetch the
 * symbol from it).
 */
static int
func_c_resolve(struct func_c *func_c)
{
	assert(is_func_c_emtpy(func_c));

	struct func_name name;
	func_split_name(func_c->base.def->name, &name);

	struct box_module *cached, *bxmod;
	cached = cache_find(name.package, name.package_end);
	if (cached == NULL) {
		size_t len = name.package_end - name.package;
		bxmod = box_module_load(name.package, len);
		if (bxmod == NULL)
			return -1;
		if (cache_put(bxmod) != 0) {
			box_module_delete(bxmod);
			return -1;
		}
	} else {
		bxmod = cached;
	}

	if (func_c_load(bxmod, name.sym, func_c) != 0) {
		if (cached == NULL) {
			/*
			 * In case if it was a first load we should
			 * clean the cache immediately otherwise
			 * the module continue being referenced even
			 * if there will be no use of it.
			 *
			 * Note the box_module_sym set an error thus be
			 * careful to not wipe it.
			 */
			cache_del(bxmod);
			box_module_delete(bxmod);
		}
		return -1;
	}
	return 0;
}

/** Execute C function. */
static int
func_c_call(struct func *base, struct port *args, struct port *ret)
{
	assert(base->vtab == &func_c_vtab);
	assert(base != NULL && base->def->language == FUNC_LANGUAGE_C);
	struct func_c *func_c = (struct func_c *) base;
	if (is_func_c_emtpy(func_c)) {
		if (func_c_resolve(func_c) != 0)
			return -1;
	}

	return module_func_call(&func_c->mf, args, ret);
}

static struct func_vtab func_c_vtab = {
	.call = func_c_call,
	.destroy = func_c_destroy,
};

void
func_delete(struct func *func)
{
	struct func_def *def = func->def;
	credentials_destroy(&func->owner_credentials);
	func->vtab->destroy(func);
	free(def);
}

/** Check "EXECUTE" permissions for a given function. */
static int
func_access_check(struct func *func)
{
	struct credentials *credentials = effective_user();
	/*
	 * If the user has universal access, don't bother with
	 * checks. No special check for ADMIN user is necessary
	 * since ADMIN has universal access.
	 */
	if ((credentials->universal_access & (PRIV_X | PRIV_U)) ==
	    (PRIV_X | PRIV_U))
		return 0;
	user_access_t access = PRIV_X | PRIV_U;
	/* Check access for all functions. */
	access &= ~entity_access_get(SC_FUNCTION)[credentials->auth_token].effective;
	user_access_t func_access = access & ~credentials->universal_access;
	if ((func_access & PRIV_U) != 0 ||
	    (func->def->uid != credentials->uid &&
	     func_access & ~func->access[credentials->auth_token].effective)) {
		/* Access violation, report error. */
		struct user *user = user_find(credentials->uid);
		if (user != NULL) {
			diag_set(AccessDeniedError, priv_name(PRIV_X),
				 schema_object_name(SC_FUNCTION),
				 func->def->name, user->def->name);
		}
		return -1;
	}
	return 0;
}

int
func_call(struct func *base, struct port *args, struct port *ret)
{
	if (func_access_check(base) != 0)
		return -1;
	/**
	 * Change the current user id if the function is
	 * a set-definer-uid one. If the function is not
	 * defined, it's obviously not a setuid one.
	 */
	struct credentials *orig_credentials = NULL;
	if (base->def->setuid) {
		orig_credentials = effective_user();
		/* Remember and change the current user id. */
		if (credentials_is_empty(&base->owner_credentials)) {
			/*
			 * Fill the cache upon first access, since
			 * when func is created, no user may
			 * be around to fill it (recovery of
			 * system spaces from a snapshot).
			 */
			struct user *owner = user_find(base->def->uid);
			if (owner == NULL)
				return -1;
			credentials_reset(&base->owner_credentials, owner);
		}
		fiber_set_user(fiber(), &base->owner_credentials);
	}
	int rc = base->vtab->call(base, args, ret);
	/* Restore the original user */
	if (orig_credentials)
		fiber_set_user(fiber(), orig_credentials);
	return rc;
}
