#include <assert.h>

#include "ffi/lj_ctype.h"
#include "ffi/lj_cdata.h"
#include "ffi/lj_cconv.h"
#include "lj_tab.h"
#include "lj_obj.h"
#include "uj_state.h"
#include "uj_lib.h"
#include "uj_meta.h"
#include "uj_capi_impl.h"

#include "lmisclib.h"

void *
luaM_pushcdata(struct lua_State *L, uint32_t ctypeid)
{
	/*
	 * ctypeid is actually has CTypeID type.
	 * CTypeId is defined inside lj_ctype.h
	 */
	assert(sizeof(ctypeid) == sizeof(CTypeID));

	/* Code below is based on ffi_new() from luajit/src/lib_ffi.c */

	/* Get information about ctype */
	CTSize size;
	CTState *cts = ctype_cts(L);
	CTInfo info = lj_ctype_info(cts, ctypeid, &size);
	assert(size != CTSIZE_INVALID);

	/* Allocate a new cdata */
	GCcdata *cd = lj_cdata_new(cts, ctypeid, size);

	/* Anchor the uninitialized cdata with the stack. */
	TValue *o = L->top;
	setcdataV(L, o, cd);
	uj_state_stack_incr_top(L);

	/*
	 * lj_cconv_ct_init is omitted for non-structs because it actually
	 * does memset()
	 * Caveats: cdata memory is returned uninitialized
	 */
	if (ctype_isstruct(info)) {
		/* Initialize cdata. */
		CType *ct = ctype_raw(cts, ctypeid);
		lj_cconv_ct_init(cts, ct, size, cdataptr(cd), o,
				 (size_t)(L->top - o));
		/* Handle ctype __gc metamethod. Use the fast lookup here. */
		const TValue *tv = lj_tab_getinth(cts->miscmap, -(int32_t)ctypeid);
		if (tv && tvistab(tv) && (tv = uj_meta_lookup_mt(G(L), tabV(tv), MM_gc))) {
			GCtab *t = cts->finalizer;
			if (t->metatable) {
				/* Add to finalizer table, if still enabled. */
				copyTV(L, lj_tab_set(L, t, o), tv);
				lj_gc_anybarriert(L, t);
				cd->marked |= LJ_GC_CDATA_FIN;
			}
		}
	}

	lj_gc_check(L);
	return cdataptr(cd);
}

void
luaM_setcdatagc(struct lua_State *L, int idx)
{
	/* Calculate absolute value in the stack. */
	if (idx < 0)
		idx = lua_gettop(L) + idx + 1;

	/* Code below is based on ffi_gc() from luajit/src/lib_ffi.c */

	/* Get cdata from the stack */
	assert(lua_type(L, idx) == LUA_TCDATA);
	GCcdata *cd = cdataV(L->base + idx - 1);

	/* Get finalizer from the stack */
	TValue *fin = uj_lib_checkany(L, lua_gettop(L));
	CTState *cts = ctype_cts(L);
	GCtab *t = cts->finalizer;

#if !defined(NDEBUG)
	CType *ct = ctype_raw(cts, cd->ctypeid);
	(void) ct;
	assert(ctype_isptr(ct->info) || ctype_isstruct(ct->info) ||
	       ctype_isrefarray(ct->info));
#endif /* !defined(NDEBUG) */

	/* Set finalizer */
	if (t->metatable != NULL) {  /* Update finalizer table, if still enabled. */
		copyTV(L, lj_tab_set(L, t, L->base + idx - 1), fin);
		lj_gc_anybarriert(L, t);
		if (!tvisnil(fin))
			cd->marked |= LJ_GC_CDATA_FIN;
		else
			cd->marked &= ~LJ_GC_CDATA_FIN;
	}

	/* Pop finalizer */
	lua_pop(L, 1);
}

/* Based on ffi_meta___call() from luajit/src/lib_ffi.c. */
int
luaM_cdata_hasmm(lua_State *L, int idx, MMS metamethod)
{
	/* Calculate absolute value in the stack. */
	if (idx < 0)
		idx = lua_gettop(L) + idx + 1;

	/* Get cdata from the stack. */
	assert(lua_type(L, idx) == LUA_TCDATA);
	GCcdata *cd = cdataV(L->base + idx - 1);

	CTState *cts = ctype_cts(L);
	CTypeID id = cd->ctypeid;
	CType *ct = ctype_raw(cts, id);
	if (ctype_isptr(ct->info))
		id = ctype_cid(ct->info);

	/* Get ctype metamethod. */
	const TValue *tv = lj_ctype_meta(cts, id, metamethod);

	return tv != NULL;
}

int
luaM_pushthread1(lua_State *L, lua_State *L1)
{
	setthreadV(L, L->top, L1);
	uj_state_stack_incr_top(L);
	return mainthread(G(L)) == L1;
}

uint32_t
lua_hashstring(lua_State *L, int idx)
{
	TValue *o = uj_capi_index2adr(L, idx);
	lua_assert(tvisstr(o));
	GCstr *s = strV(o);
	return s->hash;
}

