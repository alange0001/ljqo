/*
 * debuggraph_rel.c
 *
 *   DebugGraph structures and functions. They are used to generate directed
 *   graphs for debug purposes.
 *
 * Copyright (C) 2009-2014, Adriano Lange
 *
 * This file is part of LJQO Plugin.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without a written agreement
 * is hereby granted, provided that the above copyright notice and this
 * paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL THE AUTHOR OR DISTRIBUTORS BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
 * LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS
 * DOCUMENTATION, EVEN IF THE AUTHOR OR DISTRIBUTORS HAVE BEEN ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * THE AUTHOR AND DISTRIBUTORS SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE AUTHOR AND DISTRIBUTORS HAS NO OBLIGATIONS TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

#include "debuggraph.h"
#include "debuggraph_rel.h"
#include <lib/stringinfo.h>
#include <parser/parsetree.h>
#include <optimizer/pathnode.h>
#include <utils/fmgrtab.h>
#include <utils/lsyscache.h>
#include <utils/syscache.h>
#include <optimizer/clauses.h>

static void add_relids(DebugNode *node, const char *name,
		PlannerInfo *root, Relids relids);
static DebugNode* get_restrictclauses(DebugGraph *graph, PlannerInfo *root,
		List *clauses);
static DebugNode* get_path(DebugGraph *graph, PlannerInfo *root,
		Path *path);
static const char* get_pathkeys(const List *pathkeys, const List *rtable);
static const char* get_expr(const Node *expr, const List *rtable);

void
printDebugGraphRel(PlannerInfo *root, RelOptInfo *rel)
{
	DebugGraph *graph = createDebugGraph("RelOptInfo");
	DebugNode  *node  = newDebugNodeByPointer(graph, rel, "RelOptInfo");
	DebugNode  *n;

	add_relids(node, "relids", root, rel->relids);
	addDebugNodeAttributeArgs(node, "rows", "%.0f", rel->rows);
	addDebugNodeAttributeArgs(node, "width", "%d", rel->width);

	if (n = get_restrictclauses(graph, root, rel->baserestrictinfo))
		newDebugEdgeByName(graph, node->internal_name, n->internal_name,
				"baserestrictinfo");

	if (n = get_restrictclauses(graph, root, rel->joininfo))
		newDebugEdgeByName(graph, node->internal_name, n->internal_name,
				"joininfo");

	if (rel->pathlist)
	{
		ListCell   *l;
		DebugNode  *node_list = newDebugNodeByPointer(graph, rel->pathlist,
				"List");

		Assert(node_list);
		newDebugEdgeByName(graph, node->internal_name, node_list->internal_name,
				"pathlist");

		foreach(l, rel->pathlist)
		{
			n = get_path(graph, root, lfirst(l));
			Assert(n);
			newDebugEdgeByName(graph, node_list->internal_name,
					n->internal_name, "");

			if (lfirst(l) == rel->cheapest_startup_path)
				newDebugEdgeByName(graph, node->internal_name,
									n->internal_name, "cheapest_startup_path");
			if (lfirst(l) == rel->cheapest_total_path)
				newDebugEdgeByName(graph, node->internal_name,
									n->internal_name, "cheapest_total_path");
		}
	}

	printDebugGraph(graph);

	destroyDebugGraph(graph);
}

static const char*
get_relation_name(PlannerInfo *root, int relid)
{
	RangeTblEntry *rte;

	Assert(relid <= list_length(root->parse->rtable));

	rte = rt_fetch(relid, root->parse->rtable);
	return rte->eref->aliasname;
}

static void
add_relids(DebugNode *node, const char *name, PlannerInfo *root, Relids relids)
{
	StringInfoData  str;
	int         count = 0;
	Relids		tmprelids;
	int			x;

	initStringInfo(&str);

	tmprelids = bms_copy(relids);
	while ((x = bms_first_member(tmprelids)) >= 0)
	{
		const char *relname;

		resetStringInfo(&str);
		appendStringInfo(&str, "%s[%d]", name, count);

		relname = get_relation_name(root, x);

		addDebugNodeAttribute(node, str.data, relname);

		count++;
	}
	bms_free(tmprelids);
	pfree(str.data);
}

static DebugNode*
get_restrictclauses(DebugGraph *graph, PlannerInfo *root, List *clauses)
{
	DebugNode *node;
	ListCell   *l;

	if (!clauses)
		return NULL;

	node = newDebugNodeByPointer(graph, clauses, "List");
	Assert(node);

	foreach(l, clauses)
	{
		RestrictInfo *c = lfirst(l);
		const char  *aux;
		DebugNode    *node_c = newDebugNodeByPointer(graph, c, "RestrictInfo");
		Assert(node_c);

		aux = get_expr((Node *) c->clause, root->parse->rtable);
		addDebugNodeAttribute(node_c, "clause", aux);
		pfree((void*)aux);

		newDebugEdgeByName(graph, node->internal_name, node_c->internal_name,
				"");
	}

	return node;
}

static DebugNode*
get_path(DebugGraph *graph, PlannerInfo *root, Path *path)
{
	DebugNode   *node;
	const char *ptype;
	bool         join = false;
	Path        *subpath = NULL;
	int          i;

	switch (nodeTag(path))
	{
		case T_Path:
			ptype = "SeqScan";
			break;
		case T_IndexPath:
			ptype = "IdxScan";
			break;
		case T_BitmapHeapPath:
			ptype = "BitmapHeapScan";
			break;
		case T_BitmapAndPath:
			ptype = "BitmapAndPath";
			break;
		case T_BitmapOrPath:
			ptype = "BitmapOrPath";
			break;
		case T_TidPath:
			ptype = "TidScan";
			break;
		case T_ForeignPath:
			ptype = "ForeignScan";
			break;
		case T_AppendPath:
			ptype = "Append";
			break;
		case T_MergeAppendPath:
			ptype = "MergeAppend";
			break;
		case T_ResultPath:
			ptype = "Result";
			break;
		case T_MaterialPath:
			ptype = "Material";
			subpath = ((MaterialPath *) path)->subpath;
			break;
		case T_UniquePath:
			ptype = "Unique";
			subpath = ((UniquePath *) path)->subpath;
			break;
		case T_NestPath:
			ptype = "NestLoop";
			join = true;
			break;
		case T_MergePath:
			ptype = "MergeJoin";
			join = true;
			break;
		case T_HashPath:
			ptype = "HashJoin";
			join = true;
			break;
		default:
			ptype = "???Path";
			break;
	}

	node = newDebugNodeByPointer(graph, path, ptype);
	Assert(node);

	addDebugNodeAttributeArgs(node, "mem address", "%p", path);

	if (path->parent)
		add_relids(node, "parent->relids", root, path->parent->relids);

	addDebugNodeAttributeArgs(node, "startup_cost", "%.2lf",
			path->startup_cost);
	addDebugNodeAttributeArgs(node, "total_cost", "%.2lf", path->total_cost);

	if (path->pathkeys)
	{
		const char *aux = get_pathkeys(path->pathkeys, root->parse->rtable);
		addDebugNodeAttribute(node, "pathkeys", aux);
		pfree((void*)aux);
	}

	if (path->parent->baserestrictinfo)
	{
		DebugNode  *node_aux = get_restrictclauses(graph, root,
				path->parent->baserestrictinfo);
		Assert(node_aux);
		newDebugEdgeByName(graph, node->internal_name,
				node_aux->internal_name, "parent->baserestrictinfo");
	}

	if (join)
	{
		JoinPath   *jp = (JoinPath *) path;
		DebugNode  *node_aux;

		if (node_aux = get_restrictclauses(graph, root, jp->joinrestrictinfo))
			newDebugEdgeByName(graph, node->internal_name,
					node_aux->internal_name, "joinrestrictinfo");

		if (IsA(path, MergePath))
		{
			MergePath  *mp = (MergePath *) path;
			addDebugNodeAttributeArgs(node, "outersortkeys", "%d",
					((mp->outersortkeys) ? 1 : 0));
			addDebugNodeAttributeArgs(node, "innersortkeys", "%d",
					((mp->innersortkeys) ? 1 : 0));
			addDebugNodeAttributeArgs(node, "materialize_inner", "%d",
					((mp->materialize_inner) ? 1 : 0));
		}

		node_aux = get_path(graph, root, jp->outerjoinpath);
		newDebugEdgeByName(graph, node->internal_name,
				node_aux->internal_name, "outerjoinpath");

		node_aux = get_path(graph, root, jp->innerjoinpath);
		newDebugEdgeByName(graph, node->internal_name,
				node_aux->internal_name, "innerjoinpath");
	}

	if (subpath)
	{
		DebugNode  *node_aux;
		node_aux = get_path(graph, root, subpath);
		newDebugEdgeByName(graph, node->internal_name, node_aux->internal_name,
				"subpath");
	}

	return node;
}

static const char *
get_pathkeys(const List *pathkeys, const List *rtable)
{
	StringInfoData str;
	const ListCell *i;

	initStringInfo(&str);

	appendStringInfo(&str, "(");
	foreach(i, pathkeys)
	{
		PathKey    *pathkey = (PathKey *) lfirst(i);
		EquivalenceClass *eclass;
		ListCell   *k;
		bool		first = true;

		eclass = pathkey->pk_eclass;
		/* chase up, in case pathkey is non-canonical */
		while (eclass->ec_merged)
			eclass = eclass->ec_merged;

		appendStringInfo(&str, "(");
		foreach(k, eclass->ec_members)
		{
			const char *aux;
			EquivalenceMember *mem = (EquivalenceMember *) lfirst(k);

			if (first)
				first = false;
			else
				appendStringInfo(&str, ", ");
			aux = get_expr((Node *) mem->em_expr, rtable);
			appendStringInfo(&str, "%s", aux);
			pfree((void*)aux);
		}
		appendStringInfo(&str, ")");
		if (lnext(i))
			appendStringInfo(&str, ", ");
	}
	appendStringInfo(&str, ")");

	return str.data; /*return (const char*) must be pfree'd*/
}

static const char*
get_expr(const Node *expr, const List *rtable)
{
	StringInfoData str;

	initStringInfo(&str);

	if (expr == NULL)
	{
		appendStringInfo(&str,"<>");
		return str.data;
	}

	if (IsA(expr, Var))
	{
		const Var  *var = (const Var *) expr;
		const char *relname,
		            *attname;

		switch (var->varno)
		{
			case INNER_VAR:
				relname = "INNER";
				attname = "?";
				break;
			case OUTER_VAR:
				relname = "OUTER";
				attname = "?";
				break;
			case INDEX_VAR:
				relname = "INDEX";
				attname = "?";
				break;
			default:
				{
					RangeTblEntry *rte;

					Assert(var->varno > 0 &&
						   (int) var->varno <= list_length(rtable));
					rte = rt_fetch(var->varno, rtable);
					relname = rte->eref->aliasname;
					attname = get_rte_attribute_name(rte, var->varattno);
				}
				break;
		}
		appendStringInfo(&str, "%s.%s", relname, attname);
	}
	else if (IsA(expr, Const))
	{
		const Const *c = (const Const *) expr;
		Oid			typoutput;
		bool		typIsVarlena;
		char	   *outputstr;

		if (c->constisnull)
		{
			appendStringInfo(&str, "NULL");
			return str.data;
		}

		getTypeOutputInfo(c->consttype,
						  &typoutput, &typIsVarlena);

		outputstr = OidOutputFunctionCall(typoutput, c->constvalue);
		appendStringInfo(&str, "%s", outputstr);
		pfree(outputstr);
	}
	else if (IsA(expr, OpExpr))
	{
		const OpExpr *e = (const OpExpr *) expr;
		const char   *opname;

		opname = get_opname(e->opno);
		if (list_length(e->args) > 1)
		{
			const char *aux;
			aux = get_expr(get_leftop((const Expr *) e), rtable);
			appendStringInfo(&str, "%s", aux);
			pfree((void*)aux);

			appendStringInfo(&str, " %s ",
					((opname != NULL) ? opname : "(invalid operator)"));

			aux = get_expr(get_rightop((const Expr *) e), rtable);
			appendStringInfo(&str, "%s", aux);
			pfree((void*)aux);
		}
		else
		{
			const char *aux;
			/* we print prefix and postfix ops the same... */
			appendStringInfo(&str, "%s ",
					((opname != NULL) ? opname : "(invalid operator)"));
			aux = get_expr(get_leftop((const Expr *) e), rtable);
			appendStringInfo(&str, "%s", aux);
			pfree((void*)aux);
		}
	}
	else if (IsA(expr, FuncExpr))
	{
		const FuncExpr *e = (const FuncExpr *) expr;
		char	   *funcname;
		ListCell   *l;

		funcname = get_func_name(e->funcid);
		appendStringInfo(&str, "%s(",
				((funcname != NULL) ? funcname : "(invalid function)"));
		foreach(l, e->args)
		{
			const char *aux = get_expr(lfirst(l), rtable);
			appendStringInfo(&str, "%s", aux);
			pfree((void*)aux);

			if (lnext(l))
				appendStringInfo(&str, ",");
		}
		appendStringInfo(&str, ")");
	}
	else
		appendStringInfo(&str, "unknown expr");

	return str.data; /*return (const char*) must be pfree'd*/
}
