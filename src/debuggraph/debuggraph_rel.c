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
#if PG_VERSION_NUM/100 >= 903
#include <access/htup_details.h>
#endif
#include <lib/stringinfo.h>
#include <parser/parsetree.h>
#include <optimizer/pathnode.h>
#include <utils/fmgrtab.h>
#include <utils/lsyscache.h>
#include <utils/syscache.h>
#include <optimizer/clauses.h>
#include <optimizer/cost.h>

static DebugNode* getParams(DebugGraph *graph);
static DebugNode* getNode(DebugGraph *graph, PlannerInfo *root, Node *node);

void
printDebugGraphRel(PlannerInfo *root, RelOptInfo *rel, const char *name)
{
	DebugGraph *graph;

	Assert(root && IsA(root, PlannerInfo));
	Assert(rel && IsA(rel, RelOptInfo));
	Assert(name);

	graph = createDebugGraph(name);

	getParams(graph);
	getNode(graph, root, (Node*)root);
	getNode(graph, root, (Node*)rel);

	printDebugGraph(graph);

	renameDebugGraph(graph, "nodes");
	printDebugGraphAsOctaveStruct(graph);

	destroyDebugGraph(graph);
}

#define booltostr(x)  ((x) ? "true" : "false")

static DebugNode* getParams(DebugGraph *graph)
{
	DebugNode *n;

	n = newDebugNode(graph, "params", "params");
	Assert(n);

	addDebugNodeAttributeArgs(n, "seq_page_cost", "%lf", seq_page_cost);
	addDebugNodeAttributeArgs(n, "random_page_cost", "%lf",
			random_page_cost);
	addDebugNodeAttributeArgs(n, "cpu_tuple_cost", "%lf", cpu_tuple_cost);
	addDebugNodeAttributeArgs(n, "cpu_index_tuple_cost", "%lf",
			cpu_index_tuple_cost);
	addDebugNodeAttributeArgs(n, "cpu_operator_cost", "%lf",
			cpu_operator_cost);
	addDebugNodeAttributeArgs(n, "BLCKSZ", "%d", BLCKSZ);
	addDebugNodeAttributeArgs(n, "sizeof(HeapTupleHeaderData)", "%u",
			sizeof(HeapTupleHeaderData));
	addDebugNodeAttributeArgs(n, "MAXIMUM_ALIGNOF", "%u",
			MAXIMUM_ALIGNOF);

	addDebugNodeAttributeArgs(n, "effective_cache_size", "%d", effective_cache_size);

	addDebugNodeAttributeArgs(n, "disable_cost", "%lf", disable_cost);

	addDebugNodeAttributeArgs(n, "enable_seqscan", "%s",
			booltostr(enable_seqscan));
	addDebugNodeAttributeArgs(n, "enable_indexscan", "%s",
			booltostr(enable_indexscan));
	addDebugNodeAttributeArgs(n, "enable_indexonlyscan", "%s",
			booltostr(enable_indexonlyscan));
	addDebugNodeAttributeArgs(n, "enable_bitmapscan", "%s",
			booltostr(enable_bitmapscan));
	addDebugNodeAttributeArgs(n, "enable_tidscan", "%s",
			booltostr(enable_tidscan));
	addDebugNodeAttributeArgs(n, "enable_sort", "%s",
			booltostr(enable_sort));
	addDebugNodeAttributeArgs(n, "enable_hashagg", "%s",
			booltostr(enable_hashagg));
	addDebugNodeAttributeArgs(n, "enable_nestloop", "%s",
			booltostr(enable_nestloop));
	addDebugNodeAttributeArgs(n, "enable_material", "%s",
			booltostr(enable_material));
	addDebugNodeAttributeArgs(n, "enable_mergejoin", "%s",
			booltostr(enable_mergejoin));
	addDebugNodeAttributeArgs(n, "enable_hashjoin", "%s",
			booltostr(enable_hashjoin));

	return n;
}

/* ****************************** Nodes *********************************** */

#define WRITE_NODE_FIELD(fldname) \
		newDebugEdgeByNode(graph, n, \
					getNode(graph, root, (Node*) actual_node->fldname), \
					CppAsString(fldname))
#define WRITE_LIST_ITEM(lc) \
		newDebugEdgeByNode(graph, n, \
					getNode(graph, root, (Node*) lfirst(lc)), \
					"")
#define WRITE_FLOAT_FIELD(fldname,format) \
		addDebugNodeAttributeArgs(n, CppAsString(fldname), format, \
				actual_node->fldname)
#define WRITE_INT_FIELD(fldname) \
		addDebugNodeAttributeArgs(n, CppAsString(fldname), "%d", \
				actual_node->fldname)
#define WRITE_UINT_FIELD(fldname) \
		addDebugNodeAttributeArgs(n, CppAsString(fldname), "%u", \
				actual_node->fldname)
#define WRITE_OID_FIELD WRITE_UINT_FIELD
#define WRITE_BOOL_FIELD(fldname) \
		addDebugNodeAttributeArgs(n, CppAsString(fldname), "%s", \
				booltostr(actual_node->fldname))
#define WRITE_CUSTOM_FIELD(fldname, format, val) \
		addDebugNodeAttributeArgs(n, fldname, format, \
						val)

const char* getNodeTagName(NodeTag tag);
const char* getJoinTypeName(JoinType t);

static void addRelids(DebugNode *n, const char *name,
		PlannerInfo *root, Relids relids);
static const char* getPathkeys(const List *pathkeys, const List *rtable);
static const char* getExpr(const Node *expr, const List *rtable);
static DebugNode* getTablespace(DebugGraph *graph, Oid tablespace);
static double get_loop_count(PlannerInfo *root, Relids outer_relids);

static void
set_T_PlannerInfo(DebugGraph *graph, DebugNode *n,
		PlannerInfo *ignored, Node *node)
{
	PlannerInfo *root = (PlannerInfo*) node;
	PlannerInfo *actual_node = (PlannerInfo*) node;

	WRITE_UINT_FIELD(query_level);

	WRITE_NODE_FIELD(parent_root);

	addRelids(n, "all_baserels", root, root->all_baserels);

	WRITE_NODE_FIELD(left_join_clauses);
	WRITE_NODE_FIELD(right_join_clauses);
	WRITE_NODE_FIELD(full_join_clauses);
	WRITE_NODE_FIELD(initial_rels);

	WRITE_FLOAT_FIELD(total_table_pages, "%lf");
	WRITE_FLOAT_FIELD(tuple_fraction, "%lf");
	WRITE_FLOAT_FIELD(limit_tuples, "%lf");
}

static void
set_T_RelOptInfo(DebugGraph *graph, DebugNode *n, PlannerInfo *root,
		Node *node)
{
	RelOptInfo *actual_node = (RelOptInfo*) node;

	addRelids(n, "relids", root, actual_node->relids);
	WRITE_FLOAT_FIELD(rows, "%lf");
	WRITE_INT_FIELD(width);
#	if PG_VERSION_NUM/100 >= 903
	addRelids(n, "lateral_relids", root, actual_node->lateral_relids);
	WRITE_BOOL_FIELD(consider_startup);
#	endif

	WRITE_UINT_FIELD(relid);
	newDebugEdgeByNode(graph, n,
			getTablespace(graph, actual_node->reltablespace), "reltablespace");
	WRITE_INT_FIELD(rtekind);
	WRITE_FLOAT_FIELD(tuples, "%lf");
	WRITE_FLOAT_FIELD(allvisfrac, "%lf");
	WRITE_UINT_FIELD(pages);

	WRITE_NODE_FIELD(pathlist);
	WRITE_NODE_FIELD(cheapest_startup_path);
	WRITE_NODE_FIELD(cheapest_total_path);
	WRITE_NODE_FIELD(baserestrictinfo);

	WRITE_FLOAT_FIELD(baserestrictcost.startup, "%lf");
	WRITE_FLOAT_FIELD(baserestrictcost.per_tuple, "%lf");

	WRITE_NODE_FIELD(joininfo);
	WRITE_BOOL_FIELD(has_eclass_joins);

	/* pseudo attribute. Is there more than one root per reloptinfo? */
	newDebugEdgeByNode(graph, n, getNode(graph, root, (Node*)root), "_root");
}

static void
set_T_RestrictInfo(DebugGraph *graph, DebugNode *n, PlannerInfo *root,
		Node *node)
{
	RestrictInfo *actual_node = (RestrictInfo*) node;

	{const char *aux;
	aux = getExpr((Node *) actual_node->clause, root->parse->rtable);
	WRITE_CUSTOM_FIELD("clause", "%s", aux);
	pfree((void*)aux);
	aux = getExpr((Node *) actual_node->orclause, root->parse->rtable);
	WRITE_CUSTOM_FIELD("orclause", "%s", aux);
	pfree((void*)aux);}

	WRITE_BOOL_FIELD(is_pushed_down);
	WRITE_BOOL_FIELD(outerjoin_delayed);
	WRITE_BOOL_FIELD(can_join);
	WRITE_BOOL_FIELD(pseudoconstant);
	WRITE_FLOAT_FIELD(eval_cost.startup, "%lf");
	WRITE_FLOAT_FIELD(eval_cost.per_tuple, "%lf");
	WRITE_FLOAT_FIELD(norm_selec, "%lf");
	WRITE_FLOAT_FIELD(outer_selec, "%lf");
}

static void
set_T_Path(DebugGraph *graph, DebugNode *n, PlannerInfo *root, Node *node)
{
	Path *actual_node = (Path*) node;

	WRITE_CUSTOM_FIELD("pathtype", "%s", getNodeTagName(actual_node->pathtype));

	WRITE_NODE_FIELD(parent);
	WRITE_NODE_FIELD(param_info);

	{double loops = 1.0;
	if (actual_node->param_info)
		loops = get_loop_count(root, actual_node->param_info->ppi_req_outer);
	WRITE_CUSTOM_FIELD("loops", "%lf", loops);}

	WRITE_FLOAT_FIELD(startup_cost, "%lf");
	WRITE_FLOAT_FIELD(total_cost, "%lf");
	WRITE_FLOAT_FIELD(rows, "%lf");

	{const char *aux = getPathkeys(actual_node->pathkeys,
	                                 root->parse->rtable);
	WRITE_CUSTOM_FIELD("pathkeys", "%s", aux);
	pfree((void*)aux);}
}

#define set_T_BitmapHeapPath set_T_Path
#define set_T_BitmapAndPath set_T_Path
#define set_T_BitmapOrPath set_T_Path
#define set_T_TidPath set_T_Path
#define set_T_ForeignPath set_T_Path
#define set_T_AppendPath set_T_Path
#define set_T_ResultPath set_T_Path

static void
set_T_IndexPath(DebugGraph *graph, DebugNode *n, PlannerInfo *root,
		Node *node)
{
	IndexPath *actual_node = (IndexPath*) node;
	set_T_Path(graph, n, root, node); /* inheritance */

	WRITE_NODE_FIELD(indexinfo);
	WRITE_NODE_FIELD(indexclauses);
	WRITE_NODE_FIELD(indexquals);
	WRITE_NODE_FIELD(indexorderbys);
	WRITE_INT_FIELD(indexscandir);

	{Cost indexStartupCost=0, indexTotalCost=0, indexSelectivity=0,
	     indexCorrelation=0;
	if (actual_node->indexinfo && actual_node->path.param_info)
		OidFunctionCall7(actual_node->indexinfo->amcostestimate,
	                     PointerGetDatum(root),
	                     PointerGetDatum(actual_node),
	                     Float8GetDatum(get_loop_count(root,
	                           actual_node->path.param_info->ppi_req_outer)),
	                     PointerGetDatum(&indexStartupCost),
	                     PointerGetDatum(&indexTotalCost),
	                     PointerGetDatum(&indexSelectivity),
	                     PointerGetDatum(&indexCorrelation));
	WRITE_CUSTOM_FIELD("indexstartupcost", "%lf", indexStartupCost);
	WRITE_CUSTOM_FIELD("indexcorrelation", "%le", indexCorrelation);}

	WRITE_FLOAT_FIELD(indextotalcost, "%lf");
	WRITE_FLOAT_FIELD(indexselectivity, "%le");
}

static void
set_T_MergeAppendPath(DebugGraph *graph, DebugNode *n, PlannerInfo *root,
		Node *node)
{
	MergePath  *actual_node = (MergePath *) node;
	set_T_Path(graph, n, root, node); /* inheritance */

	WRITE_BOOL_FIELD(outersortkeys);
	WRITE_BOOL_FIELD(innersortkeys);
	WRITE_BOOL_FIELD(materialize_inner);
}

static void
set_T_MaterialPath(DebugGraph *graph, DebugNode *n, PlannerInfo *root,
		Node *node)
{
	MaterialPath *actual_node = (MaterialPath*) node;
	set_T_Path(graph, n, root, node); /* inheritance */

	WRITE_NODE_FIELD(subpath);
}

static void
set_T_UniquePath(DebugGraph *graph, DebugNode *n, PlannerInfo *root,
		Node *node)
{
	UniquePath *actual_node = (UniquePath*) node;
	set_T_Path(graph, n, root, node); /* inheritance */

	WRITE_NODE_FIELD(subpath);
}

static void
set_T_JoinPath(DebugGraph *graph, DebugNode *n, PlannerInfo *root,
		Node *node)
{
	JoinPath *actual_node = (JoinPath*) node;
	set_T_Path(graph, n, root, node); /* inheritance */

	WRITE_CUSTOM_FIELD("jointype", "%s",
			getJoinTypeName(actual_node->jointype));

	WRITE_NODE_FIELD(joinrestrictinfo);
	WRITE_NODE_FIELD(outerjoinpath);
	WRITE_NODE_FIELD(innerjoinpath);
}

#define set_T_NestPath set_T_JoinPath

static void
set_T_MergePath(DebugGraph *graph, DebugNode *n, PlannerInfo *root,
		Node *node)
{
	MergePath *actual_node = (MergePath*) node;
	set_T_JoinPath(graph, n, root, node); /* inheritance */

	WRITE_NODE_FIELD(path_mergeclauses);
	WRITE_NODE_FIELD(outersortkeys);
	WRITE_NODE_FIELD(innersortkeys);
	WRITE_BOOL_FIELD(materialize_inner);
}

static void
set_T_HashPath(DebugGraph *graph, DebugNode *n, PlannerInfo *root,
		Node *node)
{
	HashPath *actual_node = (HashPath*) node;
	set_T_JoinPath(graph, n, root, node); /* inheritance */

	WRITE_NODE_FIELD(path_hashclauses);
	WRITE_INT_FIELD(num_batches);
}

static void
set_T_ParamPathInfo(DebugGraph *graph, DebugNode *n, PlannerInfo *root,
		Node *node)
{
	ParamPathInfo *actual_node = (ParamPathInfo*) node;

	addRelids(n, "ppi_req_outer", root, actual_node->ppi_req_outer);
	WRITE_FLOAT_FIELD(ppi_rows, "%lf");

	WRITE_NODE_FIELD(ppi_clauses);
}

static void
set_T_IndexOptInfo(DebugGraph *graph, DebugNode *n, PlannerInfo *root,
		Node *node)
{
	IndexOptInfo *actual_node = (IndexOptInfo*) node;

	/* NB: this isn't a complete set of fields */
	WRITE_OID_FIELD(indexoid);

	WRITE_NODE_FIELD(rel);

	WRITE_UINT_FIELD(indexoid);
	newDebugEdgeByNode(graph, n,
			getTablespace(graph, actual_node->reltablespace), "reltablespace");

	WRITE_UINT_FIELD(pages);
	WRITE_FLOAT_FIELD(tuples, "%lf");
#	if PG_VERSION_NUM/100 >= 903
	WRITE_INT_FIELD(tree_height);
#	endif
	WRITE_INT_FIELD(ncolumns);
	WRITE_OID_FIELD(relam);
	WRITE_UINT_FIELD(amcostestimate);

	//List	   *indpred;		/* predicate if a partial index, else NIL */
	//List	   *indextlist;		/* targetlist representing index columns */

	WRITE_BOOL_FIELD(reverse_sort);
	WRITE_BOOL_FIELD(nulls_first);
	WRITE_BOOL_FIELD(predOK);
	WRITE_BOOL_FIELD(unique);
	WRITE_BOOL_FIELD(immediate);
	WRITE_BOOL_FIELD(hypothetical);
	WRITE_BOOL_FIELD(canreturn);
}

static void
set_T_List(DebugGraph *graph, DebugNode *n, PlannerInfo *root,
		Node *node)
{
	List *actual_node = (List*) node;
	ListCell *lc;

	foreach(lc, actual_node)
	{

		WRITE_LIST_ITEM(lc);
	}
}

/* ***************************** getNode ********************************** */

#define ENUM_MAP_N(name) name, CppAsString(name), NULL
#define ENUM_MAP_F(name) name, CppAsString(name), set_ ## name
typedef void (setNodeType)(DebugGraph*,DebugNode*,PlannerInfo*,Node*);
typedef struct NodeTagMapType
{
	NodeTag      tag;
	const char *name;
	setNodeType *setNode;
} NodeTagMapType;

const NodeTagMapType nodetag_maps[] = {
		{ENUM_MAP_N(T_Invalid)},

		/*
		 * TAGS FOR EXECUTOR NODES (execnodes.h)
		 */
		{ENUM_MAP_N(T_IndexInfo)},
		{ENUM_MAP_N(T_ExprContext)},
		{ENUM_MAP_N(T_ProjectionInfo)},
		{ENUM_MAP_N(T_JunkFilter)},
		{ENUM_MAP_N(T_ResultRelInfo)},
		{ENUM_MAP_N(T_EState)},
		{ENUM_MAP_N(T_TupleTableSlot)},

		/*
		 * TAGS FOR PLAN NODES (plannodes.h)
		 */
		{ENUM_MAP_N(T_Plan)},
		{ENUM_MAP_N(T_Result)},
		{ENUM_MAP_N(T_ModifyTable)},
		{ENUM_MAP_N(T_Append)},
		{ENUM_MAP_N(T_MergeAppend)},
		{ENUM_MAP_N(T_RecursiveUnion)},
		{ENUM_MAP_N(T_BitmapAnd)},
		{ENUM_MAP_N(T_BitmapOr)},
		{ENUM_MAP_N(T_Scan)},
		{ENUM_MAP_N(T_SeqScan)},
		{ENUM_MAP_N(T_IndexScan)},
		{ENUM_MAP_N(T_IndexOnlyScan)},
		{ENUM_MAP_N(T_BitmapIndexScan)},
		{ENUM_MAP_N(T_BitmapHeapScan)},
		{ENUM_MAP_N(T_TidScan)},
		{ENUM_MAP_N(T_SubqueryScan)},
		{ENUM_MAP_N(T_FunctionScan)},
		{ENUM_MAP_N(T_ValuesScan)},
		{ENUM_MAP_N(T_CteScan)},
		{ENUM_MAP_N(T_WorkTableScan)},
		{ENUM_MAP_N(T_ForeignScan)},
		{ENUM_MAP_N(T_Join)},
		{ENUM_MAP_N(T_NestLoop)},
		{ENUM_MAP_N(T_MergeJoin)},
		{ENUM_MAP_N(T_HashJoin)},
		{ENUM_MAP_N(T_Material)},
		{ENUM_MAP_N(T_Sort)},
		{ENUM_MAP_N(T_Group)},
		{ENUM_MAP_N(T_Agg)},
		{ENUM_MAP_N(T_WindowAgg)},
		{ENUM_MAP_N(T_Unique)},
		{ENUM_MAP_N(T_Hash)},
		{ENUM_MAP_N(T_SetOp)},
		{ENUM_MAP_N(T_LockRows)},
		{ENUM_MAP_N(T_Limit)},
		/* these aren't subclasses of Plan: */
		{ENUM_MAP_N(T_NestLoopParam)},
		{ENUM_MAP_N(T_PlanRowMark)},
		{ENUM_MAP_N(T_PlanInvalItem)},

		/*
		 * TAGS FOR PLAN STATE NODES (execnodes.h)
		 *
		 * These should correspond one-to-one with Plan node types.
		 */
		{ENUM_MAP_N(T_PlanState)},
		{ENUM_MAP_N(T_ResultState)},
		{ENUM_MAP_N(T_ModifyTableState)},
		{ENUM_MAP_N(T_AppendState)},
		{ENUM_MAP_N(T_MergeAppendState)},
		{ENUM_MAP_N(T_RecursiveUnionState)},
		{ENUM_MAP_N(T_BitmapAndState)},
		{ENUM_MAP_N(T_BitmapOrState)},
		{ENUM_MAP_N(T_ScanState)},
		{ENUM_MAP_N(T_SeqScanState)},
		{ENUM_MAP_N(T_IndexScanState)},
		{ENUM_MAP_N(T_IndexOnlyScanState)},
		{ENUM_MAP_N(T_BitmapIndexScanState)},
		{ENUM_MAP_N(T_BitmapHeapScanState)},
		{ENUM_MAP_N(T_TidScanState)},
		{ENUM_MAP_N(T_SubqueryScanState)},
		{ENUM_MAP_N(T_FunctionScanState)},
		{ENUM_MAP_N(T_ValuesScanState)},
		{ENUM_MAP_N(T_CteScanState)},
		{ENUM_MAP_N(T_WorkTableScanState)},
		{ENUM_MAP_N(T_ForeignScanState)},
		{ENUM_MAP_N(T_JoinState)},
		{ENUM_MAP_N(T_NestLoopState)},
		{ENUM_MAP_N(T_MergeJoinState)},
		{ENUM_MAP_N(T_HashJoinState)},
		{ENUM_MAP_N(T_MaterialState)},
		{ENUM_MAP_N(T_SortState)},
		{ENUM_MAP_N(T_GroupState)},
		{ENUM_MAP_N(T_AggState)},
		{ENUM_MAP_N(T_WindowAggState)},
		{ENUM_MAP_N(T_UniqueState)},
		{ENUM_MAP_N(T_HashState)},
		{ENUM_MAP_N(T_SetOpState)},
		{ENUM_MAP_N(T_LockRowsState)},
		{ENUM_MAP_N(T_LimitState)},

		/*
		 * TAGS FOR PRIMITIVE NODES (primnodes.h)
		 */
		{ENUM_MAP_N(T_Alias)},
		{ENUM_MAP_N(T_RangeVar)},
		{ENUM_MAP_N(T_Expr)},
		{ENUM_MAP_N(T_Var)},
		{ENUM_MAP_N(T_Const)},
		{ENUM_MAP_N(T_Param)},
		{ENUM_MAP_N(T_Aggref)},
		{ENUM_MAP_N(T_WindowFunc)},
		{ENUM_MAP_N(T_ArrayRef)},
		{ENUM_MAP_N(T_FuncExpr)},
		{ENUM_MAP_N(T_NamedArgExpr)},
		{ENUM_MAP_N(T_OpExpr)},
		{ENUM_MAP_N(T_DistinctExpr)},
		{ENUM_MAP_N(T_NullIfExpr)},
		{ENUM_MAP_N(T_ScalarArrayOpExpr)},
		{ENUM_MAP_N(T_BoolExpr)},
		{ENUM_MAP_N(T_SubLink)},
		{ENUM_MAP_N(T_SubPlan)},
		{ENUM_MAP_N(T_AlternativeSubPlan)},
		{ENUM_MAP_N(T_FieldSelect)},
		{ENUM_MAP_N(T_FieldStore)},
		{ENUM_MAP_N(T_RelabelType)},
		{ENUM_MAP_N(T_CoerceViaIO)},
		{ENUM_MAP_N(T_ArrayCoerceExpr)},
		{ENUM_MAP_N(T_ConvertRowtypeExpr)},
		{ENUM_MAP_N(T_CollateExpr)},
		{ENUM_MAP_N(T_CaseExpr)},
		{ENUM_MAP_N(T_CaseWhen)},
		{ENUM_MAP_N(T_CaseTestExpr)},
		{ENUM_MAP_N(T_ArrayExpr)},
		{ENUM_MAP_N(T_RowExpr)},
		{ENUM_MAP_N(T_RowCompareExpr)},
		{ENUM_MAP_N(T_CoalesceExpr)},
		{ENUM_MAP_N(T_MinMaxExpr)},
		{ENUM_MAP_N(T_XmlExpr)},
		{ENUM_MAP_N(T_NullTest)},
		{ENUM_MAP_N(T_BooleanTest)},
		{ENUM_MAP_N(T_CoerceToDomain)},
		{ENUM_MAP_N(T_CoerceToDomainValue)},
		{ENUM_MAP_N(T_SetToDefault)},
		{ENUM_MAP_N(T_CurrentOfExpr)},
		{ENUM_MAP_N(T_TargetEntry)},
		{ENUM_MAP_N(T_RangeTblRef)},
		{ENUM_MAP_N(T_JoinExpr)},
		{ENUM_MAP_N(T_FromExpr)},
		{ENUM_MAP_N(T_IntoClause)},

		/*
		 * TAGS FOR EXPRESSION STATE NODES (execnodes.h)
		 *
		 * These correspond (not always one-for-one) to primitive nodes derived
		 * from Expr.
		 */
		{ENUM_MAP_N(T_ExprState)},
		{ENUM_MAP_N(T_GenericExprState)},
		{ENUM_MAP_N(T_WholeRowVarExprState)},
		{ENUM_MAP_N(T_AggrefExprState)},
		{ENUM_MAP_N(T_WindowFuncExprState)},
		{ENUM_MAP_N(T_ArrayRefExprState)},
		{ENUM_MAP_N(T_FuncExprState)},
		{ENUM_MAP_N(T_ScalarArrayOpExprState)},
		{ENUM_MAP_N(T_BoolExprState)},
		{ENUM_MAP_N(T_SubPlanState)},
		{ENUM_MAP_N(T_AlternativeSubPlanState)},
		{ENUM_MAP_N(T_FieldSelectState)},
		{ENUM_MAP_N(T_FieldStoreState)},
		{ENUM_MAP_N(T_CoerceViaIOState)},
		{ENUM_MAP_N(T_ArrayCoerceExprState)},
		{ENUM_MAP_N(T_ConvertRowtypeExprState)},
		{ENUM_MAP_N(T_CaseExprState)},
		{ENUM_MAP_N(T_CaseWhenState)},
		{ENUM_MAP_N(T_ArrayExprState)},
		{ENUM_MAP_N(T_RowExprState)},
		{ENUM_MAP_N(T_RowCompareExprState)},
		{ENUM_MAP_N(T_CoalesceExprState)},
		{ENUM_MAP_N(T_MinMaxExprState)},
		{ENUM_MAP_N(T_XmlExprState)},
		{ENUM_MAP_N(T_NullTestState)},
		{ENUM_MAP_N(T_CoerceToDomainState)},
		{ENUM_MAP_N(T_DomainConstraintState)},

		/*
		 * TAGS FOR PLANNER NODES (relation.h)
		 */
		{ENUM_MAP_F(T_PlannerInfo)},
		{ENUM_MAP_N(T_PlannerGlobal)},
		{ENUM_MAP_F(T_RelOptInfo)},
		{ENUM_MAP_F(T_IndexOptInfo)},
		{ENUM_MAP_F(T_ParamPathInfo)},
		{ENUM_MAP_F(T_Path)},
		{ENUM_MAP_F(T_IndexPath)},
		{ENUM_MAP_F(T_BitmapHeapPath)},
		{ENUM_MAP_F(T_BitmapAndPath)},
		{ENUM_MAP_F(T_BitmapOrPath)},
		{ENUM_MAP_F(T_NestPath)},
		{ENUM_MAP_F(T_MergePath)},
		{ENUM_MAP_F(T_HashPath)},
		{ENUM_MAP_F(T_TidPath)},
		{ENUM_MAP_F(T_ForeignPath)},
		{ENUM_MAP_F(T_AppendPath)},
		{ENUM_MAP_F(T_MergeAppendPath)},
		{ENUM_MAP_F(T_ResultPath)},
		{ENUM_MAP_F(T_MaterialPath)},
		{ENUM_MAP_F(T_UniquePath)},
		{ENUM_MAP_N(T_EquivalenceClass)},
		{ENUM_MAP_N(T_EquivalenceMember)},
		{ENUM_MAP_N(T_PathKey)},
		{ENUM_MAP_F(T_RestrictInfo)},
		{ENUM_MAP_N(T_PlaceHolderVar)},
		{ENUM_MAP_N(T_SpecialJoinInfo)},
#		if PG_VERSION_NUM/100 >= 903
		{ENUM_MAP_N(T_LateralJoinInfo)},
#		endif
		{ENUM_MAP_N(T_AppendRelInfo)},
		{ENUM_MAP_N(T_PlaceHolderInfo)},
		{ENUM_MAP_N(T_MinMaxAggInfo)},
		{ENUM_MAP_N(T_PlannerParamItem)},

		/*
		 * TAGS FOR MEMORY NODES (memnodes.h)
		 */
		{ENUM_MAP_N(T_MemoryContext)},
		{ENUM_MAP_N(T_AllocSetContext)},

		/*
		 * TAGS FOR VALUE NODES (value.h)
		 */
		{ENUM_MAP_N(T_Value)},
		{ENUM_MAP_N(T_Integer)},
		{ENUM_MAP_N(T_Float)},
		{ENUM_MAP_N(T_String)},
		{ENUM_MAP_N(T_BitString)},
		{ENUM_MAP_N(T_Null)},

		/*
		 * TAGS FOR LIST NODES (pg_list.h)
		 */
		{ENUM_MAP_F(T_List)},
		{ENUM_MAP_N(T_IntList)},
		{ENUM_MAP_N(T_OidList)},

		/*
		 * TAGS FOR STATEMENT NODES (mostly in parsenodes.h)
		 */
		{ENUM_MAP_N(T_Query)},
		{ENUM_MAP_N(T_PlannedStmt)},
		{ENUM_MAP_N(T_InsertStmt)},
		{ENUM_MAP_N(T_DeleteStmt)},
		{ENUM_MAP_N(T_UpdateStmt)},
		{ENUM_MAP_N(T_SelectStmt)},
		{ENUM_MAP_N(T_AlterTableStmt)},
		{ENUM_MAP_N(T_AlterTableCmd)},
		{ENUM_MAP_N(T_AlterDomainStmt)},
		{ENUM_MAP_N(T_SetOperationStmt)},
		{ENUM_MAP_N(T_GrantStmt)},
		{ENUM_MAP_N(T_GrantRoleStmt)},
		{ENUM_MAP_N(T_AlterDefaultPrivilegesStmt)},
		{ENUM_MAP_N(T_ClosePortalStmt)},
		{ENUM_MAP_N(T_ClusterStmt)},
		{ENUM_MAP_N(T_CopyStmt)},
		{ENUM_MAP_N(T_CreateStmt)},
		{ENUM_MAP_N(T_DefineStmt)},
		{ENUM_MAP_N(T_DropStmt)},
		{ENUM_MAP_N(T_TruncateStmt)},
		{ENUM_MAP_N(T_CommentStmt)},
		{ENUM_MAP_N(T_FetchStmt)},
		{ENUM_MAP_N(T_IndexStmt)},
		{ENUM_MAP_N(T_CreateFunctionStmt)},
		{ENUM_MAP_N(T_AlterFunctionStmt)},
		{ENUM_MAP_N(T_DoStmt)},
		{ENUM_MAP_N(T_RenameStmt)},
		{ENUM_MAP_N(T_RuleStmt)},
		{ENUM_MAP_N(T_NotifyStmt)},
		{ENUM_MAP_N(T_ListenStmt)},
		{ENUM_MAP_N(T_UnlistenStmt)},
		{ENUM_MAP_N(T_TransactionStmt)},
		{ENUM_MAP_N(T_ViewStmt)},
		{ENUM_MAP_N(T_LoadStmt)},
		{ENUM_MAP_N(T_CreateDomainStmt)},
		{ENUM_MAP_N(T_CreatedbStmt)},
		{ENUM_MAP_N(T_DropdbStmt)},
		{ENUM_MAP_N(T_VacuumStmt)},
		{ENUM_MAP_N(T_ExplainStmt)},
		{ENUM_MAP_N(T_CreateTableAsStmt)},
		{ENUM_MAP_N(T_CreateSeqStmt)},
		{ENUM_MAP_N(T_AlterSeqStmt)},
		{ENUM_MAP_N(T_VariableSetStmt)},
		{ENUM_MAP_N(T_VariableShowStmt)},
		{ENUM_MAP_N(T_DiscardStmt)},
		{ENUM_MAP_N(T_CreateTrigStmt)},
		{ENUM_MAP_N(T_CreatePLangStmt)},
		{ENUM_MAP_N(T_CreateRoleStmt)},
		{ENUM_MAP_N(T_AlterRoleStmt)},
		{ENUM_MAP_N(T_DropRoleStmt)},
		{ENUM_MAP_N(T_LockStmt)},
		{ENUM_MAP_N(T_ConstraintsSetStmt)},
		{ENUM_MAP_N(T_ReindexStmt)},
		{ENUM_MAP_N(T_CheckPointStmt)},
		{ENUM_MAP_N(T_CreateSchemaStmt)},
		{ENUM_MAP_N(T_AlterDatabaseStmt)},
		{ENUM_MAP_N(T_AlterDatabaseSetStmt)},
		{ENUM_MAP_N(T_AlterRoleSetStmt)},
		{ENUM_MAP_N(T_CreateConversionStmt)},
		{ENUM_MAP_N(T_CreateCastStmt)},
		{ENUM_MAP_N(T_CreateOpClassStmt)},
		{ENUM_MAP_N(T_CreateOpFamilyStmt)},
		{ENUM_MAP_N(T_AlterOpFamilyStmt)},
		{ENUM_MAP_N(T_PrepareStmt)},
		{ENUM_MAP_N(T_ExecuteStmt)},
		{ENUM_MAP_N(T_DeallocateStmt)},
		{ENUM_MAP_N(T_DeclareCursorStmt)},
		{ENUM_MAP_N(T_CreateTableSpaceStmt)},
		{ENUM_MAP_N(T_DropTableSpaceStmt)},
		{ENUM_MAP_N(T_AlterObjectSchemaStmt)},
		{ENUM_MAP_N(T_AlterOwnerStmt)},
		{ENUM_MAP_N(T_DropOwnedStmt)},
		{ENUM_MAP_N(T_ReassignOwnedStmt)},
		{ENUM_MAP_N(T_CompositeTypeStmt)},
		{ENUM_MAP_N(T_CreateEnumStmt)},
		{ENUM_MAP_N(T_CreateRangeStmt)},
		{ENUM_MAP_N(T_AlterEnumStmt)},
		{ENUM_MAP_N(T_AlterTSDictionaryStmt)},
		{ENUM_MAP_N(T_AlterTSConfigurationStmt)},
		{ENUM_MAP_N(T_CreateFdwStmt)},
		{ENUM_MAP_N(T_AlterFdwStmt)},
		{ENUM_MAP_N(T_CreateForeignServerStmt)},
		{ENUM_MAP_N(T_AlterForeignServerStmt)},
		{ENUM_MAP_N(T_CreateUserMappingStmt)},
		{ENUM_MAP_N(T_AlterUserMappingStmt)},
		{ENUM_MAP_N(T_DropUserMappingStmt)},
		{ENUM_MAP_N(T_AlterTableSpaceOptionsStmt)},
		{ENUM_MAP_N(T_SecLabelStmt)},
		{ENUM_MAP_N(T_CreateForeignTableStmt)},
		{ENUM_MAP_N(T_CreateExtensionStmt)},
		{ENUM_MAP_N(T_AlterExtensionStmt)},
		{ENUM_MAP_N(T_AlterExtensionContentsStmt)},
#		if PG_VERSION_NUM/100 >= 903
		{ENUM_MAP_N(T_CreateEventTrigStmt)},
		{ENUM_MAP_N(T_AlterEventTrigStmt)},
		{ENUM_MAP_N(T_RefreshMatViewStmt)},
#		endif

		/*
		 * TAGS FOR PARSE TREE NODES (parsenodes.h)
		 */
		{ENUM_MAP_N(T_A_Expr)},
		{ENUM_MAP_N(T_ColumnRef)},
		{ENUM_MAP_N(T_ParamRef)},
		{ENUM_MAP_N(T_A_Const)},
		{ENUM_MAP_N(T_FuncCall)},
		{ENUM_MAP_N(T_A_Star)},
		{ENUM_MAP_N(T_A_Indices)},
		{ENUM_MAP_N(T_A_Indirection)},
		{ENUM_MAP_N(T_A_ArrayExpr)},
		{ENUM_MAP_N(T_ResTarget)},
		{ENUM_MAP_N(T_TypeCast)},
		{ENUM_MAP_N(T_CollateClause)},
		{ENUM_MAP_N(T_SortBy)},
		{ENUM_MAP_N(T_WindowDef)},
		{ENUM_MAP_N(T_RangeSubselect)},
		{ENUM_MAP_N(T_RangeFunction)},
		{ENUM_MAP_N(T_TypeName)},
		{ENUM_MAP_N(T_ColumnDef)},
		{ENUM_MAP_N(T_IndexElem)},
		{ENUM_MAP_N(T_Constraint)},
		{ENUM_MAP_N(T_DefElem)},
		{ENUM_MAP_N(T_RangeTblEntry)},
		{ENUM_MAP_N(T_SortGroupClause)},
		{ENUM_MAP_N(T_WindowClause)},
		{ENUM_MAP_N(T_PrivGrantee)},
		{ENUM_MAP_N(T_FuncWithArgs)},
		{ENUM_MAP_N(T_AccessPriv)},
		{ENUM_MAP_N(T_CreateOpClassItem)},
		{ENUM_MAP_N(T_TableLikeClause)},
		{ENUM_MAP_N(T_FunctionParameter)},
		{ENUM_MAP_N(T_LockingClause)},
		{ENUM_MAP_N(T_RowMarkClause)},
		{ENUM_MAP_N(T_XmlSerialize)},
		{ENUM_MAP_N(T_WithClause)},
		{ENUM_MAP_N(T_CommonTableExpr)},

		/*
		 * TAGS FOR REPLICATION GRAMMAR PARSE NODES (replnodes.h)
		 */
		{ENUM_MAP_N(T_IdentifySystemCmd)},
		{ENUM_MAP_N(T_BaseBackupCmd)},
		{ENUM_MAP_N(T_StartReplicationCmd)},
#		if PG_VERSION_NUM/100 >= 903
		{ENUM_MAP_N(T_TimeLineHistoryCmd)},
#		endif

		/*
		 * TAGS FOR RANDOM OTHER STUFF
		 *
		 * These are objects that aren't part of parse/plan/execute node tree
		 * structures)}, but we give them NodeTags anyway for identification
		 * purposes (usually because they are involved in APIs where we want to
		 * pass multiple object types through the same pointer).
		 */
		{ENUM_MAP_N(T_TriggerData)},		/* in commands/trigger.h */
#		if PG_VERSION_NUM/100 >= 903
		{ENUM_MAP_N(T_EventTriggerData)},			/* in commands/even{TAG_STR(T_trigger.h */
#		endif
		{ENUM_MAP_N(T_ReturnSetInfo)},			/* in nodes/execnodes.h */
		{ENUM_MAP_N(T_WindowObjectData)},			/* private in nodeWindowAgg.c */
		{ENUM_MAP_N(T_TIDBitmap)},				/* in nodes/tidbitmap.h */
		{ENUM_MAP_N(T_InlineCodeBlock)},			/* in nodes/parsenodes.h */
		{ENUM_MAP_N(T_FdwRoutine)},				/* in foreign/fdwapi.h */
		{0,"",NULL}
	};

static DebugNode*
getNode(DebugGraph *graph, PlannerInfo *root, Node *node)
{
	DebugNode *n = NULL;

	Assert(graph);
	Assert(root && IsA(root, PlannerInfo));

	if (node)
	{
		const NodeTagMapType *i;
		for (i = nodetag_maps; i->tag != node->type && i->name[0] != '\0'; i++);

		n = newDebugNodeByPointer(graph, node, i->name);
		Assert(n);
		if (!n->create_node_again)
		{
			WRITE_CUSTOM_FIELD("address", "%p", node);
			WRITE_CUSTOM_FIELD("type", "%s", getNodeTagName(node->type));
			if (i->setNode)
				i->setNode(graph, n, root, node);
		}
	}

	return n;
}

const char*
getNodeTagName(NodeTag tag)
{
	const NodeTagMapType *i;
	for (i = nodetag_maps; i->tag != tag && i->name[0] != '\0'; i++);
	return i->name;
}

/* ************************** enum JoinType ******************************* */

typedef struct JoinTypeMapType
{
	JoinType type;
	const char *name;
} JoinTypeMapType;

#define ENUM_MAP(name) name, CppAsString(name)

const char*
getJoinTypeName(JoinType t)
{
	const JoinTypeMapType types[] = {
			{ENUM_MAP(JOIN_INNER)},					/* matching tuple pairs only */
			{ENUM_MAP(JOIN_LEFT)},					/* pairs + unmatched LHS tuples */
			{ENUM_MAP(JOIN_FULL)},					/* pairs + unmatched LHS + unmatched RHS */
			{ENUM_MAP(JOIN_RIGHT)},					/* pairs + unmatched RHS tuples */

		/*
		 * Semijoins and anti-semijoins (as defined in relational theory) do not
		 * appear in the SQL JOIN syntax, but there are standard idioms for
		 * representing them (e.g., using EXISTS).	The planner recognizes these
		 * cases and converts them to joins.  So the planner and executor must
		 * support these codes.  NOTE: in JOIN_SEMI output, it is unspecified
		 * which matching RHS row is joined to.  In JOIN_ANTI output, the row is
		 * guaranteed to be null-extended.
		 */
			{ENUM_MAP(JOIN_SEMI)},					/* 1 copy of each LHS row that has match(es) */
			{ENUM_MAP(JOIN_ANTI)},					/* 1 copy of each LHS row that has no match */

		/*
		 * These codes are used internally in the planner, but are not supported
		 * by the executor (nor, indeed, by most of the planner).
		 */
			{ENUM_MAP(JOIN_UNIQUE_OUTER)},			/* LHS path must be made unique */
			{ENUM_MAP(JOIN_UNIQUE_INNER)},			/* RHS path must be made unique */
			{0,""}
	};
	const JoinTypeMapType *i;
	for (i=types; i->type != t && i->name[0] != '\0'; i++);
	return i->name;
}

/* ******************************* UTILS ************************************ */

static const char*
get_relation_name(PlannerInfo *root, int relid)
{
	RangeTblEntry *rte;

	Assert(relid <= list_length(root->parse->rtable));

	rte = rt_fetch(relid, root->parse->rtable);
	return rte->eref->aliasname;
}

static void
addRelids(DebugNode *n, const char *name, PlannerInfo *root, Relids relids)
{
	if (!relids)
	{
		addDebugNodeAttribute(n, name, "NULL");
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

			addDebugNodeAttribute(n, str.data, relname);
		}
		bms_free(tmprelids);
		pfree(str.data);
	}
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

static const char*
getExpr(const Node *expr, const List *rtable)
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
			aux = getExpr(get_leftop((const Expr *) e), rtable);
			appendStringInfo(&str, "%s", aux);
			pfree((void*)aux);

			appendStringInfo(&str, " %s ",
					((opname != NULL) ? opname : "(invalid operator)"));

			aux = getExpr(get_rightop((const Expr *) e), rtable);
			appendStringInfo(&str, "%s", aux);
			pfree((void*)aux);
		}
		else
		{
			const char *aux;
			/* we print prefix and postfix ops the same... */
			appendStringInfo(&str, "%s ",
					((opname != NULL) ? opname : "(invalid operator)"));
			aux = getExpr(get_leftop((const Expr *) e), rtable);
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
			const char *aux = getExpr(lfirst(l), rtable);
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

static const char *
getPathkeys(const List *pathkeys, const List *rtable)
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
				aux = getExpr((Node *) mem->em_expr, rtable);
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

static DebugNode*
getTablespace(DebugGraph *graph, Oid tablespace)
{
	DebugNode *n;
	Assert(graph);
	Assert(tablespace >= 0);

	{StringInfoData str;
	initStringInfo(&str);
	appendStringInfo(&str, "tablespace_%u", tablespace);
	n = newDebugNode(graph, str.data, "TableSpace");
	Assert(n);
		pfree(str.data);}

	if (!n->create_node_again)
	{
		addDebugNodeAttributeArgs(n, "oid", "%u", tablespace);
		addDebugNodeAttributeArgs(n, "name", "%s",
				get_tablespace_name(tablespace));
		{
			Cost c_rand, c_seq;
			get_tablespace_page_costs(tablespace, &c_rand, &c_seq);
			addDebugNodeAttributeArgs(n, "seq_page_cost", "%lf", c_seq);
			addDebugNodeAttributeArgs(n, "random_page_cost", "%lf", c_rand);
		}
	}

	return n;
}
