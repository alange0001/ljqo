/*
 * debuggraph_rel.c
 *
 *   DebugGraph structures and functions. They are used to generate directed
 *   graphs for debug purposes.
 *
 * Portions Copyright (C) 2009-2014, Adriano Lange
 * Portions Copyright (c) 1996-2013, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
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

static DebugNode* get_plannerinfo(DebugGraph *graph, PlannerInfo *root);
static DebugNode* get_reloptinfo(DebugGraph *graph, PlannerInfo *root,
		RelOptInfo *rel);

void
printDebugGraphRel(PlannerInfo *root, RelOptInfo *rel)
{
	DebugGraph *graph = createDebugGraph("RelOptInfo");

	Assert(rel && IsA(rel, RelOptInfo));

	get_plannerinfo(graph, root);
	get_reloptinfo(graph, root, rel);

	printDebugGraph(graph);

	renameDebugGraph(graph, "nodes");
	printDebugGraphAsOctaveStruct(graph);

	destroyDebugGraph(graph);
}

static void add_relids(DebugNode *node, const char *name,
		PlannerInfo *root, Relids relids);
static DebugNode* get_restrictclauses(DebugGraph *graph, PlannerInfo *root,
		List *clauses);

static DebugNode* get_plannerinfo(DebugGraph *graph, PlannerInfo *root)
{
	DebugNode  *node;

	if(!root)
		return NULL;

	Assert(graph);
	Assert(IsA(root, PlannerInfo));

	node  = newDebugNodeByPointer(graph, root, "PlannerInfo");
	Assert(node);
	if (node->create_node_again)
		return node;

	addDebugNodeAttributeArgs(node, "type", "%d", root->type);

	addDebugNodeAttributeArgs(node, "query_level", "%u", root->query_level);

	newDebugEdgeByNode(graph, node,
			get_plannerinfo(graph, root->parent_root), "parent_root");

	add_relids(node, "all_baserels", root, root->all_baserels);

	newDebugEdgeByNode(graph, node,
			get_restrictclauses(graph, root, root->left_join_clauses),
			"left_join_clauses");
	newDebugEdgeByNode(graph, node,
			get_restrictclauses(graph, root, root->right_join_clauses),
			"right_join_clauses");
	newDebugEdgeByNode(graph, node,
			get_restrictclauses(graph, root, root->full_join_clauses),
			"full_join_clauses");

	{
		DebugNode *list = (root->initial_rels)
					?newDebugNodeByPointer(graph, root->initial_rels,
							"initial_rels")
					:NULL;
		newDebugEdgeByNode(graph, node, list, "initial_rels");
		if (list)
		{
			ListCell *lc;
			foreach(lc, root->initial_rels)
			{
				RelOptInfo *rel = (RelOptInfo*) lfirst(lc);
				DebugNode *node_rel = get_reloptinfo(graph, root, rel);
				newDebugEdgeByNode(graph, list, node_rel, "");
			}
		}
	}

	addDebugNodeAttributeArgs(node, "total_table_pages", "%lf",
			root->total_table_pages);
	addDebugNodeAttributeArgs(node, "tuple_fraction", "%lf",
			root->tuple_fraction);
	addDebugNodeAttributeArgs(node, "limit_tuples", "%lf",
			root->limit_tuples);

	return node;
}

static DebugNode* get_path(DebugGraph *graph, PlannerInfo *root,
		Path *path);
static const char* get_pathkeys(const List *pathkeys, const List *rtable);
static const char* get_expr(const Node *expr, const List *rtable);
static DebugNode* get_tablespace(DebugGraph *graph, Oid tablespace);

#define booltostr(x)  ((x) ? "true" : "false")

static DebugNode*
get_reloptinfo(DebugGraph *graph, PlannerInfo *root, RelOptInfo *rel)
{
	DebugNode  *node;

	Assert(graph && root);

	if (!rel)
		return NULL;

	node  = newDebugNodeByPointer(graph, rel, "RelOptInfo");
	Assert(node);
	if (node->create_node_again)
		return node;

	addDebugNodeAttributeArgs(node, "type", "%d", rel->type);

	add_relids(node, "relids", root, rel->relids);
	add_relids(node, "lateral_relids", root, rel->lateral_relids);
	addDebugNodeAttributeArgs(node, "rows", "%lf", rel->rows);
	addDebugNodeAttributeArgs(node, "width", "%d", rel->width);
	addDebugNodeAttributeArgs(node, "consider_startup", "%s",
			booltostr(rel->consider_startup));

	addDebugNodeAttributeArgs(node, "relid", "%u", rel->relid);
	//addDebugNodeAttributeArgs(node, "reltablespace", "%u", rel->reltablespace);
	newDebugEdgeByNode(graph, node,
			get_tablespace(graph, rel->reltablespace), "reltablespace");
	addDebugNodeAttributeArgs(node, "rtekind", "%d", rel->rtekind);
	addDebugNodeAttributeArgs(node, "tuples", "%lf", rel->tuples);
	addDebugNodeAttributeArgs(node, "allvisfrac", "%lf", rel->allvisfrac);
	addDebugNodeAttributeArgs(node, "pages", "%u", rel->pages);

	if (rel->pathlist)
	{
		ListCell   *l;
		DebugNode  *node_list = newDebugNodeByPointer(graph, rel->pathlist,
				"List");

		Assert(node_list);
		addDebugNodeAttributeArgs(node_list, "type", "%d", rel->pathlist->type);
		newDebugEdgeByName(graph, node->internal_name, node_list->internal_name,
				"pathlist");

		foreach(l, rel->pathlist)
		{
			DebugNode  *n;
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

	newDebugEdgeByNode(graph, node,
			get_restrictclauses(graph, root, rel->baserestrictinfo),
			"baserestrictinfo");
	addDebugNodeAttributeArgs(node, "baserestrictcost.startup", "%lf",
			rel->baserestrictcost.startup);
	addDebugNodeAttributeArgs(node, "baserestrictcost.per_tuple", "%lf",
			rel->baserestrictcost.per_tuple);
	newDebugEdgeByNode(graph, node,
			get_restrictclauses(graph, root, rel->joininfo),
			"joininfo");
	addDebugNodeAttributeArgs(node, "has_eclass_joins", "%s",
			booltostr(rel->has_eclass_joins));

	/* pseudo attribute. Is there more than one root per reloptinfo? */
	newDebugEdgeByNode(graph, node, get_plannerinfo(graph, root), "_root");

	return node;
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
	if (!relids)
	{
		addDebugNodeAttribute(node, name, "NULL");
	}
	else
	{
		StringInfoData  str;
		Relids		tmprelids;
		int			x;

		initStringInfo(&str);

		tmprelids = bms_copy(relids);
		while ((x = bms_first_member(tmprelids)) >= 0)
		{
			const char *relname;

			resetStringInfo(&str);
			appendStringInfo(&str, "%s[%d]", name, x);

			relname = get_relation_name(root, x);

			addDebugNodeAttribute(node, str.data, relname);
		}
		bms_free(tmprelids);
		pfree(str.data);
	}
}

static DebugNode*
get_tablespace(DebugGraph *graph, Oid tablespace)
{
	DebugNode *node;
	Assert(graph);
	Assert(tablespace >= 0);

	{
		StringInfoData str;
		initStringInfo(&str);

		appendStringInfo(&str, "tablespace_%u", tablespace);
		node = newDebugNode(graph, str.data, "TableSpace");
		Assert(node);

		pfree(str.data);
	}

	if (!node->create_node_again)
	{
		addDebugNodeAttributeArgs(node, "oid", "%u", tablespace);
		addDebugNodeAttributeArgs(node, "name", "%s",
				get_tablespace_name(tablespace));
		{
			Cost c_rand, c_seq;
			get_tablespace_page_costs(tablespace, &c_rand, &c_seq);
			addDebugNodeAttributeArgs(node, "seq_page_cost", "%lf", c_seq);
			addDebugNodeAttributeArgs(node, "random_page_cost", "%lf", c_rand);
		}
	}

	return node;
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

	if (node->create_node_again)
		return node;

	addDebugNodeAttributeArgs(node, "type", "%d", clauses->type);

	foreach(l, clauses)
	{
		RestrictInfo *c = lfirst(l);
		DebugNode    *node_c;
		const char  *aux;

		Assert(IsA(c, RestrictInfo));

		node_c = newDebugNodeByPointer(graph, c, "RestrictInfo");
		Assert(node_c);
		addDebugNodeAttributeArgs(node_c, "type", "%d", c->type);
		newDebugEdgeByName(graph, node->internal_name, node_c->internal_name,
				"");

		if (node_c->create_node_again)
			continue;

		aux = get_expr((Node *) c->clause, root->parse->rtable);
		addDebugNodeAttribute(node_c, "clause", aux);
		pfree((void*)aux);
		aux = get_expr((Node *) c->orclause, root->parse->rtable);
		addDebugNodeAttribute(node_c, "orclause", aux);
		pfree((void*)aux);

		addDebugNodeAttributeArgs(node_c, "is_pushed_down", "%s",
				booltostr(c->is_pushed_down));
		addDebugNodeAttributeArgs(node_c, "outerjoin_delayed", "%s",
				booltostr(c->outerjoin_delayed));
		addDebugNodeAttributeArgs(node_c, "can_join", "%s",
				booltostr(c->can_join));
		addDebugNodeAttributeArgs(node_c, "pseudoconstant", "%s",
				booltostr(c->pseudoconstant));
		addDebugNodeAttributeArgs(node_c, "eval_cost.startup", "%lf",
				c->eval_cost.startup);
		addDebugNodeAttributeArgs(node_c, "eval_cost.per_tuple", "%lf",
				c->eval_cost.per_tuple);
		addDebugNodeAttributeArgs(node_c, "norm_selec", "%lf", c->norm_selec);
		addDebugNodeAttributeArgs(node_c, "outer_selec", "%lf",
				c->outer_selec);

	}

	return node;
}

static DebugNode* get_parampathinfo(DebugGraph *graph, PlannerInfo *root,
		ParamPathInfo *param_info);
static DebugNode* get_indexoptinfo(DebugGraph *graph, PlannerInfo *root,
		IndexPath *path, IndexOptInfo *index_info);
static double get_loop_count(PlannerInfo *root, Relids outer_relids);

static DebugNode*
get_path(DebugGraph *graph, PlannerInfo *root, Path *path)
{
	DebugNode   *node;
	const char *ptype;
	bool         join = false;
	Path        *subpath = NULL;
	double      loops = 1.0;

	if (!path)
		return NULL;

	node = newDebugNodeByPointer(graph, path, "");
	Assert(node);
	if (node->create_node_again)
		return node;

	addDebugNodeAttributeArgs(node, "type", "%d", path->type);
	addDebugNodeAttributeArgs(node, "pathtype", "%d", path->pathtype);

	newDebugEdgeByNode(graph, node, get_reloptinfo(graph, root, path->parent),
			"parent");

	newDebugEdgeByNode(graph, node,
			get_parampathinfo(graph, root, path->param_info), "param_info");
	if (path->param_info)
		loops = get_loop_count(root, path->param_info->ppi_req_outer);
	addDebugNodeAttributeArgs(node, "loops", "%lf", loops);

	addDebugNodeAttributeArgs(node, "startup_cost", "%lf",
			path->startup_cost);
	addDebugNodeAttributeArgs(node, "total_cost", "%lf", path->total_cost);
	addDebugNodeAttributeArgs(node, "rows", "%lf", path->rows);

	{
		const char *aux = get_pathkeys(path->pathkeys, root->parse->rtable);
		addDebugNodeAttribute(node, "pathkeys", aux);
		pfree((void*)aux);
	}

	switch (nodeTag(path))
	{
		case T_Path:
			ptype = "SeqScan";
			break;
		case T_IndexPath:
			ptype = "IdxScan";
			{
				IndexPath *idxpath = (IndexPath*)path;
				Cost indexStartupCost=0, indexTotalCost=0, indexSelectivity=0,
				     indexCorrelation=0;
				addDebugNodeAttributeArgs(node, "indexonly", "%s",
						booltostr(idxpath->path.pathtype == T_IndexOnlyScan));
				newDebugEdgeByNode(graph, node,
						get_indexoptinfo(graph, root, idxpath,
								idxpath->indexinfo),
						"indexinfo");
				newDebugEdgeByNode(graph, node,
						get_restrictclauses(graph, root, idxpath->indexclauses),
						"indexclauses");
				newDebugEdgeByNode(graph, node,
						get_restrictclauses(graph, root, idxpath->indexquals),
						"indexquals");
				newDebugEdgeByNode(graph, node,
						get_restrictclauses(graph, root, idxpath->indexorderbys),
						"indexorderbys");
				addDebugNodeAttributeArgs(node, "indexscandir", "%d",
						idxpath->indexscandir);
				if (idxpath->indexinfo)
					OidFunctionCall7(idxpath->indexinfo->amcostestimate,
									 PointerGetDatum(root),
									 PointerGetDatum(path),
									 Float8GetDatum(loops),
									 PointerGetDatum(&indexStartupCost),
									 PointerGetDatum(&indexTotalCost),
									 PointerGetDatum(&indexSelectivity),
									 PointerGetDatum(&indexCorrelation));
				addDebugNodeAttributeArgs(node, "indexstartupcost", "%lf",
						indexStartupCost);
				addDebugNodeAttributeArgs(node, "indextotalcost", "%lf",
						idxpath->indextotalcost);
				addDebugNodeAttributeArgs(node, "indexselectivity", "%lf",
						idxpath->indexselectivity);
				addDebugNodeAttributeArgs(node, "indexcorrelation", "%lf",
						indexCorrelation);
			}
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
			{
				MergePath  *mp = (MergePath *) path;
				addDebugNodeAttributeArgs(node, "outersortkeys", "%d",
						((mp->outersortkeys) ? 1 : 0));
				addDebugNodeAttributeArgs(node, "innersortkeys", "%d",
						((mp->innersortkeys) ? 1 : 0));
				addDebugNodeAttributeArgs(node, "materialize_inner", "%d",
						((mp->materialize_inner) ? 1 : 0));
			}
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

	renameDebugNode(node, ptype);

	if (join)
	{
		JoinPath   *jp = (JoinPath *) path;

		newDebugEdgeByNode(graph, node,
				get_restrictclauses(graph, root, jp->joinrestrictinfo),
				"joinrestrictinfo");

		newDebugEdgeByNode(graph, node,
				get_path(graph, root, jp->outerjoinpath),
				"outerjoinpath");
		newDebugEdgeByNode(graph, node,
				get_path(graph, root, jp->innerjoinpath),
				"innerjoinpath");
	}

	if (subpath)
		newDebugEdgeByNode(graph, node,
				get_path(graph, root, subpath), "subpath");

	return node;
}

static const char *
get_pathkeys(const List *pathkeys, const List *rtable)
{
	StringInfoData str;
	const ListCell *i;

	initStringInfo(&str);

	if (pathkeys)
	{
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
	}
	else
		appendStringInfoString(&str, "NULL");

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

static DebugNode*
get_parampathinfo(DebugGraph *graph, PlannerInfo *root,
		ParamPathInfo *param_info)
{
	DebugNode *node;

	Assert(graph && root);

	if (!param_info)
		return NULL;

	Assert(IsA(param_info, ParamPathInfo));
	node = newDebugNodeByPointer(graph, param_info, "ParamPathInfo");
	Assert(node);

	addDebugNodeAttributeArgs(node, "type", "%d", param_info->type);
	add_relids(node, "ppi_req_outer", root, param_info->ppi_req_outer);
	addDebugNodeAttributeArgs(node, "ppi_rows", "%lf", param_info->ppi_rows);

	newDebugEdgeByNode(graph, node,
			get_restrictclauses(graph, root, param_info->ppi_clauses),
			"ppi_clauses");

	return node;
}

static DebugNode*
get_indexoptinfo(DebugGraph *graph, PlannerInfo *root,
		IndexPath *path, IndexOptInfo *index_info)
{
	DebugNode *node;

	Assert(graph && root);

	if (!index_info)
		return NULL;

	Assert(IsA(index_info, IndexOptInfo));
	node = newDebugNodeByPointer(graph, (void*)index_info, "IndexOptInfo");
	Assert(node);

	addDebugNodeAttributeArgs(node, "type", "%d", index_info->type);

	/* NB: this isn't a complete set of fields */
	//WRITE_OID_FIELD(indexoid);

	newDebugEdgeByNode(graph, node,
			get_reloptinfo(graph, root, index_info->rel), "rel");

	addDebugNodeAttributeArgs(node, "indexoid", "%u", index_info->indexoid);
	//addDebugNodeAttributeArgs(node, "reltablespace", "%u", index_info->reltablespace);
	newDebugEdgeByNode(graph, node,
			get_tablespace(graph, index_info->reltablespace), "reltablespace");
	addDebugNodeAttributeArgs(node, "pages", "%u", index_info->pages);
	addDebugNodeAttributeArgs(node, "tuples", "%lf", index_info->tuples);

	addDebugNodeAttributeArgs(node, "tree_height", "%d", index_info->tree_height);
	addDebugNodeAttributeArgs(node, "ncolumns", "%d", index_info->ncolumns);
	addDebugNodeAttributeArgs(node, "relam", "%u", index_info->relam);
	addDebugNodeAttributeArgs(node, "amcostestimate", "%u", index_info->amcostestimate);

	//List	   *indpred;		/* predicate if a partial index, else NIL */
	//List	   *indextlist;		/* targetlist representing index columns */

	addDebugNodeAttributeArgs(node, "reverse_sort", "%s",
			booltostr(index_info->reverse_sort));
	addDebugNodeAttributeArgs(node, "nulls_first", "%s",
			booltostr(index_info->nulls_first));
	addDebugNodeAttributeArgs(node, "predOK", "%s",
			booltostr(index_info->predOK));
	addDebugNodeAttributeArgs(node, "unique", "%s",
			booltostr(index_info->unique));
	addDebugNodeAttributeArgs(node, "immediate", "%s",
			booltostr(index_info->immediate));
	addDebugNodeAttributeArgs(node, "hypothetical", "%s",
			booltostr(index_info->hypothetical));
	addDebugNodeAttributeArgs(node, "canreturn", "%s",
			booltostr(index_info->canreturn));

	return node;
}

static double
get_loop_count(PlannerInfo *root, Relids outer_relids)
{
	double		result = 1.0;

	/* For a non-parameterized path, just return 1.0 quickly */
	if (outer_relids != NULL)
	{
		int			relid;

		/* Need a working copy since bms_first_member is destructive */
		outer_relids = bms_copy(outer_relids);
		while ((relid = bms_first_member(outer_relids)) >= 0)
		{
			RelOptInfo *outer_rel;

			/* Paranoia: ignore bogus relid indexes */
			if (relid >= root->simple_rel_array_size)
				continue;
			outer_rel = root->simple_rel_array[relid];
			if (outer_rel == NULL)
				continue;
			Assert(outer_rel->relid == relid);	/* sanity check on array */

			/* Other relation could be proven empty, if so ignore */
			if (IS_DUMMY_REL(outer_rel))
				continue;

			/* Otherwise, rel's rows estimate should be valid by now */
			Assert(outer_rel->rows > 0);

			/* Remember smallest row count estimate among the outer rels */
			if (result == 1.0 || result > outer_rel->rows)
				result = outer_rel->rows;
		}
		bms_free(outer_relids);
	}
	return result;
}
