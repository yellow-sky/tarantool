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

/*
 * This file contains the implementation for TRIGGERs
 */

#include "box/box.h"
#include "box/schema.h"
#include "box/trigger_def.h"
#include "sqlInt.h"
#include "tarantoolInt.h"
#include "vdbeInt.h"

/* See comment in sqlInt.h */
int sqlSubProgramsRemaining;

/*
 * Delete a linked list of TriggerStep structures.
 */
void
sqlDeleteTriggerStep(sql * db, TriggerStep * pTriggerStep)
{
	while (pTriggerStep) {
		TriggerStep *pTmp = pTriggerStep;
		pTriggerStep = pTriggerStep->pNext;

		sql_expr_delete(db, pTmp->pWhere, false);
		sql_expr_list_delete(db, pTmp->pExprList);
		sql_select_delete(db, pTmp->pSelect);
		sqlIdListDelete(db, pTmp->pIdList);

		sqlDbFree(db, pTmp);
	}
}

void
sql_store_trigger(struct Parse *parse)
{
	/* The database connection. */
	struct sql *db = parse->db;
	struct create_trigger_def *trigger_def = &parse->create_trigger_def;
	struct create_entity_def *create_def = &trigger_def->base;
	struct alter_entity_def *alter_def = &create_def->base;
	assert(alter_def->entity_type == ENTITY_TYPE_TRIGGER);
	assert(alter_def->alter_action == ALTER_ACTION_CREATE);

	assert(alter_def->entity_name->nSrc == 1);
	assert(create_def->name.n > 0);
	sqlSrcListDelete(db, alter_def->entity_name);

	assert(parse->parsed_ast.trigger_expr == NULL);
	parse->parsed_ast_type = AST_TYPE_TRIGGER_EXPR;
	parse->parsed_ast.trigger_expr =
		sql_trigger_expr_new(db, trigger_def->cols, trigger_def->when,
				     trigger_def->step_list);
	if (parse->parsed_ast.trigger_expr == NULL)
		goto set_tarantool_error_and_cleanup;
	return;
set_tarantool_error_and_cleanup:
	parse->is_aborted = true;
	sqlIdListDelete(db, trigger_def->cols);
	sql_expr_delete(db, trigger_def->when, false);
	sqlDeleteTriggerStep(db, trigger_def->step_list);
}

struct TriggerStep *
sql_trigger_select_step(struct sql *db, struct Select *select)
{
	struct TriggerStep *trigger_step =
		sqlDbMallocZero(db, sizeof(struct TriggerStep));
	if (trigger_step == NULL) {
		sql_select_delete(db, select);
		diag_set(OutOfMemory, sizeof(struct TriggerStep),
			 "sqlDbMallocZero", "trigger_step");
		return NULL;
	}
	trigger_step->op = TK_SELECT;
	trigger_step->pSelect = select;
	trigger_step->orconf = ON_CONFLICT_ACTION_DEFAULT;
	return trigger_step;
}

/*
 * Allocate space to hold a new trigger step.  The allocated space
 * holds both the TriggerStep object and the TriggerStep.target.z
 * string.
 *
 * @param db The database connection.
 * @param op Trigger opcode.
 * @param target_name The target name token.
 * @retval Not NULL TriggerStep object on success.
 * @retval NULL Otherwise. The diag message is set.
 */
static struct TriggerStep *
sql_trigger_step_new(struct sql *db, u8 op, struct Token *target_name)
{
	int name_size = target_name->n + 1;
	int size = sizeof(struct TriggerStep) + name_size;
	struct TriggerStep *trigger_step = sqlDbMallocZero(db, size);
	if (trigger_step == NULL) {
		diag_set(OutOfMemory, size, "sqlDbMallocZero", "trigger_step");
		return NULL;
	}
	char *z = (char *)&trigger_step[1];
	int rc = sql_normalize_name(z, name_size, target_name->z,
				    target_name->n);
	if (rc > name_size) {
		name_size = rc;
		trigger_step = sqlDbReallocOrFree(db, trigger_step,
						  sizeof(*trigger_step) +
						  name_size);
		if (trigger_step == NULL)
			return NULL;
		z = (char *) &trigger_step[1];
		if (sql_normalize_name(z, name_size, target_name->z,
				       target_name->n) > name_size)
			unreachable();
	}
	trigger_step->zTarget = z;
	trigger_step->op = op;
	return trigger_step;
}

struct TriggerStep *
sql_trigger_insert_step(struct sql *db, struct Token *table_name,
			struct IdList *column_list, struct Select *select,
			enum on_conflict_action orconf)
{
	assert(select != NULL || db->mallocFailed);
	struct TriggerStep *trigger_step =
		sql_trigger_step_new(db, TK_INSERT, table_name);
	if (trigger_step != NULL) {
		trigger_step->pSelect =
			sqlSelectDup(db, select, EXPRDUP_REDUCE);
		trigger_step->pIdList = column_list;
		trigger_step->orconf = orconf;
	} else {
		sqlIdListDelete(db, column_list);
	}
	sql_select_delete(db, select);
	return trigger_step;
}

struct TriggerStep *
sql_trigger_update_step(struct sql *db, struct Token *table_name,
		        struct ExprList *new_list, struct Expr *where,
			enum on_conflict_action orconf)
{
	struct TriggerStep *trigger_step =
		sql_trigger_step_new(db, TK_UPDATE, table_name);
	if (trigger_step != NULL) {
		trigger_step->pExprList =
		    sql_expr_list_dup(db, new_list, EXPRDUP_REDUCE);
		trigger_step->pWhere = sqlExprDup(db, where, EXPRDUP_REDUCE);
		trigger_step->orconf = orconf;
	}
	sql_expr_list_delete(db, new_list);
	sql_expr_delete(db, where, false);
	return trigger_step;
}

struct TriggerStep *
sql_trigger_delete_step(struct sql *db, struct Token *table_name,
			struct Expr *where)
{
	struct TriggerStep *trigger_step =
		sql_trigger_step_new(db, TK_DELETE, table_name);
	if (trigger_step != NULL) {
		trigger_step->pWhere = sqlExprDup(db, where, EXPRDUP_REDUCE);
		trigger_step->orconf = ON_CONFLICT_ACTION_DEFAULT;
	}
	sql_expr_delete(db, where, false);
	return trigger_step;
}

struct sql_trigger_expr *
sql_trigger_expr_new(struct sql *db, struct IdList *cols, struct Expr *when,
		     struct TriggerStep *step_list)
{
	struct sql_trigger_expr *trigger_expr =
		(struct sql_trigger_expr *)malloc(sizeof(*trigger_expr));
	if (trigger_expr == NULL) {
		diag_set(OutOfMemory, sizeof(*trigger_expr),
			 "malloc", "trigger_expr");
		return NULL;
	}
	if (when != NULL) {
		trigger_expr->when = sqlExprDup(db, when, EXPRDUP_REDUCE);
		if (trigger_expr->when == NULL) {
			diag_set(OutOfMemory,
				 sql_expr_sizeof(when, EXPRDUP_REDUCE),
				 "sqlExprDup", "trigger_expr->when");
			free(trigger_expr);
			return NULL;
		}
	} else {
		trigger_expr->when = NULL;
	}
	/* Object refers to reduced copy of when expression. */
	sql_expr_delete(db, when, false);
	trigger_expr->cols = cols;
	trigger_expr->step_list = step_list;
	return trigger_expr;
}

void
sql_trigger_expr_delete(struct sql *db, struct sql_trigger_expr *trigger_expr)
{
	sqlDeleteTriggerStep(db, trigger_expr->step_list);
	sql_expr_delete(db, trigger_expr->when, false);
	sqlIdListDelete(db, trigger_expr->cols);
	free(trigger_expr);
}

static struct trigger_vtab sql_trigger_vtab;

struct sql_trigger *
sql_trigger_new(struct trigger_def *def, struct sql_trigger_expr *expr,
		bool is_fk_constraint_trigger)
{
	struct sql_trigger *trigger = malloc(sizeof(*trigger));
	if (trigger == NULL) {
		diag_set(OutOfMemory, sizeof(*trigger), "malloc", "trigger");
		return NULL;
	}
	if (expr == NULL) {
		assert(def->code != NULL);
		expr = sql_trigger_expr_compile(sql_get(), def->code);
		if (expr == NULL) {
			free(trigger);
			return NULL;
		}
	}
	rlist_create(&trigger->base.link);
	trigger->base.def = def;
	trigger->base.vtab = &sql_trigger_vtab;
	trigger->expr = expr;
	trigger->is_fk_constraint_trigger = is_fk_constraint_trigger;
	return trigger;
}

static void
sql_trigger_delete(struct trigger *base)
{
	assert(base->vtab == &sql_trigger_vtab);
	assert(base != NULL && base->def->language == TRIGGER_LANGUAGE_SQL);
	struct sql_trigger *trigger = (struct sql_trigger *) base;
	sql_trigger_expr_delete(sql_get(), trigger->expr);
	free(trigger);
}

static struct trigger_vtab sql_trigger_vtab = {
	.destroy = sql_trigger_delete,
};

void
vdbe_code_drop_trigger(struct Parse *parser, const char *trigger_name,
		       bool account_changes)
{
	sql *db = parser->db;
	struct Vdbe *v = sqlGetVdbe(parser);
	if (v == NULL)
		return;
	/*
	 * Generate code to delete entry from _trigger and
	 * internal SQL structures.
	 */
	int trig_name_reg = ++parser->nMem;
	int record_to_delete = ++parser->nMem;
	sqlVdbeAddOp4(v, OP_String8, 0, trig_name_reg, 0,
			  sqlDbStrDup(db, trigger_name), P4_DYNAMIC);
	sqlVdbeAddOp3(v, OP_MakeRecord, trig_name_reg, 1,
			  record_to_delete);
	sqlVdbeAddOp2(v, OP_SDelete, BOX_TRIGGER_ID,
			  record_to_delete);
	if (account_changes)
		sqlVdbeChangeP5(v, OPFLAG_NCHANGE);
}

void
sql_drop_trigger(struct Parse *parser)
{
	struct drop_entity_def *drop_def = &parser->drop_trigger_def.base;
	struct alter_entity_def *alter_def = &drop_def->base;
	assert(alter_def->entity_type == ENTITY_TYPE_TRIGGER);
	assert(alter_def->alter_action == ALTER_ACTION_DROP);
	struct SrcList *name = alter_def->entity_name;
	bool no_err = drop_def->if_exist;
	sql *db = parser->db;
	if (db->mallocFailed)
		goto drop_trigger_cleanup;

	struct Vdbe *v = sqlGetVdbe(parser);
	if (v != NULL)
		sqlVdbeCountChanges(v);

	assert(name->nSrc == 1);
	const char *trigger_name = name->a[0].zName;
	const char *error_msg =
		tt_sprintf(tnt_errcode_desc(ER_NO_SUCH_TRIGGER),
			   trigger_name);
	char *name_copy = sqlDbStrDup(db, trigger_name);
	if (name_copy == NULL)
		goto drop_trigger_cleanup;
	int name_reg = ++parser->nMem;
	sqlVdbeAddOp4(v, OP_String8, 0, name_reg, 0, name_copy, P4_DYNAMIC);
	if (vdbe_emit_halt_with_presence_test(parser, BOX_TRIGGER_ID, 0,
					      name_reg, 1, ER_NO_SUCH_TRIGGER,
					      error_msg, no_err, OP_Found) != 0)
		goto drop_trigger_cleanup;

	vdbe_code_drop_trigger(parser, trigger_name, true);

 drop_trigger_cleanup:
	sqlSrcListDelete(db, name);
}

/*
 * pEList is the SET clause of an UPDATE statement.  Each entry
 * in pEList is of the format <id>=<expr>.  If any of the entries
 * in pEList have an <id> which matches an identifier in pIdList,
 * then return TRUE.  If pIdList==NULL, then it is considered a
 * wildcard that matches anything.  Likewise if pEList==NULL then
 * it matches anything so always return true.  Return false only
 * if there is no match.
 */
static int
checkColumnOverlap(IdList * pIdList, ExprList * pEList)
{
	int e;
	if (pIdList == 0 || NEVER(pEList == 0))
		return 1;
	for (e = 0; e < pEList->nExpr; e++) {
		if (sqlIdListIndex(pIdList, pEList->a[e].zName) >= 0)
			return 1;
	}
	return 0;
}

struct rlist *
sql_triggers_exist(struct space *space,
		   enum trigger_event_manipulation event_manipulation,
		   struct ExprList *changes_list, uint32_t sql_flags,
		   int *mask_ptr)
{
	int mask = 0;
	struct rlist *trigger_list = NULL;
	if ((sql_flags & SQL_EnableTrigger) != 0)
		trigger_list = &space->trigger_list;
	struct trigger *p;
	rlist_foreach_entry(p, trigger_list, link) {
		struct sql_trigger *trigger = (struct sql_trigger *) p;
		if (p->def->event_manipulation == event_manipulation &&
		    checkColumnOverlap(trigger->expr->cols, changes_list) != 0)
			mask |= (1 << p->def->action_timing);
	}
	if (mask_ptr != NULL)
		*mask_ptr = mask;
	return mask != 0 ? trigger_list : NULL;
}

/*
 * Convert the pStep->zTarget string into a SrcList and return a pointer
 * to that SrcList.
 */
static SrcList *
targetSrcList(Parse * pParse,	/* The parsing context */
	      TriggerStep * pStep	/* The trigger containing the target token */
    )
{
	sql *db = pParse->db;
	SrcList *pSrc;		/* SrcList to be returned */

	pSrc = sql_src_list_append(db, 0, 0);
	if (pSrc == NULL) {
		pParse->is_aborted = true;
		return NULL;
	}
	assert(pSrc->nSrc > 0);
	pSrc->a[pSrc->nSrc - 1].zName = sqlDbStrDup(db, pStep->zTarget);
	return pSrc;
}

/*
 * Generate VDBE code for the statements inside the body of a single
 * trigger.
 */
static int
codeTriggerProgram(Parse * pParse,	/* The parser context */
		   TriggerStep * pStepList,	/* List of statements inside the trigger body */
		   int orconf	/* Conflict algorithm.
				 * (ON_CONFLICT_ACTION_ABORT,
				 * etc)
				 */
    )
{
	TriggerStep *pStep;
	Vdbe *v = pParse->pVdbe;
	sql *db = pParse->db;

	assert(pParse->triggered_space != NULL && pParse->pToplevel != NULL);
	assert(pStepList);
	assert(v != 0);

	/* Tarantool: check if compile chain is not too long.  */
	sqlSubProgramsRemaining--;

	if (sqlSubProgramsRemaining == 0) {
		diag_set(ClientError, ER_SQL_PARSER_GENERIC, "Maximum number "\
			 "of chained trigger activations exceeded.");
		pParse->is_aborted = true;
	}

	for (pStep = pStepList; pStep; pStep = pStep->pNext) {
		/* Figure out the ON CONFLICT policy that will be used for this step
		 * of the trigger program. If the statement that caused this trigger
		 * to fire had an explicit ON CONFLICT, then use it. Otherwise, use
		 * the ON CONFLICT policy that was specified as part of the trigger
		 * step statement. Example:
		 *
		 *   CREATE TRIGGER AFTER INSERT ON t1 BEGIN;
		 *     INSERT OR REPLACE INTO t2 VALUES(new.a, new.b);
		 *   END;
		 *
		 *   INSERT INTO t1 ... ;            -- insert into t2 uses REPLACE policy
		 *   INSERT OR IGNORE INTO t1 ... ;  -- insert into t2 uses IGNORE policy
		 */
		pParse->eOrconf =
		    (orconf == ON_CONFLICT_ACTION_DEFAULT) ? pStep->orconf : (u8) orconf;

		switch (pStep->op) {
		case TK_UPDATE:{
				sqlUpdate(pParse,
					      targetSrcList(pParse, pStep),
					      sql_expr_list_dup(db,
								pStep->pExprList,
								0),
					      sqlExprDup(db, pStep->pWhere,
							     0),
					      pParse->eOrconf);
				break;
			}
		case TK_INSERT:{
				sqlInsert(pParse,
					      targetSrcList(pParse, pStep),
					      sqlSelectDup(db,
							       pStep->pSelect,
							       0),
					      sqlIdListDup(db,
							       pStep->pIdList),
					      pParse->eOrconf);
				break;
			}
		case TK_DELETE:{
				sql_table_delete_from(pParse,
						      targetSrcList(pParse, pStep),
						      sqlExprDup(db,
								     pStep->pWhere,
								     0)
				    );
				break;
			}
		default:
			assert(pStep->op == TK_SELECT); {
				SelectDest sDest;
				Select *pSelect =
				    sqlSelectDup(db, pStep->pSelect, 0);
				sqlSelectDestInit(&sDest, SRT_Discard, 0, -1);
				sqlSelect(pParse, pSelect, &sDest);
				sql_select_delete(db, pSelect);
				break;
			}
		}
		if (pStep->op != TK_SELECT) {
			sqlVdbeAddOp0(v, OP_ResetCount);
		}
	}

	/* Tarantool: check if compile chain is not too long.  */
	sqlSubProgramsRemaining++;
	return 0;
}

#ifdef SQL_ENABLE_EXPLAIN_COMMENTS
/*
 * This function is used to add VdbeComment() annotations to a VDBE
 * program. It is not used in production code, only for debugging.
 */
static const char *
onErrorText(int onError)
{
	switch (onError) {
	case ON_CONFLICT_ACTION_ABORT:
		return "abort";
	case ON_CONFLICT_ACTION_ROLLBACK:
		return "rollback";
	case ON_CONFLICT_ACTION_FAIL:
		return "fail";
	case ON_CONFLICT_ACTION_REPLACE:
		return "replace";
	case ON_CONFLICT_ACTION_IGNORE:
		return "ignore";
	case ON_CONFLICT_ACTION_DEFAULT:
		return "default";
	}
	return "n/a";
}
#endif

/**
 * Create and populate a new TriggerPrg object with a sub-program
 * implementing trigger pTrigger with ON CONFLICT policy orconf.
 *
 * @param parser Current parse context.
 * @param trigger sql_trigger to code.
 * @param space Trigger is attached to.
 * @param orconf ON CONFLICT policy to code trigger program with.
 *
 * @retval not NULL on success.
 * @retval NULL on error.
 */
static TriggerPrg *
sql_row_trigger_program(struct Parse *parser, struct sql_trigger *trigger,
			struct space *space, int orconf)
{
	Parse *pTop = sqlParseToplevel(parser);
	/* Database handle. */
	sql *db = parser->db;
	TriggerPrg *pPrg;	/* Value to return */
	Expr *pWhen = 0;	/* Duplicate of trigger WHEN expression */
	NameContext sNC;	/* Name context for sub-vdbe */
	SubProgram *pProgram = 0;	/* Sub-vdbe for trigger program */
	Parse *pSubParse;	/* Parse context for sub-vdbe */
	int iEndTrigger = 0;	/* Label to jump to if WHEN is false */

	assert(pTop->pVdbe);

	/* Allocate the TriggerPrg and SubProgram objects. To ensure that they
	 * are freed if an error occurs, link them into the Parse.pTriggerPrg
	 * list of the top-level Parse object sooner rather than later.
	 */
	pPrg = sqlDbMallocZero(db, sizeof(TriggerPrg));
	if (!pPrg)
		return 0;
	pPrg->pNext = pTop->pTriggerPrg;
	pTop->pTriggerPrg = pPrg;
	pPrg->pProgram = pProgram = sqlDbMallocZero(db, sizeof(SubProgram));
	if (!pProgram)
		return 0;
	sqlVdbeLinkSubProgram(pTop->pVdbe, pProgram);
	pPrg->trigger = trigger;
	pPrg->orconf = orconf;
	pPrg->column_mask[0] = COLUMN_MASK_FULL;
	pPrg->column_mask[1] = COLUMN_MASK_FULL;

	/*
	 * Allocate and populate a new Parse context to use for
	 * coding the trigger sub-program.
	 */
	pSubParse = sqlStackAllocZero(db, sizeof(Parse));
	if (!pSubParse)
		return 0;
	sql_parser_create(pSubParse, db, parser->sql_flags);
	memset(&sNC, 0, sizeof(sNC));
	sNC.pParse = pSubParse;
	pSubParse->triggered_space = space;
	pSubParse->pToplevel = pTop;
	pSubParse->trigger_event_manipulation =
		trigger->base.def->event_manipulation;
	pSubParse->nQueryLoop = parser->nQueryLoop;

	/* Temporary VM. */
	struct Vdbe *v = sqlGetVdbe(pSubParse);
	if (v != NULL) {
		VdbeComment((v, "Start: %s.%s (%s %s ON %s)",
			     trigger->base.def->name, onErrorText(orconf),
			     trigger_action_timing_strs[trigger->base.
							def->action_timing],
			      trigger_event_manipulation_strs[
						trigger->base.def->
						event_manipulation],
			      space->def->name));
		sqlVdbeChangeP4(v, -1,
				    sqlMPrintf(db, "-- TRIGGER %s",
					       trigger->base.def->name),
					       P4_DYNAMIC);

		/*
		 * If one was specified, code the WHEN clause. If
		 * it evaluates to false (or NULL) the sub-vdbe is
		 * immediately halted by jumping to the OP_Halt
		 * inserted at the end of the program.
		 */
		if (trigger->expr->when != NULL) {
			pWhen = sqlExprDup(db, trigger->expr->when, 0);
			if (0 == sqlResolveExprNames(&sNC, pWhen)
			    && db->mallocFailed == 0) {
				iEndTrigger = sqlVdbeMakeLabel(v);
				sqlExprIfFalse(pSubParse, pWhen,
						   iEndTrigger,
						   SQL_JUMPIFNULL);
			}
			sql_expr_delete(db, pWhen, false);
		}

		/* Code the trigger program into the sub-vdbe. */
		codeTriggerProgram(pSubParse, trigger->expr->step_list, orconf);

		/* Insert an OP_Halt at the end of the sub-program. */
		if (iEndTrigger)
			sqlVdbeResolveLabel(v, iEndTrigger);
		sqlVdbeAddOp0(v, OP_Halt);
		VdbeComment((v, "End: %s.%s", trigger->base.def->name,
			     onErrorText(orconf)));

		if (!parser->is_aborted)
			parser->is_aborted = pSubParse->is_aborted;
		if (db->mallocFailed == 0) {
			pProgram->aOp =
			    sqlVdbeTakeOpArray(v, &pProgram->nOp,
						   &pTop->nMaxArg);
		}
		pProgram->nMem = pSubParse->nMem;
		pProgram->nCsr = pSubParse->nTab;
		pProgram->token = (void *)trigger;
		pPrg->column_mask[0] = pSubParse->oldmask;
		pPrg->column_mask[1] = pSubParse->newmask;
		sqlVdbeDelete(v);
	}

	assert(!pSubParse->pTriggerPrg && !pSubParse->nMaxArg);
	sql_parser_destroy(pSubParse);
	sqlStackFree(db, pSubParse);

	return pPrg;
}

/**
 * Return a pointer to a TriggerPrg object containing the
 * sub-program for trigger with default ON CONFLICT algorithm
 * orconf. If no such TriggerPrg object exists, a new object is
 * allocated and populated before being returned.
 *
 * @param parser Current parse context.
 * @param trigger Trigger to code.
 * @param table table trigger is attached to.
 * @param orconf ON CONFLICT algorithm.
 *
 * @retval not NULL on success.
 * @retval NULL on error.
 */
static TriggerPrg *
sql_row_trigger(struct Parse *parser, struct sql_trigger *trigger,
		struct space *space, int orconf)
{
	Parse *pRoot = sqlParseToplevel(parser);
	TriggerPrg *pPrg;

	assert(trigger->base.def == NULL ||
	       space->def->id == trigger->base.def->space_id);

	/*
	 * It may be that this trigger has already been coded (or
	 * is in the process of being coded). If this is the case,
	 * then an entry with a matching TriggerPrg.pTrigger
	 * field will be present somewhere in the
	 * Parse.pTriggerPrg list. Search for such an entry.
	 */
	for (pPrg = pRoot->pTriggerPrg;
	     pPrg && (pPrg->trigger != trigger || pPrg->orconf != orconf);
	     pPrg = pPrg->pNext) ;

	/*
	 * If an existing TriggerPrg could not be located, create
	 * a new one.
	 */
	if (pPrg == NULL)
		pPrg = sql_row_trigger_program(parser, trigger, space, orconf);

	return pPrg;
}

void
vdbe_code_row_trigger_direct(struct Parse *parser, struct sql_trigger *trigger,
			     struct space *space, int reg, int orconf,
			     int ignore_jump)
{
	/* Main VM. */
	struct Vdbe *v = sqlGetVdbe(parser);

	TriggerPrg *pPrg = sql_row_trigger(parser, trigger, space, orconf);
	assert(pPrg != NULL || parser->is_aborted ||
	       parser->db->mallocFailed != 0);

	/*
	 * Code the OP_Program opcode in the parent VDBE. P4 of
	 * the OP_Program is a pointer to the sub-vdbe containing
	 * the trigger program.
	 */
	if (pPrg == NULL)
		return;

	bool is_recursive =
		!trigger->is_fk_constraint_trigger &&
		(parser->sql_flags & SQL_RecTriggers) == 0;

	sqlVdbeAddOp4(v, OP_Program, reg, ignore_jump,
			  ++parser->nMem, (const char *)pPrg->pProgram,
			  P4_SUBPROGRAM);
	VdbeComment((v, "Call: %s.%s", (trigger->base.def ?
			trigger->base.def->name : "fk_constraint"),
		     onErrorText(orconf)));

	/*
	 * Set the P5 operand of the OP_Program
	 * instruction to non-zero if recursive invocation
	 * of this trigger program is disallowed.
	 * Recursive invocation is disallowed if (a) the
	 * sub-program is really a trigger, not a foreign
	 * key action, and (b) the flag to enable
	 * recursive triggers is clear.
	 */
	sqlVdbeChangeP5(v, (u8)is_recursive);
}

void
vdbe_code_row_trigger(struct Parse *parser, struct rlist *trigger_list,
		      enum trigger_event_manipulation event_manipulation,
		      struct ExprList *changes_list,
		      enum trigger_action_timing action_timing,
		      struct space *space, int reg, int orconf, int ignore_jump)
{
	assert(action_timing == TRIGGER_ACTION_TIMING_BEFORE ||
	       action_timing == TRIGGER_ACTION_TIMING_AFTER);
	assert((event_manipulation == TRIGGER_EVENT_MANIPULATION_UPDATE) ==
	       (changes_list != NULL));
	if (trigger_list == NULL)
		return;

	struct trigger *p;
	rlist_foreach_entry(p, trigger_list, link) {
		/* Determine whether we should code trigger. */
		struct sql_trigger *trigger = (struct sql_trigger *) p;
		if (p->def->event_manipulation == event_manipulation &&
		    p->def->action_timing == action_timing &&
		    checkColumnOverlap(trigger->expr->cols, changes_list)) {
			vdbe_code_row_trigger_direct(parser, trigger, space,
						     reg, orconf, ignore_jump);
		}
	}
}

uint64_t
sql_trigger_colmask(Parse *parser, struct rlist *trigger_list,
		    ExprList *changes_list, int new, uint8_t action_timing_mask,
		    struct space *space, int orconf)
{
	enum trigger_event_manipulation event_manipulation =
		changes_list != NULL ? TRIGGER_EVENT_MANIPULATION_UPDATE :
				       TRIGGER_EVENT_MANIPULATION_DELETE;
	uint64_t mask = 0;
	if (trigger_list == NULL)
		return mask;

	assert(new == 1 || new == 0);
	struct trigger *p;
	rlist_foreach_entry(p, trigger_list, link) {
		struct sql_trigger *trigger = (struct sql_trigger *)p;
		if (p->def->event_manipulation == event_manipulation &&
		    ((action_timing_mask & (1 << p->def->action_timing)) != 0) &&
		    checkColumnOverlap(trigger->expr->cols, changes_list)) {
			TriggerPrg *prg =
				sql_row_trigger(parser, trigger, space, orconf);
			if (prg != NULL)
				mask |= prg->column_mask[new];
		}
	}

	return mask;
}
