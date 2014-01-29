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
#include <access/htup_details.h>
#include <lib/stringinfo.h>
#include <parser/parsetree.h>
#include <optimizer/pathnode.h>
#include <utils/fmgrtab.h>
#include <utils/lsyscache.h>
#include <utils/syscache.h>
#include <optimizer/clauses.h>
#include <optimizer/cost.h>

static DebugNode* get_params(DebugGraph *graph);
static DebugNode* get_plannerinfo(DebugGraph *graph, PlannerInfo *root);
static DebugNode* get_reloptinfo(DebugGraph *graph, PlannerInfo *root,
		RelOptInfo *rel);

void
printDebugGraphRel(PlannerInfo *root, RelOptInfo *rel)
{
	DebugGraph *graph = createDebugGraph("RelOptInfo");

	Assert(rel && IsA(rel, RelOptInfo));

	get_params(graph);
	get_plannerinfo(graph, root);
	get_reloptinfo(graph, root, rel);

	printDebugGraph(graph);

	renameDebugGraph(graph, "nodes");
	printDebugGraphAsOctaveStruct(graph);

	destroyDebugGraph(graph);
}

#define booltostr(x)  ((x) ? "true" : "false")

static DebugNode* get_params(DebugGraph *graph)
{
	DebugNode *node;

	node = newDebugNode(graph, "params", "params");

	addDebugNodeAttributeArgs(node, "seq_page_cost", "%lf", seq_page_cost);
	addDebugNodeAttributeArgs(node, "random_page_cost", "%lf",
			random_page_cost);
	addDebugNodeAttributeArgs(node, "cpu_tuple_cost", "%lf", cpu_tuple_cost);
	addDebugNodeAttributeArgs(node, "cpu_index_tuple_cost", "%lf",
			cpu_index_tuple_cost);
	addDebugNodeAttributeArgs(node, "cpu_operator_cost", "%lf",
			cpu_operator_cost);
	addDebugNodeAttributeArgs(node, "BLCKSZ", "%d", BLCKSZ);
	addDebugNodeAttributeArgs(node, "sizeof(HeapTupleHeaderData)", "%u",
			sizeof(HeapTupleHeaderData));
	addDebugNodeAttributeArgs(node, "MAXIMUM_ALIGNOF", "%u",
			MAXIMUM_ALIGNOF);

	addDebugNodeAttributeArgs(node, "effective_cache_size", "%d", effective_cache_size);

	addDebugNodeAttributeArgs(node, "disable_cost", "%lf", disable_cost);

	addDebugNodeAttributeArgs(node, "enable_seqscan", "%s",
			booltostr(enable_seqscan));
	addDebugNodeAttributeArgs(node, "enable_indexscan", "%s",
			booltostr(enable_indexscan));
	addDebugNodeAttributeArgs(node, "enable_indexonlyscan", "%s",
			booltostr(enable_indexonlyscan));
	addDebugNodeAttributeArgs(node, "enable_bitmapscan", "%s",
			booltostr(enable_bitmapscan));
	addDebugNodeAttributeArgs(node, "enable_tidscan", "%s",
			booltostr(enable_tidscan));
	addDebugNodeAttributeArgs(node, "enable_sort", "%s",
			booltostr(enable_sort));
	addDebugNodeAttributeArgs(node, "enable_hashagg", "%s",
			booltostr(enable_hashagg));
	addDebugNodeAttributeArgs(node, "enable_nestloop", "%s",
			booltostr(enable_nestloop));
	addDebugNodeAttributeArgs(node, "enable_material", "%s",
			booltostr(enable_material));
	addDebugNodeAttributeArgs(node, "enable_mergejoin", "%s",
			booltostr(enable_mergejoin));
	addDebugNodeAttributeArgs(node, "enable_hashjoin", "%s",
			booltostr(enable_hashjoin));

	return node;
}

static void add_relids(DebugNode *node, const char *name,
		PlannerInfo *root, Relids relids);
static DebugNode* get_restrictclauses(DebugGraph *graph, PlannerInfo *root,
		List *clauses);
static const char* get_tag_name(NodeTag tag);

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
	addDebugNodeAttributeArgs(node, "type_name", "%s",
			get_tag_name(root->type));

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
	addDebugNodeAttributeArgs(node, "type_name", "%s",
			get_tag_name(rel->type));

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
		addDebugNodeAttributeArgs(node_list, "type_name", "%s",
				get_tag_name(rel->pathlist->type));
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
	addDebugNodeAttributeArgs(node, "type_name", "%s",
			get_tag_name(clauses->type));

	foreach(l, clauses)
	{
		RestrictInfo *c = lfirst(l);
		DebugNode    *node_c;
		const char  *aux;

		Assert(IsA(c, RestrictInfo));

		node_c = newDebugNodeByPointer(graph, c, "RestrictInfo");
		Assert(node_c);
		addDebugNodeAttributeArgs(node_c, "type", "%d", c->type);
		addDebugNodeAttributeArgs(node_c, "type_name", "%s",
				get_tag_name(c->type));
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
const char* get_jointype_name(JoinType t);

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

	addDebugNodeAttributeArgs(node, "address", "%p", path);
	addDebugNodeAttributeArgs(node, "type", "%d", path->type);
	addDebugNodeAttributeArgs(node, "type_name", "%s",
			get_tag_name(path->type));
	addDebugNodeAttributeArgs(node, "pathtype", "%d", path->pathtype);
	addDebugNodeAttributeArgs(node, "pathtype_name", "%s",
			get_tag_name(path->pathtype));

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
				addDebugNodeAttributeArgs(node, "indexselectivity", "%le",
						idxpath->indexselectivity);
				addDebugNodeAttributeArgs(node, "indexcorrelation", "%le",
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

		addDebugNodeAttributeArgs(node, "jointype", "%d", jp->jointype);
		addDebugNodeAttributeArgs(node, "jointype_name", "%s",
				get_jointype_name(jp->jointype));

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
	addDebugNodeAttributeArgs(node, "type_name", "%s",
			get_tag_name(param_info->type));
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
	addDebugNodeAttributeArgs(node, "type_name", "%s",
			get_tag_name(index_info->type));

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

#define ENUM_STR(name) name, CppAsString(name)

typedef struct NodeTagNameMapType
{
	NodeTag      tag;
	const char *name;
} NodeTagNameMapType;

static const char*
get_tag_name(NodeTag tag)
{
	const NodeTagNameMapType tags[] = {
			{ENUM_STR(T_Invalid)},

			/*
			 * TAGS FOR EXECUTOR NODES (execnodes.h)
			 */
			{ENUM_STR(T_IndexInfo)},
			{ENUM_STR(T_ExprContext)},
			{ENUM_STR(T_ProjectionInfo)},
			{ENUM_STR(T_JunkFilter)},
			{ENUM_STR(T_ResultRelInfo)},
			{ENUM_STR(T_EState)},
			{ENUM_STR(T_TupleTableSlot)},

			/*
			 * TAGS FOR PLAN NODES (plannodes.h)
			 */
			{ENUM_STR(T_Plan)},
			{ENUM_STR(T_Result)},
			{ENUM_STR(T_ModifyTable)},
			{ENUM_STR(T_Append)},
			{ENUM_STR(T_MergeAppend)},
			{ENUM_STR(T_RecursiveUnion)},
			{ENUM_STR(T_BitmapAnd)},
			{ENUM_STR(T_BitmapOr)},
			{ENUM_STR(T_Scan)},
			{ENUM_STR(T_SeqScan)},
			{ENUM_STR(T_IndexScan)},
			{ENUM_STR(T_IndexOnlyScan)},
			{ENUM_STR(T_BitmapIndexScan)},
			{ENUM_STR(T_BitmapHeapScan)},
			{ENUM_STR(T_TidScan)},
			{ENUM_STR(T_SubqueryScan)},
			{ENUM_STR(T_FunctionScan)},
			{ENUM_STR(T_ValuesScan)},
			{ENUM_STR(T_CteScan)},
			{ENUM_STR(T_WorkTableScan)},
			{ENUM_STR(T_ForeignScan)},
			{ENUM_STR(T_Join)},
			{ENUM_STR(T_NestLoop)},
			{ENUM_STR(T_MergeJoin)},
			{ENUM_STR(T_HashJoin)},
			{ENUM_STR(T_Material)},
			{ENUM_STR(T_Sort)},
			{ENUM_STR(T_Group)},
			{ENUM_STR(T_Agg)},
			{ENUM_STR(T_WindowAgg)},
			{ENUM_STR(T_Unique)},
			{ENUM_STR(T_Hash)},
			{ENUM_STR(T_SetOp)},
			{ENUM_STR(T_LockRows)},
			{ENUM_STR(T_Limit)},
			/* these aren't subclasses of Plan: */
			{ENUM_STR(T_NestLoopParam)},
			{ENUM_STR(T_PlanRowMark)},
			{ENUM_STR(T_PlanInvalItem)},

			/*
			 * TAGS FOR PLAN STATE NODES (execnodes.h)
			 *
			 * These should correspond one-to-one with Plan node types.
			 */
			{ENUM_STR(T_PlanState)},
			{ENUM_STR(T_ResultState)},
			{ENUM_STR(T_ModifyTableState)},
			{ENUM_STR(T_AppendState)},
			{ENUM_STR(T_MergeAppendState)},
			{ENUM_STR(T_RecursiveUnionState)},
			{ENUM_STR(T_BitmapAndState)},
			{ENUM_STR(T_BitmapOrState)},
			{ENUM_STR(T_ScanState)},
			{ENUM_STR(T_SeqScanState)},
			{ENUM_STR(T_IndexScanState)},
			{ENUM_STR(T_IndexOnlyScanState)},
			{ENUM_STR(T_BitmapIndexScanState)},
			{ENUM_STR(T_BitmapHeapScanState)},
			{ENUM_STR(T_TidScanState)},
			{ENUM_STR(T_SubqueryScanState)},
			{ENUM_STR(T_FunctionScanState)},
			{ENUM_STR(T_ValuesScanState)},
			{ENUM_STR(T_CteScanState)},
			{ENUM_STR(T_WorkTableScanState)},
			{ENUM_STR(T_ForeignScanState)},
			{ENUM_STR(T_JoinState)},
			{ENUM_STR(T_NestLoopState)},
			{ENUM_STR(T_MergeJoinState)},
			{ENUM_STR(T_HashJoinState)},
			{ENUM_STR(T_MaterialState)},
			{ENUM_STR(T_SortState)},
			{ENUM_STR(T_GroupState)},
			{ENUM_STR(T_AggState)},
			{ENUM_STR(T_WindowAggState)},
			{ENUM_STR(T_UniqueState)},
			{ENUM_STR(T_HashState)},
			{ENUM_STR(T_SetOpState)},
			{ENUM_STR(T_LockRowsState)},
			{ENUM_STR(T_LimitState)},

			/*
			 * TAGS FOR PRIMITIVE NODES (primnodes.h)
			 */
			{ENUM_STR(T_Alias)},
			{ENUM_STR(T_RangeVar)},
			{ENUM_STR(T_Expr)},
			{ENUM_STR(T_Var)},
			{ENUM_STR(T_Const)},
			{ENUM_STR(T_Param)},
			{ENUM_STR(T_Aggref)},
			{ENUM_STR(T_WindowFunc)},
			{ENUM_STR(T_ArrayRef)},
			{ENUM_STR(T_FuncExpr)},
			{ENUM_STR(T_NamedArgExpr)},
			{ENUM_STR(T_OpExpr)},
			{ENUM_STR(T_DistinctExpr)},
			{ENUM_STR(T_NullIfExpr)},
			{ENUM_STR(T_ScalarArrayOpExpr)},
			{ENUM_STR(T_BoolExpr)},
			{ENUM_STR(T_SubLink)},
			{ENUM_STR(T_SubPlan)},
			{ENUM_STR(T_AlternativeSubPlan)},
			{ENUM_STR(T_FieldSelect)},
			{ENUM_STR(T_FieldStore)},
			{ENUM_STR(T_RelabelType)},
			{ENUM_STR(T_CoerceViaIO)},
			{ENUM_STR(T_ArrayCoerceExpr)},
			{ENUM_STR(T_ConvertRowtypeExpr)},
			{ENUM_STR(T_CollateExpr)},
			{ENUM_STR(T_CaseExpr)},
			{ENUM_STR(T_CaseWhen)},
			{ENUM_STR(T_CaseTestExpr)},
			{ENUM_STR(T_ArrayExpr)},
			{ENUM_STR(T_RowExpr)},
			{ENUM_STR(T_RowCompareExpr)},
			{ENUM_STR(T_CoalesceExpr)},
			{ENUM_STR(T_MinMaxExpr)},
			{ENUM_STR(T_XmlExpr)},
			{ENUM_STR(T_NullTest)},
			{ENUM_STR(T_BooleanTest)},
			{ENUM_STR(T_CoerceToDomain)},
			{ENUM_STR(T_CoerceToDomainValue)},
			{ENUM_STR(T_SetToDefault)},
			{ENUM_STR(T_CurrentOfExpr)},
			{ENUM_STR(T_TargetEntry)},
			{ENUM_STR(T_RangeTblRef)},
			{ENUM_STR(T_JoinExpr)},
			{ENUM_STR(T_FromExpr)},
			{ENUM_STR(T_IntoClause)},

			/*
			 * TAGS FOR EXPRESSION STATE NODES (execnodes.h)
			 *
			 * These correspond (not always one-for-one) to primitive nodes derived
			 * from Expr.
			 */
			{ENUM_STR(T_ExprState)},
			{ENUM_STR(T_GenericExprState)},
			{ENUM_STR(T_WholeRowVarExprState)},
			{ENUM_STR(T_AggrefExprState)},
			{ENUM_STR(T_WindowFuncExprState)},
			{ENUM_STR(T_ArrayRefExprState)},
			{ENUM_STR(T_FuncExprState)},
			{ENUM_STR(T_ScalarArrayOpExprState)},
			{ENUM_STR(T_BoolExprState)},
			{ENUM_STR(T_SubPlanState)},
			{ENUM_STR(T_AlternativeSubPlanState)},
			{ENUM_STR(T_FieldSelectState)},
			{ENUM_STR(T_FieldStoreState)},
			{ENUM_STR(T_CoerceViaIOState)},
			{ENUM_STR(T_ArrayCoerceExprState)},
			{ENUM_STR(T_ConvertRowtypeExprState)},
			{ENUM_STR(T_CaseExprState)},
			{ENUM_STR(T_CaseWhenState)},
			{ENUM_STR(T_ArrayExprState)},
			{ENUM_STR(T_RowExprState)},
			{ENUM_STR(T_RowCompareExprState)},
			{ENUM_STR(T_CoalesceExprState)},
			{ENUM_STR(T_MinMaxExprState)},
			{ENUM_STR(T_XmlExprState)},
			{ENUM_STR(T_NullTestState)},
			{ENUM_STR(T_CoerceToDomainState)},
			{ENUM_STR(T_DomainConstraintState)},

			/*
			 * TAGS FOR PLANNER NODES (relation.h)
			 */
			{ENUM_STR(T_PlannerInfo)},
			{ENUM_STR(T_PlannerGlobal)},
			{ENUM_STR(T_RelOptInfo)},
			{ENUM_STR(T_IndexOptInfo)},
			{ENUM_STR(T_ParamPathInfo)},
			{ENUM_STR(T_Path)},
			{ENUM_STR(T_IndexPath)},
			{ENUM_STR(T_BitmapHeapPath)},
			{ENUM_STR(T_BitmapAndPath)},
			{ENUM_STR(T_BitmapOrPath)},
			{ENUM_STR(T_NestPath)},
			{ENUM_STR(T_MergePath)},
			{ENUM_STR(T_HashPath)},
			{ENUM_STR(T_TidPath)},
			{ENUM_STR(T_ForeignPath)},
			{ENUM_STR(T_AppendPath)},
			{ENUM_STR(T_MergeAppendPath)},
			{ENUM_STR(T_ResultPath)},
			{ENUM_STR(T_MaterialPath)},
			{ENUM_STR(T_UniquePath)},
			{ENUM_STR(T_EquivalenceClass)},
			{ENUM_STR(T_EquivalenceMember)},
			{ENUM_STR(T_PathKey)},
			{ENUM_STR(T_RestrictInfo)},
			{ENUM_STR(T_PlaceHolderVar)},
			{ENUM_STR(T_SpecialJoinInfo)},
			{ENUM_STR(T_LateralJoinInfo)},
			{ENUM_STR(T_AppendRelInfo)},
			{ENUM_STR(T_PlaceHolderInfo)},
			{ENUM_STR(T_MinMaxAggInfo)},
			{ENUM_STR(T_PlannerParamItem)},

			/*
			 * TAGS FOR MEMORY NODES (memnodes.h)
			 */
			{ENUM_STR(T_MemoryContext)},
			{ENUM_STR(T_AllocSetContext)},

			/*
			 * TAGS FOR VALUE NODES (value.h)
			 */
			{ENUM_STR(T_Value)},
			{ENUM_STR(T_Integer)},
			{ENUM_STR(T_Float)},
			{ENUM_STR(T_String)},
			{ENUM_STR(T_BitString)},
			{ENUM_STR(T_Null)},

			/*
			 * TAGS FOR LIST NODES (pg_list.h)
			 */
			{ENUM_STR(T_List)},
			{ENUM_STR(T_IntList)},
			{ENUM_STR(T_OidList)},

			/*
			 * TAGS FOR STATEMENT NODES (mostly in parsenodes.h)
			 */
			{ENUM_STR(T_Query)},
			{ENUM_STR(T_PlannedStmt)},
			{ENUM_STR(T_InsertStmt)},
			{ENUM_STR(T_DeleteStmt)},
			{ENUM_STR(T_UpdateStmt)},
			{ENUM_STR(T_SelectStmt)},
			{ENUM_STR(T_AlterTableStmt)},
			{ENUM_STR(T_AlterTableCmd)},
			{ENUM_STR(T_AlterDomainStmt)},
			{ENUM_STR(T_SetOperationStmt)},
			{ENUM_STR(T_GrantStmt)},
			{ENUM_STR(T_GrantRoleStmt)},
			{ENUM_STR(T_AlterDefaultPrivilegesStmt)},
			{ENUM_STR(T_ClosePortalStmt)},
			{ENUM_STR(T_ClusterStmt)},
			{ENUM_STR(T_CopyStmt)},
			{ENUM_STR(T_CreateStmt)},
			{ENUM_STR(T_DefineStmt)},
			{ENUM_STR(T_DropStmt)},
			{ENUM_STR(T_TruncateStmt)},
			{ENUM_STR(T_CommentStmt)},
			{ENUM_STR(T_FetchStmt)},
			{ENUM_STR(T_IndexStmt)},
			{ENUM_STR(T_CreateFunctionStmt)},
			{ENUM_STR(T_AlterFunctionStmt)},
			{ENUM_STR(T_DoStmt)},
			{ENUM_STR(T_RenameStmt)},
			{ENUM_STR(T_RuleStmt)},
			{ENUM_STR(T_NotifyStmt)},
			{ENUM_STR(T_ListenStmt)},
			{ENUM_STR(T_UnlistenStmt)},
			{ENUM_STR(T_TransactionStmt)},
			{ENUM_STR(T_ViewStmt)},
			{ENUM_STR(T_LoadStmt)},
			{ENUM_STR(T_CreateDomainStmt)},
			{ENUM_STR(T_CreatedbStmt)},
			{ENUM_STR(T_DropdbStmt)},
			{ENUM_STR(T_VacuumStmt)},
			{ENUM_STR(T_ExplainStmt)},
			{ENUM_STR(T_CreateTableAsStmt)},
			{ENUM_STR(T_CreateSeqStmt)},
			{ENUM_STR(T_AlterSeqStmt)},
			{ENUM_STR(T_VariableSetStmt)},
			{ENUM_STR(T_VariableShowStmt)},
			{ENUM_STR(T_DiscardStmt)},
			{ENUM_STR(T_CreateTrigStmt)},
			{ENUM_STR(T_CreatePLangStmt)},
			{ENUM_STR(T_CreateRoleStmt)},
			{ENUM_STR(T_AlterRoleStmt)},
			{ENUM_STR(T_DropRoleStmt)},
			{ENUM_STR(T_LockStmt)},
			{ENUM_STR(T_ConstraintsSetStmt)},
			{ENUM_STR(T_ReindexStmt)},
			{ENUM_STR(T_CheckPointStmt)},
			{ENUM_STR(T_CreateSchemaStmt)},
			{ENUM_STR(T_AlterDatabaseStmt)},
			{ENUM_STR(T_AlterDatabaseSetStmt)},
			{ENUM_STR(T_AlterRoleSetStmt)},
			{ENUM_STR(T_CreateConversionStmt)},
			{ENUM_STR(T_CreateCastStmt)},
			{ENUM_STR(T_CreateOpClassStmt)},
			{ENUM_STR(T_CreateOpFamilyStmt)},
			{ENUM_STR(T_AlterOpFamilyStmt)},
			{ENUM_STR(T_PrepareStmt)},
			{ENUM_STR(T_ExecuteStmt)},
			{ENUM_STR(T_DeallocateStmt)},
			{ENUM_STR(T_DeclareCursorStmt)},
			{ENUM_STR(T_CreateTableSpaceStmt)},
			{ENUM_STR(T_DropTableSpaceStmt)},
			{ENUM_STR(T_AlterObjectSchemaStmt)},
			{ENUM_STR(T_AlterOwnerStmt)},
			{ENUM_STR(T_DropOwnedStmt)},
			{ENUM_STR(T_ReassignOwnedStmt)},
			{ENUM_STR(T_CompositeTypeStmt)},
			{ENUM_STR(T_CreateEnumStmt)},
			{ENUM_STR(T_CreateRangeStmt)},
			{ENUM_STR(T_AlterEnumStmt)},
			{ENUM_STR(T_AlterTSDictionaryStmt)},
			{ENUM_STR(T_AlterTSConfigurationStmt)},
			{ENUM_STR(T_CreateFdwStmt)},
			{ENUM_STR(T_AlterFdwStmt)},
			{ENUM_STR(T_CreateForeignServerStmt)},
			{ENUM_STR(T_AlterForeignServerStmt)},
			{ENUM_STR(T_CreateUserMappingStmt)},
			{ENUM_STR(T_AlterUserMappingStmt)},
			{ENUM_STR(T_DropUserMappingStmt)},
			{ENUM_STR(T_AlterTableSpaceOptionsStmt)},
			{ENUM_STR(T_SecLabelStmt)},
			{ENUM_STR(T_CreateForeignTableStmt)},
			{ENUM_STR(T_CreateExtensionStmt)},
			{ENUM_STR(T_AlterExtensionStmt)},
			{ENUM_STR(T_AlterExtensionContentsStmt)},
			{ENUM_STR(T_CreateEventTrigStmt)},
			{ENUM_STR(T_AlterEventTrigStmt)},
			{ENUM_STR(T_RefreshMatViewStmt)},

			/*
			 * TAGS FOR PARSE TREE NODES (parsenodes.h)
			 */
			{ENUM_STR(T_A_Expr)},
			{ENUM_STR(T_ColumnRef)},
			{ENUM_STR(T_ParamRef)},
			{ENUM_STR(T_A_Const)},
			{ENUM_STR(T_FuncCall)},
			{ENUM_STR(T_A_Star)},
			{ENUM_STR(T_A_Indices)},
			{ENUM_STR(T_A_Indirection)},
			{ENUM_STR(T_A_ArrayExpr)},
			{ENUM_STR(T_ResTarget)},
			{ENUM_STR(T_TypeCast)},
			{ENUM_STR(T_CollateClause)},
			{ENUM_STR(T_SortBy)},
			{ENUM_STR(T_WindowDef)},
			{ENUM_STR(T_RangeSubselect)},
			{ENUM_STR(T_RangeFunction)},
			{ENUM_STR(T_TypeName)},
			{ENUM_STR(T_ColumnDef)},
			{ENUM_STR(T_IndexElem)},
			{ENUM_STR(T_Constraint)},
			{ENUM_STR(T_DefElem)},
			{ENUM_STR(T_RangeTblEntry)},
			{ENUM_STR(T_SortGroupClause)},
			{ENUM_STR(T_WindowClause)},
			{ENUM_STR(T_PrivGrantee)},
			{ENUM_STR(T_FuncWithArgs)},
			{ENUM_STR(T_AccessPriv)},
			{ENUM_STR(T_CreateOpClassItem)},
			{ENUM_STR(T_TableLikeClause)},
			{ENUM_STR(T_FunctionParameter)},
			{ENUM_STR(T_LockingClause)},
			{ENUM_STR(T_RowMarkClause)},
			{ENUM_STR(T_XmlSerialize)},
			{ENUM_STR(T_WithClause)},
			{ENUM_STR(T_CommonTableExpr)},

			/*
			 * TAGS FOR REPLICATION GRAMMAR PARSE NODES (replnodes.h)
			 */
			{ENUM_STR(T_IdentifySystemCmd)},
			{ENUM_STR(T_BaseBackupCmd)},
			{ENUM_STR(T_StartReplicationCmd)},
			{ENUM_STR(T_TimeLineHistoryCmd)},

			/*
			 * TAGS FOR RANDOM OTHER STUFF
			 *
			 * These are objects that aren't part of parse/plan/execute node tree
			 * structures)}, but we give them NodeTags anyway for identification
			 * purposes (usually because they are involved in APIs where we want to
			 * pass multiple object types through the same pointer).
			 */
			{ENUM_STR(T_TriggerData)},		/* in commands/trigger.h */
			{ENUM_STR(T_EventTriggerData)},			/* in commands/even{TAG_STR(T_trigger.h */
			{ENUM_STR(T_ReturnSetInfo)},			/* in nodes/execnodes.h */
			{ENUM_STR(T_WindowObjectData)},			/* private in nodeWindowAgg.c */
			{ENUM_STR(T_TIDBitmap)},				/* in nodes/tidbitmap.h */
			{ENUM_STR(T_InlineCodeBlock)},			/* in nodes/parsenodes.h */
			{ENUM_STR(T_FdwRoutine)},				/* in foreign/fdwapi.h */
			{0,""}
		};
	const NodeTagNameMapType *i;
	for (i = tags; i->tag != tag && i->name[0] != '\0'; i++);
	return i->name;
}

typedef struct JoinTypeNameMap
{
	JoinType type;
	const char *name;
} JoinTypeNameMap;

const char*
get_jointype_name(JoinType t)
{
	const JoinTypeNameMap types[] = {
			{ENUM_STR(JOIN_INNER)},					/* matching tuple pairs only */
			{ENUM_STR(JOIN_LEFT)},					/* pairs + unmatched LHS tuples */
			{ENUM_STR(JOIN_FULL)},					/* pairs + unmatched LHS + unmatched RHS */
			{ENUM_STR(JOIN_RIGHT)},					/* pairs + unmatched RHS tuples */

		/*
		 * Semijoins and anti-semijoins (as defined in relational theory) do not
		 * appear in the SQL JOIN syntax, but there are standard idioms for
		 * representing them (e.g., using EXISTS).	The planner recognizes these
		 * cases and converts them to joins.  So the planner and executor must
		 * support these codes.  NOTE: in JOIN_SEMI output, it is unspecified
		 * which matching RHS row is joined to.  In JOIN_ANTI output, the row is
		 * guaranteed to be null-extended.
		 */
			{ENUM_STR(JOIN_SEMI)},					/* 1 copy of each LHS row that has match(es) */
			{ENUM_STR(JOIN_ANTI)},					/* 1 copy of each LHS row that has no match */

		/*
		 * These codes are used internally in the planner, but are not supported
		 * by the executor (nor, indeed, by most of the planner).
		 */
			{ENUM_STR(JOIN_UNIQUE_OUTER)},			/* LHS path must be made unique */
			{ENUM_STR(JOIN_UNIQUE_INNER)},			/* RHS path must be made unique */
			{0,""}
	};
	const JoinTypeNameMap *i;
	for (i=types; i->type != t && i->name[0] != '\0'; i++);
	return i->name;
}
