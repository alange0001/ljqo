/*
 * debuggraph_node.c
 *
 *   DebugGraph structures and functions. They are used to generate directed
 *   graphs for debug purposes.
 *
 *   This file is a copy of outfuncs.c from PostgreSQL project.
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
#include "debuggraph_node.h"

#include <ctype.h>

#include <lib/stringinfo.h>
#include <nodes/nodes.h>
#include <nodes/plannodes.h>
#include <nodes/relation.h>
#include <utils/datum.h>


/*
 * Macros to simplify output of different kinds of fields.	Use these
 * wherever possible to reduce the chance for silly typos.	Note that these
 * hard-wire conventions about the names of the local variables in an Out
 * routine.
 */

/* Write the label for the node type */
#define WRITE_NODE_TYPE(nodelabel) \
	DebugNode *deb_node = newDebugNodeByPointer(graph, (void*)node, nodelabel)

/* Write an integer field (anything written as ":fldname %d") */
#define WRITE_INT_FIELD(fldname) \
	addDebugNodeAttributeArgs(deb_node, CppAsString(fldname), "%d", \
			node->fldname)

/* Write an unsigned integer field (anything written as ":fldname %u") */
#define WRITE_UINT_FIELD(fldname) \
	addDebugNodeAttributeArgs(deb_node, CppAsString(fldname), "%u", \
			node->fldname)

/* Write an OID field (don't hard-wire assumption that OID is same as uint) */
#define WRITE_OID_FIELD(fldname) \
	addDebugNodeAttributeArgs(deb_node, CppAsString(fldname), "%u", \
			node->fldname)

/* Write a long-integer field */
#define WRITE_LONG_FIELD(fldname) \
	addDebugNodeAttributeArgs(deb_node, CppAsString(fldname), "%ld", \
			node->fldname)

/* Write a char field (ie, one ascii character) */
#define WRITE_CHAR_FIELD(fldname) \
	addDebugNodeAttributeArgs(deb_node, CppAsString(fldname), "%c", \
			node->fldname)

/* Write an enumerated-type field as an integer code */
#define WRITE_ENUM_FIELD(fldname, enumtype) \
	addDebugNodeAttributeArgs(deb_node, CppAsString(fldname), "%d", \
					 (int) node->fldname)

/* Write a float field --- caller must give format to define precision */
#define WRITE_FLOAT_FIELD(fldname,format) \
	addDebugNodeAttributeArgs(deb_node, CppAsString(fldname), format, \
			node->fldname)

/* Write a boolean field */
#define WRITE_BOOL_FIELD(fldname) \
	addDebugNodeAttributeArgs(deb_node, CppAsString(fldname), "%s", \
					 booltostr(node->fldname))

/* Write a character-string (possibly NULL) field */
#define WRITE_STRING_FIELD(fldname) \
	{ \
		StringInfoData aux; \
		initStringInfo(&aux); \
		_outToken(&aux, node->fldname); \
		addDebugNodeAttribute(deb_node, CppAsString(fldname), aux.data); \
		pfree(aux.data); \
	}

/* Write a parse location field (actually same as INT case) */
#define WRITE_LOCATION_FIELD(fldname) \
	addDebugNodeAttributeArgs(deb_node, CppAsString(fldname), "%d", \
			node->fldname)

/* Write a Node field */
#define WRITE_NODE_FIELD(fldname) \
	{ \
		DebugNode *aux; \
		char *str; \
		_outNode(graph, node->fldname, &aux, &str); \
		if (aux) \
			newDebugEdgeByName(graph, deb_node->internal_name, \
					aux->internal_name, CppAsString(fldname)); \
		else \
		{ \
			addDebugNodeAttribute(deb_node, CppAsString(fldname), str); \
			pfree(str); \
		} \
	}

/* Write a bitmapset field */
#define WRITE_BITMAPSET_FIELD(fldname) \
	{ \
		StringInfoData aux; \
		initStringInfo(&aux); \
		_outBitmapset(&aux, node->fldname); \
		addDebugNodeAttribute(deb_node, CppAsString(fldname), aux.data); \
		pfree(aux.data); \
	}


#define booltostr(x)  ((x) ? "true" : "false")

static void _outNode(DebugGraph *graph, const void *obj,
		DebugNode **out_node, char **out_str);


/*
 * _outToken
 *	  Convert an ordinary string (eg, an identifier) into a form that
 *	  will be decoded back to a plain token by read.c's functions.
 *
 *	  If a null or empty string is given, it is encoded as "<>".
 */
static void
_outToken(StringInfo str, const char *s)
{
	if (s == NULL || *s == '\0')
	{
		appendStringInfo(str, "<>");
		return;
	}

	/*
	 * Look for characters or patterns that are treated specially by read.c
	 * (either in pg_strtok() or in nodeRead()), and therefore need a
	 * protective backslash.
	 */
	/* These characters only need to be quoted at the start of the string */
	if (*s == '<' ||
		*s == '\"' ||
		isdigit((unsigned char) *s) ||
		((*s == '+' || *s == '-') &&
		 (isdigit((unsigned char) s[1]) || s[1] == '.')))
		appendStringInfoChar(str, '\\');
	while (*s)
	{
		/* These chars must be backslashed anywhere in the string */
		if (*s == ' ' || *s == '\n' || *s == '\t' ||
			*s == '(' || *s == ')' || *s == '{' || *s == '}' ||
			*s == '\\')
			appendStringInfoChar(str, '\\');
		appendStringInfoChar(str, *s++);
	}
}

static DebugNode*
_outList(DebugGraph *graph, const List *node)
{
	DebugNode *deb_node;
	const ListCell *lc;

	if (IsA(node, IntList))
		deb_node = newDebugNodeByPointer(graph, (void*) node, "INTLIST");
	else if (IsA(node, OidList))
		deb_node = newDebugNodeByPointer(graph, (void*) node, "OIDLIST");
	else
		deb_node = newDebugNodeByPointer(graph, (void*) node, "LIST");

	foreach(lc, node)
	{
		/*
		 * For the sake of backward compatibility, we emit a slightly
		 * different whitespace format for lists of nodes vs. other types of
		 * lists. XXX: is this necessary?
		 */
		if (IsA(node, List))
		{
			DebugNode *aux;
			char      *str;
			_outNode(graph, lfirst(lc), &aux, &str);
			if (aux)
				newDebugEdgeByName(graph, deb_node->internal_name,
						aux->internal_name, "");
			else
			{
				addDebugNodeAttribute(deb_node, "[]", str);
				pfree(str);
			}
		}
		else if (IsA(node, IntList))
			addDebugNodeAttributeArgs(deb_node, "[]", "%d", lfirst_int(lc));
		else if (IsA(node, OidList))
			addDebugNodeAttributeArgs(deb_node, "[]", "%u", lfirst_oid(lc));
		else
			elog(ERROR, "unrecognized list node type: %d",
				 (int) node->type);
	}

	return deb_node;
}

/*
 * _outBitmapset -
 *	   converts a bitmap set of integers
 *
 * Note: the output format is "(b int int ...)", similar to an integer List.
 */
static void
_outBitmapset(StringInfo str, const Bitmapset *bms)
{
	Bitmapset  *tmpset;
	int			x;

	appendStringInfoChar(str, '(');
	appendStringInfoChar(str, 'b');
	tmpset = bms_copy(bms);
	while ((x = bms_first_member(tmpset)) >= 0)
		appendStringInfo(str, " %d", x);
	bms_free(tmpset);
	appendStringInfoChar(str, ')');
}

/*
 * Print the value of a Datum given its type.
 */
static void
_outDatum(StringInfo str, Datum value, int typlen, bool typbyval)
{
	Size		length,
				i;
	char	   *s;

	length = datumGetSize(value, typbyval, typlen);

	if (typbyval)
	{
		s = (char *) (&value);
		appendStringInfo(str, "%u [ ", (unsigned int) length);
		for (i = 0; i < (Size) sizeof(Datum); i++)
			appendStringInfo(str, "%d ", (int) (s[i]));
		appendStringInfo(str, "]");
	}
	else
	{
		s = (char *) DatumGetPointer(value);
		if (!PointerIsValid(s))
			appendStringInfo(str, "0 [ ]");
		else
		{
			appendStringInfo(str, "%u [ ", (unsigned int) length);
			for (i = 0; i < length; i++)
				appendStringInfo(str, "%d ", (int) (s[i]));
			appendStringInfo(str, "]");
		}
	}
}


/*
 *	Stuff from plannodes.h
 */

static DebugNode*
_outPlannedStmt(DebugGraph *graph, const PlannedStmt *node)
{
	WRITE_NODE_TYPE("PLANNEDSTMT");

	WRITE_ENUM_FIELD(commandType, CmdType);
	WRITE_UINT_FIELD(queryId);
	WRITE_BOOL_FIELD(hasReturning);
	WRITE_BOOL_FIELD(hasModifyingCTE);
	WRITE_BOOL_FIELD(canSetTag);
	WRITE_BOOL_FIELD(transientPlan);
	WRITE_NODE_FIELD(planTree);
	WRITE_NODE_FIELD(rtable);
	WRITE_NODE_FIELD(resultRelations);
	WRITE_NODE_FIELD(utilityStmt);
	WRITE_NODE_FIELD(subplans);
	WRITE_BITMAPSET_FIELD(rewindPlanIDs);
	WRITE_NODE_FIELD(rowMarks);
	WRITE_NODE_FIELD(relationOids);
	WRITE_NODE_FIELD(invalItems);
	WRITE_INT_FIELD(nParamExec);

	return deb_node;
}

/*
 * print the basic stuff of all nodes that inherit from Plan
 */
static void
_outPlanInfo(DebugGraph *graph, DebugNode *deb_node, const Plan *node)
{
	WRITE_FLOAT_FIELD(startup_cost, "%.2f");
	WRITE_FLOAT_FIELD(total_cost, "%.2f");
	WRITE_FLOAT_FIELD(plan_rows, "%.0f");
	WRITE_INT_FIELD(plan_width);
	WRITE_NODE_FIELD(targetlist);
	WRITE_NODE_FIELD(qual);
	WRITE_NODE_FIELD(lefttree);
	WRITE_NODE_FIELD(righttree);
	WRITE_NODE_FIELD(initPlan);
	WRITE_BITMAPSET_FIELD(extParam);
	WRITE_BITMAPSET_FIELD(allParam);
}

/*
 * print the basic stuff of all nodes that inherit from Scan
 */
static void
_outScanInfo(DebugGraph *graph, DebugNode *deb_node, const Scan *node)
{
	_outPlanInfo(graph, deb_node, (const Plan *) node);

	WRITE_UINT_FIELD(scanrelid);
}

/*
 * print the basic stuff of all nodes that inherit from Join
 */
static void
_outJoinPlanInfo(DebugGraph *graph, DebugNode *deb_node, const Join *node)
{
	_outPlanInfo(graph, deb_node, (const Plan *) node);

	WRITE_ENUM_FIELD(jointype, JoinType);
	WRITE_NODE_FIELD(joinqual);
}


static DebugNode*
_outPlan(DebugGraph *graph, const Plan *node)
{
	WRITE_NODE_TYPE("PLAN");

	_outPlanInfo(graph, deb_node, (const Plan *) node);

	return deb_node;
}

static DebugNode*
_outResult(DebugGraph *graph, const Result *node)
{
	WRITE_NODE_TYPE("RESULT");

	_outPlanInfo(graph, deb_node, (const Plan *) node);

	WRITE_NODE_FIELD(resconstantqual);

	return deb_node;
}

static DebugNode*
_outModifyTable(DebugGraph *graph, const ModifyTable *node)
{
	WRITE_NODE_TYPE("MODIFYTABLE");

	_outPlanInfo(graph, deb_node, (const Plan *) node);

	WRITE_ENUM_FIELD(operation, CmdType);
	WRITE_BOOL_FIELD(canSetTag);
	WRITE_NODE_FIELD(resultRelations);
	WRITE_INT_FIELD(resultRelIndex);
	WRITE_NODE_FIELD(plans);
	WRITE_NODE_FIELD(returningLists);
	WRITE_NODE_FIELD(fdwPrivLists);
	WRITE_NODE_FIELD(rowMarks);
	WRITE_INT_FIELD(epqParam);

	return deb_node;
}

static DebugNode*
_outAppend(DebugGraph *graph, const Append *node)
{
	WRITE_NODE_TYPE("APPEND");

	_outPlanInfo(graph, deb_node, (const Plan *) node);

	WRITE_NODE_FIELD(appendplans);

	return deb_node;
}

static DebugNode*
_outMergeAppend(DebugGraph *graph, const MergeAppend *node)
{
	StringInfo  str = makeStringInfo();
	int			i;

	WRITE_NODE_TYPE("MERGEAPPEND");

	_outPlanInfo(graph, deb_node, (const Plan *) node);

	WRITE_NODE_FIELD(mergeplans);

	WRITE_INT_FIELD(numCols);

	for (i = 0; i < node->numCols; i++)
		appendStringInfo(str, " %d", node->sortColIdx[i]);
	addDebugNodeAttribute(deb_node, "sortColIdx", str->data);

	resetStringInfo(str);
	for (i = 0; i < node->numCols; i++)
		appendStringInfo(str, " %u", node->sortOperators[i]);
	addDebugNodeAttribute(deb_node, "sortOperators", str->data);

	resetStringInfo(str);
	for (i = 0; i < node->numCols; i++)
		appendStringInfo(str, " %u", node->collations[i]);
	addDebugNodeAttribute(deb_node, "collations", str->data);

	resetStringInfo(str);
	for (i = 0; i < node->numCols; i++)
		appendStringInfo(str, " %s", booltostr(node->nullsFirst[i]));
	addDebugNodeAttribute(deb_node, "nullsFirst", str->data);

	pfree(str->data);
	pfree(str);
	return deb_node;
}

static DebugNode*
_outRecursiveUnion(DebugGraph *graph, const RecursiveUnion *node)
{
	StringInfo  str = makeStringInfo();
	int			i;
	WRITE_NODE_TYPE("RECURSIVEUNION");

	_outPlanInfo(graph, deb_node, (const Plan *) node);

	WRITE_INT_FIELD(wtParam);
	WRITE_INT_FIELD(numCols);

	for (i = 0; i < node->numCols; i++)
		appendStringInfo(str, " %d", node->dupColIdx[i]);
	addDebugNodeAttribute(deb_node, "dupColIdx", str->data);

	resetStringInfo(str);
	for (i = 0; i < node->numCols; i++)
		appendStringInfo(str, " %u", node->dupOperators[i]);
	addDebugNodeAttribute(deb_node, "dupOperators", str->data);

	WRITE_LONG_FIELD(numGroups);

	pfree(str->data);
	pfree(str);
	return deb_node;
}

static DebugNode*
_outBitmapAnd(DebugGraph *graph, const BitmapAnd *node)
{
	WRITE_NODE_TYPE("BITMAPAND");

	_outPlanInfo(graph, deb_node, (const Plan *) node);

	WRITE_NODE_FIELD(bitmapplans);

	return deb_node;
}

static DebugNode*
_outBitmapOr(DebugGraph *graph, const BitmapOr *node)
{
	WRITE_NODE_TYPE("BITMAPOR");

	_outPlanInfo(graph, deb_node, (const Plan *) node);

	WRITE_NODE_FIELD(bitmapplans);

	return deb_node;
}

static DebugNode*
_outScan(DebugGraph *graph, const Scan *node)
{
	WRITE_NODE_TYPE("SCAN");

	_outScanInfo(graph, deb_node, node);

	return deb_node;
}

static DebugNode*
_outSeqScan(DebugGraph *graph, const SeqScan *node)
{
	WRITE_NODE_TYPE("SEQSCAN");

	_outScanInfo(graph, deb_node, (const Scan *) node);

	return deb_node;
}

static DebugNode*
_outIndexScan(DebugGraph *graph, const IndexScan *node)
{
	WRITE_NODE_TYPE("INDEXSCAN");

	_outScanInfo(graph, deb_node, (const Scan *) node);

	WRITE_OID_FIELD(indexid);
	WRITE_NODE_FIELD(indexqual);
	WRITE_NODE_FIELD(indexqualorig);
	WRITE_NODE_FIELD(indexorderby);
	WRITE_NODE_FIELD(indexorderbyorig);
	WRITE_ENUM_FIELD(indexorderdir, ScanDirection);

	return deb_node;
}

static DebugNode*
_outIndexOnlyScan(DebugGraph *graph, const IndexOnlyScan *node)
{
	WRITE_NODE_TYPE("INDEXONLYSCAN");

	_outScanInfo(graph, deb_node, (const Scan *) node);

	WRITE_OID_FIELD(indexid);
	WRITE_NODE_FIELD(indexqual);
	WRITE_NODE_FIELD(indexorderby);
	WRITE_NODE_FIELD(indextlist);
	WRITE_ENUM_FIELD(indexorderdir, ScanDirection);

	return deb_node;
}

static DebugNode*
_outBitmapIndexScan(DebugGraph *graph, const BitmapIndexScan *node)
{
	WRITE_NODE_TYPE("BITMAPINDEXSCAN");

	_outScanInfo(graph, deb_node, (const Scan *) node);

	WRITE_OID_FIELD(indexid);
	WRITE_NODE_FIELD(indexqual);
	WRITE_NODE_FIELD(indexqualorig);

	return deb_node;
}

static DebugNode*
_outBitmapHeapScan(DebugGraph *graph, const BitmapHeapScan *node)
{
	WRITE_NODE_TYPE("BITMAPHEAPSCAN");

	_outScanInfo(graph, deb_node, (const Scan *) node);

	WRITE_NODE_FIELD(bitmapqualorig);

	return deb_node;
}

static DebugNode*
_outTidScan(DebugGraph *graph, const TidScan *node)
{
	WRITE_NODE_TYPE("TIDSCAN");

	_outScanInfo(graph, deb_node, (const Scan *) node);

	WRITE_NODE_FIELD(tidquals);

	return deb_node;
}

static DebugNode*
_outSubqueryScan(DebugGraph *graph, const SubqueryScan *node)
{
	WRITE_NODE_TYPE("SUBQUERYSCAN");

	_outScanInfo(graph, deb_node, (const Scan *) node);

	WRITE_NODE_FIELD(subplan);

	return deb_node;
}

static DebugNode*
_outFunctionScan(DebugGraph *graph, const FunctionScan *node)
{
	WRITE_NODE_TYPE("FUNCTIONSCAN");

	_outScanInfo(graph, deb_node, (const Scan *) node);

	WRITE_NODE_FIELD(funcexpr);
	WRITE_NODE_FIELD(funccolnames);
	WRITE_NODE_FIELD(funccoltypes);
	WRITE_NODE_FIELD(funccoltypmods);
	WRITE_NODE_FIELD(funccolcollations);

	return deb_node;
}

static DebugNode*
_outValuesScan(DebugGraph *graph, const ValuesScan *node)
{
	WRITE_NODE_TYPE("VALUESSCAN");

	_outScanInfo(graph, deb_node, (const Scan *) node);

	WRITE_NODE_FIELD(values_lists);

	return deb_node;
}

static DebugNode*
_outCteScan(DebugGraph *graph, const CteScan *node)
{
	WRITE_NODE_TYPE("CTESCAN");

	_outScanInfo(graph, deb_node, (const Scan *) node);

	WRITE_INT_FIELD(ctePlanId);
	WRITE_INT_FIELD(cteParam);

	return deb_node;
}

static DebugNode*
_outWorkTableScan(DebugGraph *graph, const WorkTableScan *node)
{
	WRITE_NODE_TYPE("WORKTABLESCAN");

	_outScanInfo(graph, deb_node, (const Scan *) node);

	WRITE_INT_FIELD(wtParam);

	return deb_node;
}

static DebugNode*
_outForeignScan(DebugGraph *graph, const ForeignScan *node)
{
	WRITE_NODE_TYPE("FOREIGNSCAN");

	_outScanInfo(graph, deb_node, (const Scan *) node);

	WRITE_NODE_FIELD(fdw_exprs);
	WRITE_NODE_FIELD(fdw_private);
	WRITE_BOOL_FIELD(fsSystemCol);

	return deb_node;
}

static DebugNode*
_outJoin(DebugGraph *graph, const Join *node)
{
	WRITE_NODE_TYPE("JOIN");

	_outJoinPlanInfo(graph, deb_node, (const Join *) node);

	return deb_node;
}

static DebugNode*
_outNestLoop(DebugGraph *graph, const NestLoop *node)
{
	WRITE_NODE_TYPE("NESTLOOP");

	_outJoinPlanInfo(graph, deb_node, (const Join *) node);

	WRITE_NODE_FIELD(nestParams);

	return deb_node;
}

static DebugNode*
_outMergeJoin(DebugGraph *graph, const MergeJoin *node)
{
	StringInfo  str = makeStringInfo();
	int			numCols;
	int			i;

	WRITE_NODE_TYPE("MERGEJOIN");

	_outJoinPlanInfo(graph, deb_node, (const Join *) node);

	WRITE_NODE_FIELD(mergeclauses);

	numCols = list_length(node->mergeclauses);

	resetStringInfo(str);
	for (i = 0; i < numCols; i++)
		appendStringInfo(str, " %u", node->mergeFamilies[i]);
	addDebugNodeAttribute(deb_node, "mergeFamilies", str->data);

	resetStringInfo(str);
	for (i = 0; i < numCols; i++)
		appendStringInfo(str, " %u", node->mergeCollations[i]);
	addDebugNodeAttribute(deb_node, "mergeCollations", str->data);

	resetStringInfo(str);
	for (i = 0; i < numCols; i++)
		appendStringInfo(str, " %d", node->mergeStrategies[i]);
	addDebugNodeAttribute(deb_node, "mergeStrategies", str->data);

	resetStringInfo(str);
	for (i = 0; i < numCols; i++)
		appendStringInfo(str, " %d", (int) node->mergeNullsFirst[i]);
	addDebugNodeAttribute(deb_node, "mergeNullsFirst", str->data);

	pfree(str->data);
	pfree(str);
	return deb_node;
}

static DebugNode*
_outHashJoin(DebugGraph *graph, const HashJoin *node)
{
	WRITE_NODE_TYPE("HASHJOIN");

	_outJoinPlanInfo(graph, deb_node, (const Join *) node);

	WRITE_NODE_FIELD(hashclauses);

	return deb_node;
}

static DebugNode*
_outAgg(DebugGraph *graph, const Agg *node)
{
	StringInfo  str = makeStringInfo();
	int			i;

	WRITE_NODE_TYPE("AGG");

	_outPlanInfo(graph, deb_node, (const Plan *) node);

	WRITE_ENUM_FIELD(aggstrategy, AggStrategy);
	WRITE_INT_FIELD(numCols);

	for (i = 0; i < node->numCols; i++)
		appendStringInfo(str, " %d", node->grpColIdx[i]);
	addDebugNodeAttribute(deb_node, "grpColIdx", str->data);

	resetStringInfo(str);
	for (i = 0; i < node->numCols; i++)
		appendStringInfo(str, " %u", node->grpOperators[i]);
	addDebugNodeAttribute(deb_node, "grpOperators", str->data);

	WRITE_LONG_FIELD(numGroups);

	pfree(str->data);
	pfree(str);
	return deb_node;
}

static DebugNode*
_outWindowAgg(DebugGraph *graph, const WindowAgg *node)
{
	StringInfo  str = makeStringInfo();
	int			i;

	WRITE_NODE_TYPE("WINDOWAGG");

	_outPlanInfo(graph, deb_node, (const Plan *) node);

	WRITE_UINT_FIELD(winref);
	WRITE_INT_FIELD(partNumCols);

	for (i = 0; i < node->partNumCols; i++)
		appendStringInfo(str, " %d", node->partColIdx[i]);
	addDebugNodeAttribute(deb_node, "partColIdx", str->data);

	resetStringInfo(str);
	for (i = 0; i < node->partNumCols; i++)
		appendStringInfo(str, " %u", node->partOperators[i]);
	addDebugNodeAttribute(deb_node, "partOperators", str->data);

	WRITE_INT_FIELD(ordNumCols);

	resetStringInfo(str);
	for (i = 0; i < node->ordNumCols; i++)
		appendStringInfo(str, " %d", node->ordColIdx[i]);
	addDebugNodeAttribute(deb_node, "ordColIdx", str->data);

	resetStringInfo(str);
	for (i = 0; i < node->ordNumCols; i++)
		appendStringInfo(str, " %u", node->ordOperators[i]);
	addDebugNodeAttribute(deb_node, "ordOperators", str->data);

	WRITE_INT_FIELD(frameOptions);
	WRITE_NODE_FIELD(startOffset);
	WRITE_NODE_FIELD(endOffset);

	pfree(str->data);
	pfree(str);
	return deb_node;
}

static DebugNode*
_outGroup(DebugGraph *graph, const Group *node)
{
	StringInfo  str = makeStringInfo();
	int			i;

	WRITE_NODE_TYPE("GROUP");

	_outPlanInfo(graph, deb_node, (const Plan *) node);

	WRITE_INT_FIELD(numCols);

	resetStringInfo(str);
	for (i = 0; i < node->numCols; i++)
		appendStringInfo(str, " %d", node->grpColIdx[i]);
	addDebugNodeAttribute(deb_node, "grpColIdx", str->data);

	resetStringInfo(str);
	for (i = 0; i < node->numCols; i++)
		appendStringInfo(str, " %u", node->grpOperators[i]);
	addDebugNodeAttribute(deb_node, "grpOperators", str->data);

	pfree(str->data);
	pfree(str);
	return deb_node;
}

static DebugNode*
_outMaterial(DebugGraph *graph, const Material *node)
{
	WRITE_NODE_TYPE("MATERIAL");

	_outPlanInfo(graph, deb_node, (const Plan *) node);

	return deb_node;
}

static DebugNode*
_outSort(DebugGraph *graph, const Sort *node)
{
	StringInfo  str = makeStringInfo();
	int			i;

	WRITE_NODE_TYPE("SORT");

	_outPlanInfo(graph, deb_node, (const Plan *) node);

	WRITE_INT_FIELD(numCols);

	resetStringInfo(str);
	for (i = 0; i < node->numCols; i++)
		appendStringInfo(str, " %d", node->sortColIdx[i]);
	addDebugNodeAttribute(deb_node, "sortColIdx", str->data);

	resetStringInfo(str);
	for (i = 0; i < node->numCols; i++)
		appendStringInfo(str, " %u", node->sortOperators[i]);
	addDebugNodeAttribute(deb_node, "sortOperators", str->data);

	resetStringInfo(str);
	for (i = 0; i < node->numCols; i++)
		appendStringInfo(str, " %u", node->collations[i]);
	addDebugNodeAttribute(deb_node, "collations", str->data);

	resetStringInfo(str);
	for (i = 0; i < node->numCols; i++)
		appendStringInfo(str, " %s", booltostr(node->nullsFirst[i]));
	addDebugNodeAttribute(deb_node, "nullsFirst", str->data);

	pfree(str->data);
	pfree(str);
	return deb_node;
}

static DebugNode*
_outUnique(DebugGraph *graph, const Unique *node)
{
	StringInfo  str = makeStringInfo();
	int			i;

	WRITE_NODE_TYPE("UNIQUE");

	_outPlanInfo(graph, deb_node, (const Plan *) node);

	WRITE_INT_FIELD(numCols);

	resetStringInfo(str);
	for (i = 0; i < node->numCols; i++)
		appendStringInfo(str, " %d", node->uniqColIdx[i]);
	addDebugNodeAttribute(deb_node, "uniqColIdx", str->data);

	resetStringInfo(str);
	for (i = 0; i < node->numCols; i++)
		appendStringInfo(str, " %u", node->uniqOperators[i]);
	addDebugNodeAttribute(deb_node, "uniqOperators", str->data);

	pfree(str->data);
	pfree(str);
	return deb_node;
}

static DebugNode*
_outHash(DebugGraph *graph, const Hash *node)
{
	WRITE_NODE_TYPE("HASH");

	_outPlanInfo(graph, deb_node, (const Plan *) node);

	WRITE_OID_FIELD(skewTable);
	WRITE_INT_FIELD(skewColumn);
	WRITE_BOOL_FIELD(skewInherit);
	WRITE_OID_FIELD(skewColType);
	WRITE_INT_FIELD(skewColTypmod);

	return deb_node;
}

static DebugNode*
_outSetOp(DebugGraph *graph, const SetOp *node)
{
	StringInfo  str = makeStringInfo();
	int			i;

	WRITE_NODE_TYPE("SETOP");

	_outPlanInfo(graph, deb_node, (const Plan *) node);

	WRITE_ENUM_FIELD(cmd, SetOpCmd);
	WRITE_ENUM_FIELD(strategy, SetOpStrategy);
	WRITE_INT_FIELD(numCols);

	resetStringInfo(str);
	for (i = 0; i < node->numCols; i++)
		appendStringInfo(str, " %d", node->dupColIdx[i]);
	addDebugNodeAttribute(deb_node, "dupColIdx", str->data);

	resetStringInfo(str);
	for (i = 0; i < node->numCols; i++)
		appendStringInfo(str, " %u", node->dupOperators[i]);
	addDebugNodeAttribute(deb_node, "dupOperators", str->data);

	WRITE_INT_FIELD(flagColIdx);
	WRITE_INT_FIELD(firstFlag);
	WRITE_LONG_FIELD(numGroups);

	pfree(str->data);
	pfree(str);
	return deb_node;
}

static DebugNode*
_outLockRows(DebugGraph *graph, const LockRows *node)
{
	WRITE_NODE_TYPE("LOCKROWS");

	_outPlanInfo(graph, deb_node, (const Plan *) node);

	WRITE_NODE_FIELD(rowMarks);
	WRITE_INT_FIELD(epqParam);

	return deb_node;
}

static DebugNode*
_outLimit(DebugGraph *graph, const Limit *node)
{
	WRITE_NODE_TYPE("LIMIT");

	_outPlanInfo(graph, deb_node, (const Plan *) node);

	WRITE_NODE_FIELD(limitOffset);
	WRITE_NODE_FIELD(limitCount);

	return deb_node;
}

static DebugNode*
_outNestLoopParam(DebugGraph *graph, const NestLoopParam *node)
{
	WRITE_NODE_TYPE("NESTLOOPPARAM");

	WRITE_INT_FIELD(paramno);
	WRITE_NODE_FIELD(paramval);

	return deb_node;
}

static DebugNode*
_outPlanRowMark(DebugGraph *graph, const PlanRowMark *node)
{
	WRITE_NODE_TYPE("PLANROWMARK");

	WRITE_UINT_FIELD(rti);
	WRITE_UINT_FIELD(prti);
	WRITE_UINT_FIELD(rowmarkId);
	WRITE_ENUM_FIELD(markType, RowMarkType);
	WRITE_BOOL_FIELD(noWait);
	WRITE_BOOL_FIELD(isParent);

	return deb_node;
}

static DebugNode*
_outPlanInvalItem(DebugGraph *graph, const PlanInvalItem *node)
{
	WRITE_NODE_TYPE("PLANINVALITEM");

	WRITE_INT_FIELD(cacheId);
	WRITE_UINT_FIELD(hashValue);

	return deb_node;
}

/*****************************************************************************
 *
 *	Stuff from primnodes.h.
 *
 *****************************************************************************/

static DebugNode*
_outAlias(DebugGraph *graph, const Alias *node)
{
	WRITE_NODE_TYPE("ALIAS");

	WRITE_STRING_FIELD(aliasname);
	WRITE_NODE_FIELD(colnames);

	return deb_node;
}

static DebugNode*
_outRangeVar(DebugGraph *graph, const RangeVar *node)
{
	WRITE_NODE_TYPE("RANGEVAR");

	/*
	 * we deliberately ignore catalogname here, since it is presently not
	 * semantically meaningful
	 */
	WRITE_STRING_FIELD(schemaname);
	WRITE_STRING_FIELD(relname);
	WRITE_ENUM_FIELD(inhOpt, InhOption);
	WRITE_CHAR_FIELD(relpersistence);
	WRITE_NODE_FIELD(alias);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outIntoClause(DebugGraph *graph, const IntoClause *node)
{
	WRITE_NODE_TYPE("INTOCLAUSE");

	WRITE_NODE_FIELD(rel);
	WRITE_NODE_FIELD(colNames);
	WRITE_NODE_FIELD(options);
	WRITE_ENUM_FIELD(onCommit, OnCommitAction);
	WRITE_STRING_FIELD(tableSpaceName);
	WRITE_NODE_FIELD(viewQuery);
	WRITE_BOOL_FIELD(skipData);

	return deb_node;
}

static void
_outVar(StringInfo str, const Var *node)
{
	appendStringInfoString(str, nodeToString(node));
	/*
	WRITE_NODE_TYPE("VAR");

	WRITE_UINT_FIELD(varno);
	WRITE_INT_FIELD(varattno);
	WRITE_OID_FIELD(vartype);
	WRITE_INT_FIELD(vartypmod);
	WRITE_OID_FIELD(varcollid);
	WRITE_UINT_FIELD(varlevelsup);
	WRITE_UINT_FIELD(varnoold);
	WRITE_INT_FIELD(varoattno);
	WRITE_LOCATION_FIELD(location);
	*/
}

static DebugNode*
_outConst(DebugGraph *graph, const Const *node)
{
	StringInfo str = makeStringInfo();
	WRITE_NODE_TYPE("CONST");

	WRITE_OID_FIELD(consttype);
	WRITE_INT_FIELD(consttypmod);
	WRITE_OID_FIELD(constcollid);
	WRITE_INT_FIELD(constlen);
	WRITE_BOOL_FIELD(constbyval);
	WRITE_BOOL_FIELD(constisnull);
	WRITE_LOCATION_FIELD(location);

	if (node->constisnull)
		appendStringInfo(str, "<>");
	else
		_outDatum(str, node->constvalue, node->constlen, node->constbyval);
	addDebugNodeAttribute(deb_node, "constvalue", str->data);

	pfree(str->data);
	pfree(str);
	return deb_node;
}

static DebugNode*
_outParam(DebugGraph *graph, const Param *node)
{
	WRITE_NODE_TYPE("PARAM");

	WRITE_ENUM_FIELD(paramkind, ParamKind);
	WRITE_INT_FIELD(paramid);
	WRITE_OID_FIELD(paramtype);
	WRITE_INT_FIELD(paramtypmod);
	WRITE_OID_FIELD(paramcollid);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outAggref(DebugGraph *graph, const Aggref *node)
{
	WRITE_NODE_TYPE("AGGREF");

	WRITE_OID_FIELD(aggfnoid);
	WRITE_OID_FIELD(aggtype);
	WRITE_OID_FIELD(aggcollid);
	WRITE_OID_FIELD(inputcollid);
	WRITE_NODE_FIELD(args);
	WRITE_NODE_FIELD(aggorder);
	WRITE_NODE_FIELD(aggdistinct);
	WRITE_BOOL_FIELD(aggstar);
	WRITE_UINT_FIELD(agglevelsup);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outWindowFunc(DebugGraph *graph, const WindowFunc *node)
{
	WRITE_NODE_TYPE("WINDOWFUNC");

	WRITE_OID_FIELD(winfnoid);
	WRITE_OID_FIELD(wintype);
	WRITE_OID_FIELD(wincollid);
	WRITE_OID_FIELD(inputcollid);
	WRITE_NODE_FIELD(args);
	WRITE_UINT_FIELD(winref);
	WRITE_BOOL_FIELD(winstar);
	WRITE_BOOL_FIELD(winagg);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outArrayRef(DebugGraph *graph, const ArrayRef *node)
{
	WRITE_NODE_TYPE("ARRAYREF");

	WRITE_OID_FIELD(refarraytype);
	WRITE_OID_FIELD(refelemtype);
	WRITE_INT_FIELD(reftypmod);
	WRITE_OID_FIELD(refcollid);
	WRITE_NODE_FIELD(refupperindexpr);
	WRITE_NODE_FIELD(reflowerindexpr);
	WRITE_NODE_FIELD(refexpr);
	WRITE_NODE_FIELD(refassgnexpr);

	return deb_node;
}

static DebugNode*
_outFuncExpr(DebugGraph *graph, const FuncExpr *node)
{
	WRITE_NODE_TYPE("FUNCEXPR");

	WRITE_OID_FIELD(funcid);
	WRITE_OID_FIELD(funcresulttype);
	WRITE_BOOL_FIELD(funcretset);
	WRITE_BOOL_FIELD(funcvariadic);
	WRITE_ENUM_FIELD(funcformat, CoercionForm);
	WRITE_OID_FIELD(funccollid);
	WRITE_OID_FIELD(inputcollid);
	WRITE_NODE_FIELD(args);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outNamedArgExpr(DebugGraph *graph, const NamedArgExpr *node)
{
	WRITE_NODE_TYPE("NAMEDARGEXPR");

	WRITE_NODE_FIELD(arg);
	WRITE_STRING_FIELD(name);
	WRITE_INT_FIELD(argnumber);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outOpExpr(DebugGraph *graph, const OpExpr *node)
{
	WRITE_NODE_TYPE("OPEXPR");

	WRITE_OID_FIELD(opno);
	WRITE_OID_FIELD(opfuncid);
	WRITE_OID_FIELD(opresulttype);
	WRITE_BOOL_FIELD(opretset);
	WRITE_OID_FIELD(opcollid);
	WRITE_OID_FIELD(inputcollid);
	WRITE_NODE_FIELD(args);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outDistinctExpr(DebugGraph *graph, const DistinctExpr *node)
{
	WRITE_NODE_TYPE("DISTINCTEXPR");

	WRITE_OID_FIELD(opno);
	WRITE_OID_FIELD(opfuncid);
	WRITE_OID_FIELD(opresulttype);
	WRITE_BOOL_FIELD(opretset);
	WRITE_OID_FIELD(opcollid);
	WRITE_OID_FIELD(inputcollid);
	WRITE_NODE_FIELD(args);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outNullIfExpr(DebugGraph *graph, const NullIfExpr *node)
{
	WRITE_NODE_TYPE("NULLIFEXPR");

	WRITE_OID_FIELD(opno);
	WRITE_OID_FIELD(opfuncid);
	WRITE_OID_FIELD(opresulttype);
	WRITE_BOOL_FIELD(opretset);
	WRITE_OID_FIELD(opcollid);
	WRITE_OID_FIELD(inputcollid);
	WRITE_NODE_FIELD(args);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outScalarArrayOpExpr(DebugGraph *graph, const ScalarArrayOpExpr *node)
{
	WRITE_NODE_TYPE("SCALARARRAYOPEXPR");

	WRITE_OID_FIELD(opno);
	WRITE_OID_FIELD(opfuncid);
	WRITE_BOOL_FIELD(useOr);
	WRITE_OID_FIELD(inputcollid);
	WRITE_NODE_FIELD(args);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outBoolExpr(DebugGraph *graph, const BoolExpr *node)
{
	StringInfo  str = makeStringInfo();
	char	   *opstr = NULL;

	WRITE_NODE_TYPE("BOOLEXPR");

	/* do-it-yourself enum representation */
	switch (node->boolop)
	{
		case AND_EXPR:
			opstr = "and";
			break;
		case OR_EXPR:
			opstr = "or";
			break;
		case NOT_EXPR:
			opstr = "not";
			break;
	}
	_outToken(str, opstr);
	addDebugNodeAttribute(deb_node, "boolop", str->data);

	WRITE_NODE_FIELD(args);
	WRITE_LOCATION_FIELD(location);

	pfree(str->data);
	pfree(str);
	return deb_node;
}

static DebugNode*
_outSubLink(DebugGraph *graph, const SubLink *node)
{
	WRITE_NODE_TYPE("SUBLINK");

	WRITE_ENUM_FIELD(subLinkType, SubLinkType);
	WRITE_NODE_FIELD(testexpr);
	WRITE_NODE_FIELD(operName);
	WRITE_NODE_FIELD(subselect);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outSubPlan(DebugGraph *graph, const SubPlan *node)
{
	WRITE_NODE_TYPE("SUBPLAN");

	WRITE_ENUM_FIELD(subLinkType, SubLinkType);
	WRITE_NODE_FIELD(testexpr);
	WRITE_NODE_FIELD(paramIds);
	WRITE_INT_FIELD(plan_id);
	WRITE_STRING_FIELD(plan_name);
	WRITE_OID_FIELD(firstColType);
	WRITE_INT_FIELD(firstColTypmod);
	WRITE_OID_FIELD(firstColCollation);
	WRITE_BOOL_FIELD(useHashTable);
	WRITE_BOOL_FIELD(unknownEqFalse);
	WRITE_NODE_FIELD(setParam);
	WRITE_NODE_FIELD(parParam);
	WRITE_NODE_FIELD(args);
	WRITE_FLOAT_FIELD(startup_cost, "%.2f");
	WRITE_FLOAT_FIELD(per_call_cost, "%.2f");

	return deb_node;
}

static DebugNode*
_outAlternativeSubPlan(DebugGraph *graph, const AlternativeSubPlan *node)
{
	WRITE_NODE_TYPE("ALTERNATIVESUBPLAN");

	WRITE_NODE_FIELD(subplans);

	return deb_node;
}

static DebugNode*
_outFieldSelect(DebugGraph *graph, const FieldSelect *node)
{
	WRITE_NODE_TYPE("FIELDSELECT");

	WRITE_NODE_FIELD(arg);
	WRITE_INT_FIELD(fieldnum);
	WRITE_OID_FIELD(resulttype);
	WRITE_INT_FIELD(resulttypmod);
	WRITE_OID_FIELD(resultcollid);

	return deb_node;
}

static DebugNode*
_outFieldStore(DebugGraph *graph, const FieldStore *node)
{
	WRITE_NODE_TYPE("FIELDSTORE");

	WRITE_NODE_FIELD(arg);
	WRITE_NODE_FIELD(newvals);
	WRITE_NODE_FIELD(fieldnums);
	WRITE_OID_FIELD(resulttype);

	return deb_node;
}

static DebugNode*
_outRelabelType(DebugGraph *graph, const RelabelType *node)
{
	WRITE_NODE_TYPE("RELABELTYPE");

	WRITE_NODE_FIELD(arg);
	WRITE_OID_FIELD(resulttype);
	WRITE_INT_FIELD(resulttypmod);
	WRITE_OID_FIELD(resultcollid);
	WRITE_ENUM_FIELD(relabelformat, CoercionForm);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outCoerceViaIO(DebugGraph *graph, const CoerceViaIO *node)
{
	WRITE_NODE_TYPE("COERCEVIAIO");

	WRITE_NODE_FIELD(arg);
	WRITE_OID_FIELD(resulttype);
	WRITE_OID_FIELD(resultcollid);
	WRITE_ENUM_FIELD(coerceformat, CoercionForm);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outArrayCoerceExpr(DebugGraph *graph, const ArrayCoerceExpr *node)
{
	WRITE_NODE_TYPE("ARRAYCOERCEEXPR");

	WRITE_NODE_FIELD(arg);
	WRITE_OID_FIELD(elemfuncid);
	WRITE_OID_FIELD(resulttype);
	WRITE_INT_FIELD(resulttypmod);
	WRITE_OID_FIELD(resultcollid);
	WRITE_BOOL_FIELD(isExplicit);
	WRITE_ENUM_FIELD(coerceformat, CoercionForm);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outConvertRowtypeExpr(DebugGraph *graph, const ConvertRowtypeExpr *node)
{
	WRITE_NODE_TYPE("CONVERTROWTYPEEXPR");

	WRITE_NODE_FIELD(arg);
	WRITE_OID_FIELD(resulttype);
	WRITE_ENUM_FIELD(convertformat, CoercionForm);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outCollateExpr(DebugGraph *graph, const CollateExpr *node)
{
	WRITE_NODE_TYPE("COLLATE");

	WRITE_NODE_FIELD(arg);
	WRITE_OID_FIELD(collOid);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outCaseExpr(DebugGraph *graph, const CaseExpr *node)
{
	WRITE_NODE_TYPE("CASE");

	WRITE_OID_FIELD(casetype);
	WRITE_OID_FIELD(casecollid);
	WRITE_NODE_FIELD(arg);
	WRITE_NODE_FIELD(args);
	WRITE_NODE_FIELD(defresult);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outCaseWhen(DebugGraph *graph, const CaseWhen *node)
{
	WRITE_NODE_TYPE("WHEN");

	WRITE_NODE_FIELD(expr);
	WRITE_NODE_FIELD(result);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outCaseTestExpr(DebugGraph *graph, const CaseTestExpr *node)
{
	WRITE_NODE_TYPE("CASETESTEXPR");

	WRITE_OID_FIELD(typeId);
	WRITE_INT_FIELD(typeMod);
	WRITE_OID_FIELD(collation);

	return deb_node;
}

static DebugNode*
_outArrayExpr(DebugGraph *graph, const ArrayExpr *node)
{
	WRITE_NODE_TYPE("ARRAY");

	WRITE_OID_FIELD(array_typeid);
	WRITE_OID_FIELD(array_collid);
	WRITE_OID_FIELD(element_typeid);
	WRITE_NODE_FIELD(elements);
	WRITE_BOOL_FIELD(multidims);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outRowExpr(DebugGraph *graph, const RowExpr *node)
{
	WRITE_NODE_TYPE("ROW");

	WRITE_NODE_FIELD(args);
	WRITE_OID_FIELD(row_typeid);
	WRITE_ENUM_FIELD(row_format, CoercionForm);
	WRITE_NODE_FIELD(colnames);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outRowCompareExpr(DebugGraph *graph, const RowCompareExpr *node)
{
	WRITE_NODE_TYPE("ROWCOMPARE");

	WRITE_ENUM_FIELD(rctype, RowCompareType);
	WRITE_NODE_FIELD(opnos);
	WRITE_NODE_FIELD(opfamilies);
	WRITE_NODE_FIELD(inputcollids);
	WRITE_NODE_FIELD(largs);
	WRITE_NODE_FIELD(rargs);

	return deb_node;
}

static DebugNode*
_outCoalesceExpr(DebugGraph *graph, const CoalesceExpr *node)
{
	WRITE_NODE_TYPE("COALESCE");

	WRITE_OID_FIELD(coalescetype);
	WRITE_OID_FIELD(coalescecollid);
	WRITE_NODE_FIELD(args);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outMinMaxExpr(DebugGraph *graph, const MinMaxExpr *node)
{
	WRITE_NODE_TYPE("MINMAX");

	WRITE_OID_FIELD(minmaxtype);
	WRITE_OID_FIELD(minmaxcollid);
	WRITE_OID_FIELD(inputcollid);
	WRITE_ENUM_FIELD(op, MinMaxOp);
	WRITE_NODE_FIELD(args);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outXmlExpr(DebugGraph *graph, const XmlExpr *node)
{
	WRITE_NODE_TYPE("XMLEXPR");

	WRITE_ENUM_FIELD(op, XmlExprOp);
	WRITE_STRING_FIELD(name);
	WRITE_NODE_FIELD(named_args);
	WRITE_NODE_FIELD(arg_names);
	WRITE_NODE_FIELD(args);
	WRITE_ENUM_FIELD(xmloption, XmlOptionType);
	WRITE_OID_FIELD(type);
	WRITE_INT_FIELD(typmod);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outNullTest(DebugGraph *graph, const NullTest *node)
{
	WRITE_NODE_TYPE("NULLTEST");

	WRITE_NODE_FIELD(arg);
	WRITE_ENUM_FIELD(nulltesttype, NullTestType);
	WRITE_BOOL_FIELD(argisrow);

	return deb_node;
}

static DebugNode*
_outBooleanTest(DebugGraph *graph, const BooleanTest *node)
{
	WRITE_NODE_TYPE("BOOLEANTEST");

	WRITE_NODE_FIELD(arg);
	WRITE_ENUM_FIELD(booltesttype, BoolTestType);

	return deb_node;
}

static DebugNode*
_outCoerceToDomain(DebugGraph *graph, const CoerceToDomain *node)
{
	WRITE_NODE_TYPE("COERCETODOMAIN");

	WRITE_NODE_FIELD(arg);
	WRITE_OID_FIELD(resulttype);
	WRITE_INT_FIELD(resulttypmod);
	WRITE_OID_FIELD(resultcollid);
	WRITE_ENUM_FIELD(coercionformat, CoercionForm);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outCoerceToDomainValue(DebugGraph *graph, const CoerceToDomainValue *node)
{
	WRITE_NODE_TYPE("COERCETODOMAINVALUE");

	WRITE_OID_FIELD(typeId);
	WRITE_INT_FIELD(typeMod);
	WRITE_OID_FIELD(collation);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outSetToDefault(DebugGraph *graph, const SetToDefault *node)
{
	WRITE_NODE_TYPE("SETTODEFAULT");

	WRITE_OID_FIELD(typeId);
	WRITE_INT_FIELD(typeMod);
	WRITE_OID_FIELD(collation);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outCurrentOfExpr(DebugGraph *graph, const CurrentOfExpr *node)
{
	WRITE_NODE_TYPE("CURRENTOFEXPR");

	WRITE_UINT_FIELD(cvarno);
	WRITE_STRING_FIELD(cursor_name);
	WRITE_INT_FIELD(cursor_param);

	return deb_node;
}

static DebugNode*
_outTargetEntry(DebugGraph *graph, const TargetEntry *node)
{
	WRITE_NODE_TYPE("TARGETENTRY");

	WRITE_NODE_FIELD(expr);
	WRITE_INT_FIELD(resno);
	WRITE_STRING_FIELD(resname);
	WRITE_UINT_FIELD(ressortgroupref);
	WRITE_OID_FIELD(resorigtbl);
	WRITE_INT_FIELD(resorigcol);
	WRITE_BOOL_FIELD(resjunk);

	return deb_node;
}

static DebugNode*
_outRangeTblRef(DebugGraph *graph, const RangeTblRef *node)
{
	WRITE_NODE_TYPE("RANGETBLREF");

	WRITE_INT_FIELD(rtindex);

	return deb_node;
}

static DebugNode*
_outJoinExpr(DebugGraph *graph, const JoinExpr *node)
{
	WRITE_NODE_TYPE("JOINEXPR");

	WRITE_ENUM_FIELD(jointype, JoinType);
	WRITE_BOOL_FIELD(isNatural);
	WRITE_NODE_FIELD(larg);
	WRITE_NODE_FIELD(rarg);
	WRITE_NODE_FIELD(usingClause);
	WRITE_NODE_FIELD(quals);
	WRITE_NODE_FIELD(alias);
	WRITE_INT_FIELD(rtindex);

	return deb_node;
}

static DebugNode*
_outFromExpr(DebugGraph *graph, const FromExpr *node)
{
	WRITE_NODE_TYPE("FROMEXPR");

	WRITE_NODE_FIELD(fromlist);
	WRITE_NODE_FIELD(quals);

	return deb_node;
}

/*****************************************************************************
 *
 *	Stuff from relation.h.
 *
 *****************************************************************************/

/*
 * print the basic stuff of all nodes that inherit from Path
 *
 * Note we do NOT print the parent, else we'd be in infinite recursion.
 * We can print the parent's relids for identification purposes, though.
 * We also do not print the whole of param_info, since it's printed by
 * _outRelOptInfo; it's sufficient and less cluttering to print just the
 * required outer relids.
 */
static void
_outPathInfo(DebugGraph *graph, DebugNode *deb_node, const Path *node)
{
	StringInfo str = makeStringInfo();

	WRITE_ENUM_FIELD(pathtype, NodeTag);

	_outBitmapset(str, node->parent->relids);
	addDebugNodeAttribute(deb_node, "parent_relids", str->data);

	resetStringInfo(str);
	if (node->param_info)
		_outBitmapset(str, node->param_info->ppi_req_outer);
	else
		_outBitmapset(str, NULL);
	addDebugNodeAttribute(deb_node, "required_outer", str->data);

	WRITE_FLOAT_FIELD(rows, "%.0f");
	WRITE_FLOAT_FIELD(startup_cost, "%.2f");
	WRITE_FLOAT_FIELD(total_cost, "%.2f");
	WRITE_NODE_FIELD(pathkeys);

	pfree(str->data);
	pfree(str);
}

/*
 * print the basic stuff of all nodes that inherit from JoinPath
 */
static void
_outJoinPathInfo(DebugGraph *graph, DebugNode *deb_node, const JoinPath *node)
{
	_outPathInfo(graph, deb_node, (const Path *) node);

	WRITE_ENUM_FIELD(jointype, JoinType);
	WRITE_NODE_FIELD(outerjoinpath);
	WRITE_NODE_FIELD(innerjoinpath);
	WRITE_NODE_FIELD(joinrestrictinfo);
}

static DebugNode*
_outPath(DebugGraph *graph, const Path *node)
{
	WRITE_NODE_TYPE("PATH");

	_outPathInfo(graph, deb_node, (const Path *) node);

	return deb_node;
}

static DebugNode*
_outIndexPath(DebugGraph *graph, const IndexPath *node)
{
	WRITE_NODE_TYPE("INDEXPATH");

	_outPathInfo(graph, deb_node, (const Path *) node);

	WRITE_NODE_FIELD(indexinfo);
	WRITE_NODE_FIELD(indexclauses);
	WRITE_NODE_FIELD(indexquals);
	WRITE_NODE_FIELD(indexqualcols);
	WRITE_NODE_FIELD(indexorderbys);
	WRITE_NODE_FIELD(indexorderbycols);
	WRITE_ENUM_FIELD(indexscandir, ScanDirection);
	WRITE_FLOAT_FIELD(indextotalcost, "%.2f");
	WRITE_FLOAT_FIELD(indexselectivity, "%.4f");

	return deb_node;
}

static DebugNode*
_outBitmapHeapPath(DebugGraph *graph, const BitmapHeapPath *node)
{
	WRITE_NODE_TYPE("BITMAPHEAPPATH");

	_outPathInfo(graph, deb_node, (const Path *) node);

	WRITE_NODE_FIELD(bitmapqual);

	return deb_node;
}

static DebugNode*
_outBitmapAndPath(DebugGraph *graph, const BitmapAndPath *node)
{
	WRITE_NODE_TYPE("BITMAPANDPATH");

	_outPathInfo(graph, deb_node, (const Path *) node);

	WRITE_NODE_FIELD(bitmapquals);
	WRITE_FLOAT_FIELD(bitmapselectivity, "%.4f");

	return deb_node;
}

static DebugNode*
_outBitmapOrPath(DebugGraph *graph, const BitmapOrPath *node)
{
	WRITE_NODE_TYPE("BITMAPORPATH");

	_outPathInfo(graph, deb_node, (const Path *) node);

	WRITE_NODE_FIELD(bitmapquals);
	WRITE_FLOAT_FIELD(bitmapselectivity, "%.4f");

	return deb_node;
}

static DebugNode*
_outTidPath(DebugGraph *graph, const TidPath *node)
{
	WRITE_NODE_TYPE("TIDPATH");

	_outPathInfo(graph, deb_node, (const Path *) node);

	WRITE_NODE_FIELD(tidquals);

	return deb_node;
}

static DebugNode*
_outForeignPath(DebugGraph *graph, const ForeignPath *node)
{
	WRITE_NODE_TYPE("FOREIGNPATH");

	_outPathInfo(graph, deb_node, (const Path *) node);

	WRITE_NODE_FIELD(fdw_private);

	return deb_node;
}

static DebugNode*
_outAppendPath(DebugGraph *graph, const AppendPath *node)
{
	WRITE_NODE_TYPE("APPENDPATH");

	_outPathInfo(graph, deb_node, (const Path *) node);

	WRITE_NODE_FIELD(subpaths);

	return deb_node;
}

static DebugNode*
_outMergeAppendPath(DebugGraph *graph, const MergeAppendPath *node)
{
	WRITE_NODE_TYPE("MERGEAPPENDPATH");

	_outPathInfo(graph, deb_node, (const Path *) node);

	WRITE_NODE_FIELD(subpaths);
	WRITE_FLOAT_FIELD(limit_tuples, "%.0f");

	return deb_node;
}

static DebugNode*
_outResultPath(DebugGraph *graph, const ResultPath *node)
{
	WRITE_NODE_TYPE("RESULTPATH");

	_outPathInfo(graph, deb_node, (const Path *) node);

	WRITE_NODE_FIELD(quals);

	return deb_node;
}

static DebugNode*
_outMaterialPath(DebugGraph *graph, const MaterialPath *node)
{
	WRITE_NODE_TYPE("MATERIALPATH");

	_outPathInfo(graph, deb_node, (const Path *) node);

	WRITE_NODE_FIELD(subpath);

	return deb_node;
}

static DebugNode*
_outUniquePath(DebugGraph *graph, const UniquePath *node)
{
	WRITE_NODE_TYPE("UNIQUEPATH");

	_outPathInfo(graph, deb_node, (const Path *) node);

	WRITE_NODE_FIELD(subpath);
	WRITE_ENUM_FIELD(umethod, UniquePathMethod);
	WRITE_NODE_FIELD(in_operators);
	WRITE_NODE_FIELD(uniq_exprs);

	return deb_node;
}

static DebugNode*
_outNestPath(DebugGraph *graph, const NestPath *node)
{
	WRITE_NODE_TYPE("NESTPATH");

	_outJoinPathInfo(graph, deb_node, (const JoinPath *) node);

	return deb_node;
}

static DebugNode*
_outMergePath(DebugGraph *graph, const MergePath *node)
{
	WRITE_NODE_TYPE("MERGEPATH");

	_outJoinPathInfo(graph, deb_node, (const JoinPath *) node);

	WRITE_NODE_FIELD(path_mergeclauses);
	WRITE_NODE_FIELD(outersortkeys);
	WRITE_NODE_FIELD(innersortkeys);
	WRITE_BOOL_FIELD(materialize_inner);

	return deb_node;
}

static DebugNode*
_outHashPath(DebugGraph *graph, const HashPath *node)
{
	WRITE_NODE_TYPE("HASHPATH");

	_outJoinPathInfo(graph, deb_node, (const JoinPath *) node);

	WRITE_NODE_FIELD(path_hashclauses);
	WRITE_INT_FIELD(num_batches);

	return deb_node;
}

static DebugNode*
_outPlannerGlobal(DebugGraph *graph, const PlannerGlobal *node)
{
	WRITE_NODE_TYPE("PLANNERGLOBAL");

	/* NB: this isn't a complete set of fields */
	WRITE_NODE_FIELD(subplans);
	WRITE_BITMAPSET_FIELD(rewindPlanIDs);
	WRITE_NODE_FIELD(finalrtable);
	WRITE_NODE_FIELD(finalrowmarks);
	WRITE_NODE_FIELD(resultRelations);
	WRITE_NODE_FIELD(relationOids);
	WRITE_NODE_FIELD(invalItems);
	WRITE_INT_FIELD(nParamExec);
	WRITE_UINT_FIELD(lastPHId);
	WRITE_UINT_FIELD(lastRowMarkId);
	WRITE_BOOL_FIELD(transientPlan);

	return deb_node;
}

static DebugNode*
_outPlannerInfo(DebugGraph *graph, const PlannerInfo *node)
{
	WRITE_NODE_TYPE("PLANNERINFO");

	/* NB: this isn't a complete set of fields */
	WRITE_NODE_FIELD(parse);
	WRITE_NODE_FIELD(glob);
	WRITE_UINT_FIELD(query_level);
	WRITE_NODE_FIELD(plan_params);
	WRITE_BITMAPSET_FIELD(all_baserels);
	WRITE_BITMAPSET_FIELD(nullable_baserels);
	WRITE_NODE_FIELD(join_rel_list);
	WRITE_INT_FIELD(join_cur_level);
	WRITE_NODE_FIELD(init_plans);
	WRITE_NODE_FIELD(cte_plan_ids);
	WRITE_NODE_FIELD(eq_classes);
	WRITE_NODE_FIELD(canon_pathkeys);
	WRITE_NODE_FIELD(left_join_clauses);
	WRITE_NODE_FIELD(right_join_clauses);
	WRITE_NODE_FIELD(full_join_clauses);
	WRITE_NODE_FIELD(join_info_list);
	WRITE_NODE_FIELD(lateral_info_list);
	WRITE_NODE_FIELD(append_rel_list);
	WRITE_NODE_FIELD(rowMarks);
	WRITE_NODE_FIELD(placeholder_list);
	WRITE_NODE_FIELD(query_pathkeys);
	WRITE_NODE_FIELD(group_pathkeys);
	WRITE_NODE_FIELD(window_pathkeys);
	WRITE_NODE_FIELD(distinct_pathkeys);
	WRITE_NODE_FIELD(sort_pathkeys);
	WRITE_NODE_FIELD(minmax_aggs);
	WRITE_FLOAT_FIELD(total_table_pages, "%.0f");
	WRITE_FLOAT_FIELD(tuple_fraction, "%.4f");
	WRITE_FLOAT_FIELD(limit_tuples, "%.0f");
	WRITE_BOOL_FIELD(hasInheritedTarget);
	WRITE_BOOL_FIELD(hasJoinRTEs);
	WRITE_BOOL_FIELD(hasLateralRTEs);
	WRITE_BOOL_FIELD(hasHavingQual);
	WRITE_BOOL_FIELD(hasPseudoConstantQuals);
	WRITE_BOOL_FIELD(hasRecursion);
	WRITE_INT_FIELD(wt_param_id);
	WRITE_BITMAPSET_FIELD(curOuterRels);
	WRITE_NODE_FIELD(curOuterParams);

	return deb_node;
}

static DebugNode*
_outRelOptInfo(DebugGraph *graph, const RelOptInfo *node)
{
	WRITE_NODE_TYPE("RELOPTINFO");

	/* NB: this isn't a complete set of fields */
	WRITE_ENUM_FIELD(reloptkind, RelOptKind);
	WRITE_BITMAPSET_FIELD(relids);
	WRITE_FLOAT_FIELD(rows, "%.0f");
	WRITE_INT_FIELD(width);
	WRITE_BOOL_FIELD(consider_startup);
	WRITE_NODE_FIELD(reltargetlist);
	WRITE_NODE_FIELD(pathlist);
	WRITE_NODE_FIELD(ppilist);
	WRITE_NODE_FIELD(cheapest_startup_path);
	WRITE_NODE_FIELD(cheapest_total_path);
	WRITE_NODE_FIELD(cheapest_unique_path);
	WRITE_NODE_FIELD(cheapest_parameterized_paths);
	WRITE_UINT_FIELD(relid);
	WRITE_UINT_FIELD(reltablespace);
	WRITE_ENUM_FIELD(rtekind, RTEKind);
	WRITE_INT_FIELD(min_attr);
	WRITE_INT_FIELD(max_attr);
	WRITE_NODE_FIELD(lateral_vars);
	WRITE_BITMAPSET_FIELD(lateral_relids);
	WRITE_BITMAPSET_FIELD(lateral_referencers);
	WRITE_NODE_FIELD(indexlist);
	WRITE_UINT_FIELD(pages);
	WRITE_FLOAT_FIELD(tuples, "%.0f");
	WRITE_FLOAT_FIELD(allvisfrac, "%.6f");
	WRITE_NODE_FIELD(subplan);
	WRITE_NODE_FIELD(subroot);
	WRITE_NODE_FIELD(subplan_params);
	/* we don't try to print fdwroutine or fdw_private */
	WRITE_NODE_FIELD(baserestrictinfo);
	WRITE_NODE_FIELD(joininfo);
	WRITE_BOOL_FIELD(has_eclass_joins);

	return deb_node;
}

static DebugNode*
_outIndexOptInfo(DebugGraph *graph, const IndexOptInfo *node)
{
	WRITE_NODE_TYPE("INDEXOPTINFO");

	/* NB: this isn't a complete set of fields */
	WRITE_OID_FIELD(indexoid);
	/* Do NOT print rel field, else infinite recursion */
	WRITE_UINT_FIELD(pages);
	WRITE_FLOAT_FIELD(tuples, "%.0f");
	WRITE_INT_FIELD(tree_height);
	WRITE_INT_FIELD(ncolumns);
	/* array fields aren't really worth the trouble to print */
	WRITE_OID_FIELD(relam);
	/* indexprs is redundant since we print indextlist */
	WRITE_NODE_FIELD(indpred);
	WRITE_NODE_FIELD(indextlist);
	WRITE_BOOL_FIELD(predOK);
	WRITE_BOOL_FIELD(unique);
	WRITE_BOOL_FIELD(immediate);
	WRITE_BOOL_FIELD(hypothetical);
	/* we don't bother with fields copied from the pg_am entry */

	return deb_node;
}

static DebugNode*
_outEquivalenceClass(DebugGraph *graph, const EquivalenceClass *node)
{
	/*
	 * To simplify reading, we just chase up to the topmost merged EC and
	 * print that, without bothering to show the merge-ees separately.
	 */
	while (node->ec_merged)
		node = node->ec_merged;

	WRITE_NODE_TYPE("EQUIVALENCECLASS");

	WRITE_NODE_FIELD(ec_opfamilies);
	WRITE_OID_FIELD(ec_collation);
	WRITE_NODE_FIELD(ec_members);
	WRITE_NODE_FIELD(ec_sources);
	WRITE_NODE_FIELD(ec_derives);
	WRITE_BITMAPSET_FIELD(ec_relids);
	WRITE_BOOL_FIELD(ec_has_const);
	WRITE_BOOL_FIELD(ec_has_volatile);
	WRITE_BOOL_FIELD(ec_below_outer_join);
	WRITE_BOOL_FIELD(ec_broken);
	WRITE_UINT_FIELD(ec_sortref);

	return deb_node;
}

static DebugNode*
_outEquivalenceMember(DebugGraph *graph, const EquivalenceMember *node)
{
	WRITE_NODE_TYPE("EQUIVALENCEMEMBER");

	WRITE_NODE_FIELD(em_expr);
	WRITE_BITMAPSET_FIELD(em_relids);
	WRITE_BITMAPSET_FIELD(em_nullable_relids);
	WRITE_BOOL_FIELD(em_is_const);
	WRITE_BOOL_FIELD(em_is_child);
	WRITE_OID_FIELD(em_datatype);

	return deb_node;
}

static DebugNode*
_outPathKey(DebugGraph *graph, const PathKey *node)
{
	WRITE_NODE_TYPE("PATHKEY");

	WRITE_NODE_FIELD(pk_eclass);
	WRITE_OID_FIELD(pk_opfamily);
	WRITE_INT_FIELD(pk_strategy);
	WRITE_BOOL_FIELD(pk_nulls_first);

	return deb_node;
}

static DebugNode*
_outParamPathInfo(DebugGraph *graph, const ParamPathInfo *node)
{
	WRITE_NODE_TYPE("PARAMPATHINFO");

	WRITE_BITMAPSET_FIELD(ppi_req_outer);
	WRITE_FLOAT_FIELD(ppi_rows, "%.0f");
	WRITE_NODE_FIELD(ppi_clauses);

	return deb_node;
}

static DebugNode*
_outRestrictInfo(DebugGraph *graph, const RestrictInfo *node)
{
	WRITE_NODE_TYPE("RESTRICTINFO");

	/* NB: this isn't a complete set of fields */
	WRITE_NODE_FIELD(clause);
	WRITE_BOOL_FIELD(is_pushed_down);
	WRITE_BOOL_FIELD(outerjoin_delayed);
	WRITE_BOOL_FIELD(can_join);
	WRITE_BOOL_FIELD(pseudoconstant);
	WRITE_BITMAPSET_FIELD(clause_relids);
	WRITE_BITMAPSET_FIELD(required_relids);
	WRITE_BITMAPSET_FIELD(outer_relids);
	WRITE_BITMAPSET_FIELD(nullable_relids);
	WRITE_BITMAPSET_FIELD(left_relids);
	WRITE_BITMAPSET_FIELD(right_relids);
	WRITE_NODE_FIELD(orclause);
	/* don't write parent_ec, leads to infinite recursion in plan tree dump */
	WRITE_FLOAT_FIELD(norm_selec, "%.4f");
	WRITE_FLOAT_FIELD(outer_selec, "%.4f");
	WRITE_NODE_FIELD(mergeopfamilies);
	/* don't write left_ec, leads to infinite recursion in plan tree dump */
	/* don't write right_ec, leads to infinite recursion in plan tree dump */
	WRITE_NODE_FIELD(left_em);
	WRITE_NODE_FIELD(right_em);
	WRITE_BOOL_FIELD(outer_is_left);
	WRITE_OID_FIELD(hashjoinoperator);

	return deb_node;
}

static DebugNode*
_outPlaceHolderVar(DebugGraph *graph, const PlaceHolderVar *node)
{
	WRITE_NODE_TYPE("PLACEHOLDERVAR");

	WRITE_NODE_FIELD(phexpr);
	WRITE_BITMAPSET_FIELD(phrels);
	WRITE_UINT_FIELD(phid);
	WRITE_UINT_FIELD(phlevelsup);

	return deb_node;
}

static DebugNode*
_outSpecialJoinInfo(DebugGraph *graph, const SpecialJoinInfo *node)
{
	WRITE_NODE_TYPE("SPECIALJOININFO");

	WRITE_BITMAPSET_FIELD(min_lefthand);
	WRITE_BITMAPSET_FIELD(min_righthand);
	WRITE_BITMAPSET_FIELD(syn_lefthand);
	WRITE_BITMAPSET_FIELD(syn_righthand);
	WRITE_ENUM_FIELD(jointype, JoinType);
	WRITE_BOOL_FIELD(lhs_strict);
	WRITE_BOOL_FIELD(delay_upper_joins);
	WRITE_NODE_FIELD(join_quals);

	return deb_node;
}

static DebugNode*
_outLateralJoinInfo(DebugGraph *graph, const LateralJoinInfo *node)
{
	WRITE_NODE_TYPE("LATERALJOININFO");

	WRITE_BITMAPSET_FIELD(lateral_lhs);
	WRITE_BITMAPSET_FIELD(lateral_rhs);

	return deb_node;
}

static DebugNode*
_outAppendRelInfo(DebugGraph *graph, const AppendRelInfo *node)
{
	WRITE_NODE_TYPE("APPENDRELINFO");

	WRITE_UINT_FIELD(parent_relid);
	WRITE_UINT_FIELD(child_relid);
	WRITE_OID_FIELD(parent_reltype);
	WRITE_OID_FIELD(child_reltype);
	WRITE_NODE_FIELD(translated_vars);
	WRITE_OID_FIELD(parent_reloid);

	return deb_node;
}

static DebugNode*
_outPlaceHolderInfo(DebugGraph *graph, const PlaceHolderInfo *node)
{
	WRITE_NODE_TYPE("PLACEHOLDERINFO");

	WRITE_UINT_FIELD(phid);
	WRITE_NODE_FIELD(ph_var);
	WRITE_BITMAPSET_FIELD(ph_eval_at);
	WRITE_BITMAPSET_FIELD(ph_lateral);
	WRITE_BITMAPSET_FIELD(ph_needed);
	WRITE_INT_FIELD(ph_width);

	return deb_node;
}

static DebugNode*
_outMinMaxAggInfo(DebugGraph *graph, const MinMaxAggInfo *node)
{
	WRITE_NODE_TYPE("MINMAXAGGINFO");

	WRITE_OID_FIELD(aggfnoid);
	WRITE_OID_FIELD(aggsortop);
	WRITE_NODE_FIELD(target);
	/* We intentionally omit subroot --- too large, not interesting enough */
	WRITE_NODE_FIELD(path);
	WRITE_FLOAT_FIELD(pathcost, "%.2f");
	WRITE_NODE_FIELD(param);

	return deb_node;
}

static DebugNode*
_outPlannerParamItem(DebugGraph *graph, const PlannerParamItem *node)
{
	WRITE_NODE_TYPE("PLANNERPARAMITEM");

	WRITE_NODE_FIELD(item);
	WRITE_INT_FIELD(paramId);

	return deb_node;
}

/*****************************************************************************
 *
 *	Stuff from parsenodes.h.
 *
 *****************************************************************************/

/*
 * print the basic stuff of all nodes that inherit from CreateStmt
 */
static void
_outCreateStmtInfo(DebugGraph *graph, DebugNode *deb_node, const CreateStmt *node)
{
	WRITE_NODE_FIELD(relation);
	WRITE_NODE_FIELD(tableElts);
	WRITE_NODE_FIELD(inhRelations);
	WRITE_NODE_FIELD(ofTypename);
	WRITE_NODE_FIELD(constraints);
	WRITE_NODE_FIELD(options);
	WRITE_ENUM_FIELD(oncommit, OnCommitAction);
	WRITE_STRING_FIELD(tablespacename);
	WRITE_BOOL_FIELD(if_not_exists);
}

static DebugNode*
_outCreateStmt(DebugGraph *graph, const CreateStmt *node)
{
	WRITE_NODE_TYPE("CREATESTMT");

	_outCreateStmtInfo(graph, deb_node, (const CreateStmt *) node);

	return deb_node;
}

static DebugNode*
_outCreateForeignTableStmt(DebugGraph *graph, const CreateForeignTableStmt *node)
{
	WRITE_NODE_TYPE("CREATEFOREIGNTABLESTMT");

	_outCreateStmtInfo(graph, deb_node, (const CreateStmt *) node);

	WRITE_STRING_FIELD(servername);
	WRITE_NODE_FIELD(options);

	return deb_node;
}

static DebugNode*
_outIndexStmt(DebugGraph *graph, const IndexStmt *node)
{
	WRITE_NODE_TYPE("INDEXSTMT");

	WRITE_STRING_FIELD(idxname);
	WRITE_NODE_FIELD(relation);
	WRITE_STRING_FIELD(accessMethod);
	WRITE_STRING_FIELD(tableSpace);
	WRITE_NODE_FIELD(indexParams);
	WRITE_NODE_FIELD(options);
	WRITE_NODE_FIELD(whereClause);
	WRITE_NODE_FIELD(excludeOpNames);
	WRITE_STRING_FIELD(idxcomment);
	WRITE_OID_FIELD(indexOid);
	WRITE_OID_FIELD(oldNode);
	WRITE_BOOL_FIELD(unique);
	WRITE_BOOL_FIELD(primary);
	WRITE_BOOL_FIELD(isconstraint);
	WRITE_BOOL_FIELD(deferrable);
	WRITE_BOOL_FIELD(initdeferred);
	WRITE_BOOL_FIELD(concurrent);

	return deb_node;
}

static DebugNode*
_outNotifyStmt(DebugGraph *graph, const NotifyStmt *node)
{
	WRITE_NODE_TYPE("NOTIFY");

	WRITE_STRING_FIELD(conditionname);
	WRITE_STRING_FIELD(payload);

	return deb_node;
}

static DebugNode*
_outDeclareCursorStmt(DebugGraph *graph, const DeclareCursorStmt *node)
{
	WRITE_NODE_TYPE("DECLARECURSOR");

	WRITE_STRING_FIELD(portalname);
	WRITE_INT_FIELD(options);
	WRITE_NODE_FIELD(query);

	return deb_node;
}

static DebugNode*
_outSelectStmt(DebugGraph *graph, const SelectStmt *node)
{
	WRITE_NODE_TYPE("SELECT");

	WRITE_NODE_FIELD(distinctClause);
	WRITE_NODE_FIELD(intoClause);
	WRITE_NODE_FIELD(targetList);
	WRITE_NODE_FIELD(fromClause);
	WRITE_NODE_FIELD(whereClause);
	WRITE_NODE_FIELD(groupClause);
	WRITE_NODE_FIELD(havingClause);
	WRITE_NODE_FIELD(windowClause);
	WRITE_NODE_FIELD(valuesLists);
	WRITE_NODE_FIELD(sortClause);
	WRITE_NODE_FIELD(limitOffset);
	WRITE_NODE_FIELD(limitCount);
	WRITE_NODE_FIELD(lockingClause);
	WRITE_NODE_FIELD(withClause);
	WRITE_ENUM_FIELD(op, SetOperation);
	WRITE_BOOL_FIELD(all);
	WRITE_NODE_FIELD(larg);
	WRITE_NODE_FIELD(rarg);

	return deb_node;
}

static DebugNode*
_outFuncCall(DebugGraph *graph, const FuncCall *node)
{
	WRITE_NODE_TYPE("FUNCCALL");

	WRITE_NODE_FIELD(funcname);
	WRITE_NODE_FIELD(args);
	WRITE_NODE_FIELD(agg_order);
	WRITE_BOOL_FIELD(agg_star);
	WRITE_BOOL_FIELD(agg_distinct);
	WRITE_BOOL_FIELD(func_variadic);
	WRITE_NODE_FIELD(over);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outDefElem(DebugGraph *graph, const DefElem *node)
{
	WRITE_NODE_TYPE("DEFELEM");

	WRITE_STRING_FIELD(defnamespace);
	WRITE_STRING_FIELD(defname);
	WRITE_NODE_FIELD(arg);
	WRITE_ENUM_FIELD(defaction, DefElemAction);

	return deb_node;
}

static DebugNode*
_outTableLikeClause(DebugGraph *graph, const TableLikeClause *node)
{
	WRITE_NODE_TYPE("TABLELIKECLAUSE");

	WRITE_NODE_FIELD(relation);
	WRITE_UINT_FIELD(options);

	return deb_node;
}

static DebugNode*
_outLockingClause(DebugGraph *graph, const LockingClause *node)
{
	WRITE_NODE_TYPE("LOCKINGCLAUSE");

	WRITE_NODE_FIELD(lockedRels);
	WRITE_ENUM_FIELD(strength, LockClauseStrength);
	WRITE_BOOL_FIELD(noWait);

	return deb_node;
}

static DebugNode*
_outXmlSerialize(DebugGraph *graph, const XmlSerialize *node)
{
	WRITE_NODE_TYPE("XMLSERIALIZE");

	WRITE_ENUM_FIELD(xmloption, XmlOptionType);
	WRITE_NODE_FIELD(expr);
	WRITE_NODE_FIELD(typeName);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outColumnDef(DebugGraph *graph, const ColumnDef *node)
{
	WRITE_NODE_TYPE("COLUMNDEF");

	WRITE_STRING_FIELD(colname);
	WRITE_NODE_FIELD(typeName);
	WRITE_INT_FIELD(inhcount);
	WRITE_BOOL_FIELD(is_local);
	WRITE_BOOL_FIELD(is_not_null);
	WRITE_BOOL_FIELD(is_from_type);
	WRITE_CHAR_FIELD(storage);
	WRITE_NODE_FIELD(raw_default);
	WRITE_NODE_FIELD(cooked_default);
	WRITE_NODE_FIELD(collClause);
	WRITE_OID_FIELD(collOid);
	WRITE_NODE_FIELD(constraints);
	WRITE_NODE_FIELD(fdwoptions);

	return deb_node;
}

static DebugNode*
_outTypeName(DebugGraph *graph, const TypeName *node)
{
	WRITE_NODE_TYPE("TYPENAME");

	WRITE_NODE_FIELD(names);
	WRITE_OID_FIELD(typeOid);
	WRITE_BOOL_FIELD(setof);
	WRITE_BOOL_FIELD(pct_type);
	WRITE_NODE_FIELD(typmods);
	WRITE_INT_FIELD(typemod);
	WRITE_NODE_FIELD(arrayBounds);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outTypeCast(DebugGraph *graph, const TypeCast *node)
{
	WRITE_NODE_TYPE("TYPECAST");

	WRITE_NODE_FIELD(arg);
	WRITE_NODE_FIELD(typeName);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outCollateClause(DebugGraph *graph, const CollateClause *node)
{
	WRITE_NODE_TYPE("COLLATECLAUSE");

	WRITE_NODE_FIELD(arg);
	WRITE_NODE_FIELD(collname);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outIndexElem(DebugGraph *graph, const IndexElem *node)
{
	WRITE_NODE_TYPE("INDEXELEM");

	WRITE_STRING_FIELD(name);
	WRITE_NODE_FIELD(expr);
	WRITE_STRING_FIELD(indexcolname);
	WRITE_NODE_FIELD(collation);
	WRITE_NODE_FIELD(opclass);
	WRITE_ENUM_FIELD(ordering, SortByDir);
	WRITE_ENUM_FIELD(nulls_ordering, SortByNulls);

	return deb_node;
}

static DebugNode*
_outQuery(DebugGraph *graph, const Query *node)
{
	WRITE_NODE_TYPE("QUERY");

	WRITE_ENUM_FIELD(commandType, CmdType);
	WRITE_ENUM_FIELD(querySource, QuerySource);
	/* we intentionally do not print the queryId field */
	WRITE_BOOL_FIELD(canSetTag);

	/*
	 * Hack to work around missing outfuncs routines for a lot of the
	 * utility-statement node types.  (The only one we actually *need* for
	 * rules support is NotifyStmt.)  Someday we ought to support 'em all, but
	 * for the meantime do this to avoid getting lots of warnings when running
	 * with debug_print_parse on.
	 */
	if (node->utilityStmt)
	{
		switch (nodeTag(node->utilityStmt))
		{
			case T_CreateStmt:
			case T_IndexStmt:
			case T_NotifyStmt:
			case T_DeclareCursorStmt:
				WRITE_NODE_FIELD(utilityStmt);
				break;
			default:
				WRITE_NODE_FIELD(utilityStmt);
				break;
		}
	}
	else
		WRITE_NODE_FIELD(utilityStmt);

	WRITE_INT_FIELD(resultRelation);
	WRITE_BOOL_FIELD(hasAggs);
	WRITE_BOOL_FIELD(hasWindowFuncs);
	WRITE_BOOL_FIELD(hasSubLinks);
	WRITE_BOOL_FIELD(hasDistinctOn);
	WRITE_BOOL_FIELD(hasRecursive);
	WRITE_BOOL_FIELD(hasModifyingCTE);
	WRITE_BOOL_FIELD(hasForUpdate);
	WRITE_NODE_FIELD(cteList);
	WRITE_NODE_FIELD(rtable);
	WRITE_NODE_FIELD(jointree);
	WRITE_NODE_FIELD(targetList);
	WRITE_NODE_FIELD(returningList);
	WRITE_NODE_FIELD(groupClause);
	WRITE_NODE_FIELD(havingQual);
	WRITE_NODE_FIELD(windowClause);
	WRITE_NODE_FIELD(distinctClause);
	WRITE_NODE_FIELD(sortClause);
	WRITE_NODE_FIELD(limitOffset);
	WRITE_NODE_FIELD(limitCount);
	WRITE_NODE_FIELD(rowMarks);
	WRITE_NODE_FIELD(setOperations);
	WRITE_NODE_FIELD(constraintDeps);

	return deb_node;
}

static DebugNode*
_outSortGroupClause(DebugGraph *graph, const SortGroupClause *node)
{
	WRITE_NODE_TYPE("SORTGROUPCLAUSE");

	WRITE_UINT_FIELD(tleSortGroupRef);
	WRITE_OID_FIELD(eqop);
	WRITE_OID_FIELD(sortop);
	WRITE_BOOL_FIELD(nulls_first);
	WRITE_BOOL_FIELD(hashable);

	return deb_node;
}

static DebugNode*
_outWindowClause(DebugGraph *graph, const WindowClause *node)
{
	WRITE_NODE_TYPE("WINDOWCLAUSE");

	WRITE_STRING_FIELD(name);
	WRITE_STRING_FIELD(refname);
	WRITE_NODE_FIELD(partitionClause);
	WRITE_NODE_FIELD(orderClause);
	WRITE_INT_FIELD(frameOptions);
	WRITE_NODE_FIELD(startOffset);
	WRITE_NODE_FIELD(endOffset);
	WRITE_UINT_FIELD(winref);
	WRITE_BOOL_FIELD(copiedOrder);

	return deb_node;
}

static DebugNode*
_outRowMarkClause(DebugGraph *graph, const RowMarkClause *node)
{
	WRITE_NODE_TYPE("ROWMARKCLAUSE");

	WRITE_UINT_FIELD(rti);
	WRITE_ENUM_FIELD(strength, LockClauseStrength);
	WRITE_BOOL_FIELD(noWait);
	WRITE_BOOL_FIELD(pushedDown);

	return deb_node;
}

static DebugNode*
_outWithClause(DebugGraph *graph, const WithClause *node)
{
	WRITE_NODE_TYPE("WITHCLAUSE");

	WRITE_NODE_FIELD(ctes);
	WRITE_BOOL_FIELD(recursive);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outCommonTableExpr(DebugGraph *graph, const CommonTableExpr *node)
{
	WRITE_NODE_TYPE("COMMONTABLEEXPR");

	WRITE_STRING_FIELD(ctename);
	WRITE_NODE_FIELD(aliascolnames);
	WRITE_NODE_FIELD(ctequery);
	WRITE_LOCATION_FIELD(location);
	WRITE_BOOL_FIELD(cterecursive);
	WRITE_INT_FIELD(cterefcount);
	WRITE_NODE_FIELD(ctecolnames);
	WRITE_NODE_FIELD(ctecoltypes);
	WRITE_NODE_FIELD(ctecoltypmods);
	WRITE_NODE_FIELD(ctecolcollations);

	return deb_node;
}

static DebugNode*
_outSetOperationStmt(DebugGraph *graph, const SetOperationStmt *node)
{
	WRITE_NODE_TYPE("SETOPERATIONSTMT");

	WRITE_ENUM_FIELD(op, SetOperation);
	WRITE_BOOL_FIELD(all);
	WRITE_NODE_FIELD(larg);
	WRITE_NODE_FIELD(rarg);
	WRITE_NODE_FIELD(colTypes);
	WRITE_NODE_FIELD(colTypmods);
	WRITE_NODE_FIELD(colCollations);
	WRITE_NODE_FIELD(groupClauses);

	return deb_node;
}

static DebugNode*
_outRangeTblEntry(DebugGraph *graph, const RangeTblEntry *node)
{
	WRITE_NODE_TYPE("RTE");

	/* put alias + eref first to make dump more legible */
	WRITE_NODE_FIELD(alias);
	WRITE_NODE_FIELD(eref);
	WRITE_ENUM_FIELD(rtekind, RTEKind);

	switch (node->rtekind)
	{
		case RTE_RELATION:
			WRITE_OID_FIELD(relid);
			WRITE_CHAR_FIELD(relkind);
			break;
		case RTE_SUBQUERY:
			WRITE_NODE_FIELD(subquery);
			WRITE_BOOL_FIELD(security_barrier);
			break;
		case RTE_JOIN:
			WRITE_ENUM_FIELD(jointype, JoinType);
			WRITE_NODE_FIELD(joinaliasvars);
			break;
		case RTE_FUNCTION:
			WRITE_NODE_FIELD(funcexpr);
			WRITE_NODE_FIELD(funccoltypes);
			WRITE_NODE_FIELD(funccoltypmods);
			WRITE_NODE_FIELD(funccolcollations);
			break;
		case RTE_VALUES:
			WRITE_NODE_FIELD(values_lists);
			WRITE_NODE_FIELD(values_collations);
			break;
		case RTE_CTE:
			WRITE_STRING_FIELD(ctename);
			WRITE_UINT_FIELD(ctelevelsup);
			WRITE_BOOL_FIELD(self_reference);
			WRITE_NODE_FIELD(ctecoltypes);
			WRITE_NODE_FIELD(ctecoltypmods);
			WRITE_NODE_FIELD(ctecolcollations);
			break;
		default:
			elog(ERROR, "unrecognized RTE kind: %d", (int) node->rtekind);
			break;
	}

	WRITE_BOOL_FIELD(lateral);
	WRITE_BOOL_FIELD(inh);
	WRITE_BOOL_FIELD(inFromCl);
	WRITE_UINT_FIELD(requiredPerms);
	WRITE_OID_FIELD(checkAsUser);
	WRITE_BITMAPSET_FIELD(selectedCols);
	WRITE_BITMAPSET_FIELD(modifiedCols);

	return deb_node;
}

static DebugNode*
_outAExpr(DebugGraph *graph, const A_Expr *node)
{
	WRITE_NODE_TYPE("AEXPR");

	switch (node->kind)
	{
		case AEXPR_OP:
			addDebugNodeAttribute(deb_node, "kind", "OP");
			WRITE_NODE_FIELD(name);
			break;
		case AEXPR_AND:
			addDebugNodeAttribute(deb_node, "kind", "AND");
			break;
		case AEXPR_OR:
			addDebugNodeAttribute(deb_node, "kind", "OR");
			break;
		case AEXPR_NOT:
			addDebugNodeAttribute(deb_node, "kind", "NOT");
			break;
		case AEXPR_OP_ANY:
			WRITE_NODE_FIELD(name);
			addDebugNodeAttribute(deb_node, "kind", "ANY");
			break;
		case AEXPR_OP_ALL:
			WRITE_NODE_FIELD(name);
			addDebugNodeAttribute(deb_node, "kind", "ALL");
			break;
		case AEXPR_DISTINCT:
			addDebugNodeAttribute(deb_node, "kind", "DISTINCT");
			WRITE_NODE_FIELD(name);
			break;
		case AEXPR_NULLIF:
			addDebugNodeAttribute(deb_node, "kind", "NULLIF");
			WRITE_NODE_FIELD(name);
			break;
		case AEXPR_OF:
			addDebugNodeAttribute(deb_node, "kind", "OF");
			WRITE_NODE_FIELD(name);
			break;
		case AEXPR_IN:
			addDebugNodeAttribute(deb_node, "kind", "IN");
			WRITE_NODE_FIELD(name);
			break;
		default:
			addDebugNodeAttribute(deb_node, "kind", "??");
			break;
	}

	WRITE_NODE_FIELD(lexpr);
	WRITE_NODE_FIELD(rexpr);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static void
_outValue(StringInfo str, const Value *value)
{
	switch (value->type)
	{
		case T_Integer:
			appendStringInfo(str, "%ld", value->val.ival);
			break;
		case T_Float:

			/*
			 * We assume the value is a valid numeric literal and so does not
			 * need quoting.
			 */
			appendStringInfoString(str, value->val.str);
			break;
		case T_String:
			appendStringInfoChar(str, '"');
			_outToken(str, value->val.str);
			appendStringInfoChar(str, '"');
			break;
		case T_BitString:
			/* internal representation already has leading 'b' */
			appendStringInfoString(str, value->val.str);
			break;
		case T_Null:
			/* this is seen only within A_Const, not in transformed trees */
			appendStringInfoString(str, "NULL");
			break;
		default:
			elog(ERROR, "unrecognized node type: %d", (int) value->type);
			break;
	}
}

static DebugNode*
_outColumnRef(DebugGraph *graph, const ColumnRef *node)
{
	WRITE_NODE_TYPE("COLUMNREF");

	WRITE_NODE_FIELD(fields);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outParamRef(DebugGraph *graph, const ParamRef *node)
{
	WRITE_NODE_TYPE("PARAMREF");

	WRITE_INT_FIELD(number);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outAConst(DebugGraph *graph, const A_Const *node)
{
	StringInfo str = makeStringInfo();
	WRITE_NODE_TYPE("A_CONST");

	_outValue(str, &(node->val));
	addDebugNodeAttribute(deb_node, "val", str->data);
	WRITE_LOCATION_FIELD(location);

	pfree(str->data);
	pfree(str);
	return deb_node;
}

static DebugNode*
_outA_Star(DebugGraph *graph, const A_Star *node)
{
	WRITE_NODE_TYPE("A_STAR");

	return deb_node;
}

static DebugNode*
_outA_Indices(DebugGraph *graph, const A_Indices *node)
{
	WRITE_NODE_TYPE("A_INDICES");

	WRITE_NODE_FIELD(lidx);
	WRITE_NODE_FIELD(uidx);

	return deb_node;
}

static DebugNode*
_outA_Indirection(DebugGraph *graph, const A_Indirection *node)
{
	WRITE_NODE_TYPE("A_INDIRECTION");

	WRITE_NODE_FIELD(arg);
	WRITE_NODE_FIELD(indirection);

	return deb_node;
}

static DebugNode*
_outA_ArrayExpr(DebugGraph *graph, const A_ArrayExpr *node)
{
	WRITE_NODE_TYPE("A_ARRAYEXPR");

	WRITE_NODE_FIELD(elements);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outResTarget(DebugGraph *graph, const ResTarget *node)
{
	WRITE_NODE_TYPE("RESTARGET");

	WRITE_STRING_FIELD(name);
	WRITE_NODE_FIELD(indirection);
	WRITE_NODE_FIELD(val);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outSortBy(DebugGraph *graph, const SortBy *node)
{
	WRITE_NODE_TYPE("SORTBY");

	WRITE_NODE_FIELD(node);
	WRITE_ENUM_FIELD(sortby_dir, SortByDir);
	WRITE_ENUM_FIELD(sortby_nulls, SortByNulls);
	WRITE_NODE_FIELD(useOp);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outWindowDef(DebugGraph *graph, const WindowDef *node)
{
	WRITE_NODE_TYPE("WINDOWDEF");

	WRITE_STRING_FIELD(name);
	WRITE_STRING_FIELD(refname);
	WRITE_NODE_FIELD(partitionClause);
	WRITE_NODE_FIELD(orderClause);
	WRITE_INT_FIELD(frameOptions);
	WRITE_NODE_FIELD(startOffset);
	WRITE_NODE_FIELD(endOffset);
	WRITE_LOCATION_FIELD(location);

	return deb_node;
}

static DebugNode*
_outRangeSubselect(DebugGraph *graph, const RangeSubselect *node)
{
	WRITE_NODE_TYPE("RANGESUBSELECT");

	WRITE_BOOL_FIELD(lateral);
	WRITE_NODE_FIELD(subquery);
	WRITE_NODE_FIELD(alias);

	return deb_node;
}

static DebugNode*
_outRangeFunction(DebugGraph *graph, const RangeFunction *node)
{
	WRITE_NODE_TYPE("RANGEFUNCTION");

	WRITE_BOOL_FIELD(lateral);
	WRITE_NODE_FIELD(funccallnode);
	WRITE_NODE_FIELD(alias);
	WRITE_NODE_FIELD(coldeflist);

	return deb_node;
}

static DebugNode*
_outConstraint(DebugGraph *graph, const Constraint *node)
{
	WRITE_NODE_TYPE("CONSTRAINT");

	WRITE_STRING_FIELD(conname);
	WRITE_BOOL_FIELD(deferrable);
	WRITE_BOOL_FIELD(initdeferred);
	WRITE_LOCATION_FIELD(location);

	switch (node->contype)
	{
		case CONSTR_NULL:
			addDebugNodeAttribute(deb_node, "contype", "NULL");
			break;

		case CONSTR_NOTNULL:
			addDebugNodeAttribute(deb_node, "contype", "NOT_NULL");
			break;

		case CONSTR_DEFAULT:
			addDebugNodeAttribute(deb_node, "contype", "DEFAULT");
			WRITE_NODE_FIELD(raw_expr);
			WRITE_STRING_FIELD(cooked_expr);
			break;

		case CONSTR_CHECK:
			addDebugNodeAttribute(deb_node, "contype", "CHECK");
			WRITE_BOOL_FIELD(is_no_inherit);
			WRITE_NODE_FIELD(raw_expr);
			WRITE_STRING_FIELD(cooked_expr);
			break;

		case CONSTR_PRIMARY:
			addDebugNodeAttribute(deb_node, "contype", "PRIMARY_KEY");
			WRITE_NODE_FIELD(keys);
			WRITE_NODE_FIELD(options);
			WRITE_STRING_FIELD(indexname);
			WRITE_STRING_FIELD(indexspace);
			/* access_method and where_clause not currently used */
			break;

		case CONSTR_UNIQUE:
			addDebugNodeAttribute(deb_node, "contype", "UNIQUE");
			WRITE_NODE_FIELD(keys);
			WRITE_NODE_FIELD(options);
			WRITE_STRING_FIELD(indexname);
			WRITE_STRING_FIELD(indexspace);
			/* access_method and where_clause not currently used */
			break;

		case CONSTR_EXCLUSION:
			addDebugNodeAttribute(deb_node, "contype", "EXCLUSION");
			WRITE_NODE_FIELD(exclusions);
			WRITE_NODE_FIELD(options);
			WRITE_STRING_FIELD(indexname);
			WRITE_STRING_FIELD(indexspace);
			WRITE_STRING_FIELD(access_method);
			WRITE_NODE_FIELD(where_clause);
			break;

		case CONSTR_FOREIGN:
			addDebugNodeAttribute(deb_node, "contype", "FOREIGN_KEY");
			WRITE_NODE_FIELD(pktable);
			WRITE_NODE_FIELD(fk_attrs);
			WRITE_NODE_FIELD(pk_attrs);
			WRITE_CHAR_FIELD(fk_matchtype);
			WRITE_CHAR_FIELD(fk_upd_action);
			WRITE_CHAR_FIELD(fk_del_action);
			WRITE_NODE_FIELD(old_conpfeqop);
			WRITE_BOOL_FIELD(skip_validation);
			WRITE_BOOL_FIELD(initially_valid);
			break;

		case CONSTR_ATTR_DEFERRABLE:
			addDebugNodeAttribute(deb_node, "contype", "ATTR_DEFERRABLE");
			break;

		case CONSTR_ATTR_NOT_DEFERRABLE:
			addDebugNodeAttribute(deb_node, "contype", "ATTR_NOT_DEFERRABLE");
			break;

		case CONSTR_ATTR_DEFERRED:
			addDebugNodeAttribute(deb_node, "contype", "ATTR_DEFERRED");
			break;

		case CONSTR_ATTR_IMMEDIATE:
			addDebugNodeAttribute(deb_node, "contype", "ATTR_IMMEDIATE");
			break;

		default:
			addDebugNodeAttributeArgs(deb_node, "contype",
					"<unrecognized_constraint %d>",
							 (int) node->contype);
			break;
	}

	return deb_node;
}


static void _outNode(DebugGraph *graph, const void *obj,
		DebugNode **out_node, char **out_str)
{
	StringInfo      str      = makeStringInfo();
	DebugNode      *deb_node = NULL;

	Assert(out_node && out_str);

	if (obj == NULL)
		appendStringInfo(str, "NULL");
	else if (IsA(obj, List) ||IsA(obj, IntList) || IsA(obj, OidList))
		deb_node = _outList(graph, obj);
	else if (IsA(obj, Integer) ||
			 IsA(obj, Float) ||
			 IsA(obj, String) ||
			 IsA(obj, BitString))
	{
		/* nodeRead does not want to see { } around these! */
		_outValue(str, obj);
	}
	else
	{
		switch (nodeTag(obj))
		{
			case T_PlannedStmt:
				deb_node = _outPlannedStmt(graph, obj);
				break;
			case T_Plan:
				deb_node = _outPlan(graph, obj);
				break;
			case T_Result:
				deb_node = _outResult(graph, obj);
				break;
			case T_ModifyTable:
				deb_node = _outModifyTable(graph, obj);
				break;
			case T_Append:
				deb_node = _outAppend(graph, obj);
				break;
			case T_MergeAppend:
				deb_node = _outMergeAppend(graph, obj);
				break;
			case T_RecursiveUnion:
				deb_node = _outRecursiveUnion(graph, obj);
				break;
			case T_BitmapAnd:
				deb_node = _outBitmapAnd(graph, obj);
				break;
			case T_BitmapOr:
				deb_node = _outBitmapOr(graph, obj);
				break;
			case T_Scan:
				deb_node = _outScan(graph, obj);
				break;
			case T_SeqScan:
				deb_node = _outSeqScan(graph, obj);
				break;
			case T_IndexScan:
				deb_node = _outIndexScan(graph, obj);
				break;
			case T_IndexOnlyScan:
				deb_node = _outIndexOnlyScan(graph, obj);
				break;
			case T_BitmapIndexScan:
				deb_node = _outBitmapIndexScan(graph, obj);
				break;
			case T_BitmapHeapScan:
				deb_node = _outBitmapHeapScan(graph, obj);
				break;
			case T_TidScan:
				deb_node = _outTidScan(graph, obj);
				break;
			case T_SubqueryScan:
				deb_node = _outSubqueryScan(graph, obj);
				break;
			case T_FunctionScan:
				deb_node = _outFunctionScan(graph, obj);
				break;
			case T_ValuesScan:
				deb_node = _outValuesScan(graph, obj);
				break;
			case T_CteScan:
				deb_node = _outCteScan(graph, obj);
				break;
			case T_WorkTableScan:
				deb_node = _outWorkTableScan(graph, obj);
				break;
			case T_ForeignScan:
				deb_node = _outForeignScan(graph, obj);
				break;
			case T_Join:
				deb_node = _outJoin(graph, obj);
				break;
			case T_NestLoop:
				deb_node = _outNestLoop(graph, obj);
				break;
			case T_MergeJoin:
				deb_node = _outMergeJoin(graph, obj);
				break;
			case T_HashJoin:
				deb_node = _outHashJoin(graph, obj);
				break;
			case T_Agg:
				deb_node = _outAgg(graph, obj);
				break;
			case T_WindowAgg:
				deb_node = _outWindowAgg(graph, obj);
				break;
			case T_Group:
				deb_node = _outGroup(graph, obj);
				break;
			case T_Material:
				deb_node = _outMaterial(graph, obj);
				break;
			case T_Sort:
				deb_node = _outSort(graph, obj);
				break;
			case T_Unique:
				deb_node = _outUnique(graph, obj);
				break;
			case T_Hash:
				deb_node = _outHash(graph, obj);
				break;
			case T_SetOp:
				deb_node = _outSetOp(graph, obj);
				break;
			case T_LockRows:
				deb_node = _outLockRows(graph, obj);
				break;
			case T_Limit:
				deb_node = _outLimit(graph, obj);
				break;
			case T_NestLoopParam:
				deb_node = _outNestLoopParam(graph, obj);
				break;
			case T_PlanRowMark:
				deb_node = _outPlanRowMark(graph, obj);
				break;
			case T_PlanInvalItem:
				deb_node = _outPlanInvalItem(graph, obj);
				break;
			case T_Alias:
				deb_node = _outAlias(graph, obj);
				break;
			case T_RangeVar:
				deb_node = _outRangeVar(graph, obj);
				break;
			case T_IntoClause:
				deb_node = _outIntoClause(graph, obj);
				break;
			case T_Var:
				_outVar(str, obj);
				break;
			case T_Const:
				deb_node = _outConst(graph, obj);
				break;
			case T_Param:
				deb_node = _outParam(graph, obj);
				break;
			case T_Aggref:
				deb_node = _outAggref(graph, obj);
				break;
			case T_WindowFunc:
				deb_node = _outWindowFunc(graph, obj);
				break;
			case T_ArrayRef:
				deb_node = _outArrayRef(graph, obj);
				break;
			case T_FuncExpr:
				deb_node = _outFuncExpr(graph, obj);
				break;
			case T_NamedArgExpr:
				deb_node = _outNamedArgExpr(graph, obj);
				break;
			case T_OpExpr:
				deb_node = _outOpExpr(graph, obj);
				break;
			case T_DistinctExpr:
				deb_node = _outDistinctExpr(graph, obj);
				break;
			case T_NullIfExpr:
				deb_node = _outNullIfExpr(graph, obj);
				break;
			case T_ScalarArrayOpExpr:
				deb_node = _outScalarArrayOpExpr(graph, obj);
				break;
			case T_BoolExpr:
				deb_node = _outBoolExpr(graph, obj);
				break;
			case T_SubLink:
				deb_node = _outSubLink(graph, obj);
				break;
			case T_SubPlan:
				deb_node = _outSubPlan(graph, obj);
				break;
			case T_AlternativeSubPlan:
				deb_node = _outAlternativeSubPlan(graph, obj);
				break;
			case T_FieldSelect:
				deb_node = _outFieldSelect(graph, obj);
				break;
			case T_FieldStore:
				deb_node = _outFieldStore(graph, obj);
				break;
			case T_RelabelType:
				deb_node = _outRelabelType(graph, obj);
				break;
			case T_CoerceViaIO:
				deb_node = _outCoerceViaIO(graph, obj);
				break;
			case T_ArrayCoerceExpr:
				deb_node = _outArrayCoerceExpr(graph, obj);
				break;
			case T_ConvertRowtypeExpr:
				deb_node = _outConvertRowtypeExpr(graph, obj);
				break;
			case T_CollateExpr:
				deb_node = _outCollateExpr(graph, obj);
				break;
			case T_CaseExpr:
				deb_node = _outCaseExpr(graph, obj);
				break;
			case T_CaseWhen:
				deb_node = _outCaseWhen(graph, obj);
				break;
			case T_CaseTestExpr:
				deb_node = _outCaseTestExpr(graph, obj);
				break;
			case T_ArrayExpr:
				deb_node = _outArrayExpr(graph, obj);
				break;
			case T_RowExpr:
				deb_node = _outRowExpr(graph, obj);
				break;
			case T_RowCompareExpr:
				deb_node = _outRowCompareExpr(graph, obj);
				break;
			case T_CoalesceExpr:
				deb_node = _outCoalesceExpr(graph, obj);
				break;
			case T_MinMaxExpr:
				deb_node = _outMinMaxExpr(graph, obj);
				break;
			case T_XmlExpr:
				deb_node = _outXmlExpr(graph, obj);
				break;
			case T_NullTest:
				deb_node = _outNullTest(graph, obj);
				break;
			case T_BooleanTest:
				deb_node = _outBooleanTest(graph, obj);
				break;
			case T_CoerceToDomain:
				deb_node = _outCoerceToDomain(graph, obj);
				break;
			case T_CoerceToDomainValue:
				deb_node = _outCoerceToDomainValue(graph, obj);
				break;
			case T_SetToDefault:
				deb_node = _outSetToDefault(graph, obj);
				break;
			case T_CurrentOfExpr:
				deb_node = _outCurrentOfExpr(graph, obj);
				break;
			case T_TargetEntry:
				deb_node = _outTargetEntry(graph, obj);
				break;
			case T_RangeTblRef:
				deb_node = _outRangeTblRef(graph, obj);
				break;
			case T_JoinExpr:
				deb_node = _outJoinExpr(graph, obj);
				break;
			case T_FromExpr:
				deb_node = _outFromExpr(graph, obj);
				break;

			case T_Path:
				deb_node = _outPath(graph, obj);
				break;
			case T_IndexPath:
				deb_node = _outIndexPath(graph, obj);
				break;
			case T_BitmapHeapPath:
				deb_node = _outBitmapHeapPath(graph, obj);
				break;
			case T_BitmapAndPath:
				deb_node = _outBitmapAndPath(graph, obj);
				break;
			case T_BitmapOrPath:
				deb_node = _outBitmapOrPath(graph, obj);
				break;
			case T_TidPath:
				deb_node = _outTidPath(graph, obj);
				break;
			case T_ForeignPath:
				deb_node = _outForeignPath(graph, obj);
				break;
			case T_AppendPath:
				deb_node = _outAppendPath(graph, obj);
				break;
			case T_MergeAppendPath:
				deb_node = _outMergeAppendPath(graph, obj);
				break;
			case T_ResultPath:
				deb_node = _outResultPath(graph, obj);
				break;
			case T_MaterialPath:
				deb_node = _outMaterialPath(graph, obj);
				break;
			case T_UniquePath:
				deb_node = _outUniquePath(graph, obj);
				break;
			case T_NestPath:
				deb_node = _outNestPath(graph, obj);
				break;
			case T_MergePath:
				deb_node = _outMergePath(graph, obj);
				break;
			case T_HashPath:
				deb_node = _outHashPath(graph, obj);
				break;
			case T_PlannerGlobal:
				deb_node = _outPlannerGlobal(graph, obj);
				break;
			case T_PlannerInfo:
				deb_node = _outPlannerInfo(graph, obj);
				break;
			case T_RelOptInfo:
				deb_node = _outRelOptInfo(graph, obj);
				break;
			case T_IndexOptInfo:
				deb_node = _outIndexOptInfo(graph, obj);
				break;
			case T_EquivalenceClass:
				deb_node = _outEquivalenceClass(graph, obj);
				break;
			case T_EquivalenceMember:
				deb_node = _outEquivalenceMember(graph, obj);
				break;
			case T_PathKey:
				deb_node = _outPathKey(graph, obj);
				break;
			case T_ParamPathInfo:
				deb_node = _outParamPathInfo(graph, obj);
				break;
			case T_RestrictInfo:
				deb_node = _outRestrictInfo(graph, obj);
				break;
			case T_PlaceHolderVar:
				deb_node = _outPlaceHolderVar(graph, obj);
				break;
			case T_SpecialJoinInfo:
				deb_node = _outSpecialJoinInfo(graph, obj);
				break;
			case T_LateralJoinInfo:
				deb_node = _outLateralJoinInfo(graph, obj);
				break;
			case T_AppendRelInfo:
				deb_node = _outAppendRelInfo(graph, obj);
				break;
			case T_PlaceHolderInfo:
				deb_node = _outPlaceHolderInfo(graph, obj);
				break;
			case T_MinMaxAggInfo:
				deb_node = _outMinMaxAggInfo(graph, obj);
				break;
			case T_PlannerParamItem:
				deb_node = _outPlannerParamItem(graph, obj);
				break;

			case T_CreateStmt:
				deb_node = _outCreateStmt(graph, obj);
				break;
			case T_CreateForeignTableStmt:
				deb_node = _outCreateForeignTableStmt(graph, obj);
				break;
			case T_IndexStmt:
				deb_node = _outIndexStmt(graph, obj);
				break;
			case T_NotifyStmt:
				deb_node = _outNotifyStmt(graph, obj);
				break;
			case T_DeclareCursorStmt:
				deb_node = _outDeclareCursorStmt(graph, obj);
				break;
			case T_SelectStmt:
				deb_node = _outSelectStmt(graph, obj);
				break;
			case T_ColumnDef:
				deb_node = _outColumnDef(graph, obj);
				break;
			case T_TypeName:
				deb_node = _outTypeName(graph, obj);
				break;
			case T_TypeCast:
				deb_node = _outTypeCast(graph, obj);
				break;
			case T_CollateClause:
				deb_node = _outCollateClause(graph, obj);
				break;
			case T_IndexElem:
				deb_node = _outIndexElem(graph, obj);
				break;
			case T_Query:
				deb_node = _outQuery(graph, obj);
				break;
			case T_SortGroupClause:
				deb_node = _outSortGroupClause(graph, obj);
				break;
			case T_WindowClause:
				deb_node = _outWindowClause(graph, obj);
				break;
			case T_RowMarkClause:
				deb_node = _outRowMarkClause(graph, obj);
				break;
			case T_WithClause:
				deb_node = _outWithClause(graph, obj);
				break;
			case T_CommonTableExpr:
				deb_node = _outCommonTableExpr(graph, obj);
				break;
			case T_SetOperationStmt:
				deb_node = _outSetOperationStmt(graph, obj);
				break;
			case T_RangeTblEntry:
				deb_node = _outRangeTblEntry(graph, obj);
				break;
			case T_A_Expr:
				deb_node = _outAExpr(graph, obj);
				break;
			case T_ColumnRef:
				deb_node = _outColumnRef(graph, obj);
				break;
			case T_ParamRef:
				deb_node = _outParamRef(graph, obj);
				break;
			case T_A_Const:
				deb_node = _outAConst(graph, obj);
				break;
			case T_A_Star:
				deb_node = _outA_Star(graph, obj);
				break;
			case T_A_Indices:
				deb_node = _outA_Indices(graph, obj);
				break;
			case T_A_Indirection:
				deb_node = _outA_Indirection(graph, obj);
				break;
			case T_A_ArrayExpr:
				deb_node = _outA_ArrayExpr(graph, obj);
				break;
			case T_ResTarget:
				deb_node = _outResTarget(graph, obj);
				break;
			case T_SortBy:
				deb_node = _outSortBy(graph, obj);
				break;
			case T_WindowDef:
				deb_node = _outWindowDef(graph, obj);
				break;
			case T_RangeSubselect:
				deb_node = _outRangeSubselect(graph, obj);
				break;
			case T_RangeFunction:
				deb_node = _outRangeFunction(graph, obj);
				break;
			case T_Constraint:
				deb_node = _outConstraint(graph, obj);
				break;
			case T_FuncCall:
				deb_node = _outFuncCall(graph, obj);
				break;
			case T_DefElem:
				deb_node = _outDefElem(graph, obj);
				break;
			case T_TableLikeClause:
				deb_node = _outTableLikeClause(graph, obj);
				break;
			case T_LockingClause:
				deb_node = _outLockingClause(graph, obj);
				break;
			case T_XmlSerialize:
				deb_node = _outXmlSerialize(graph, obj);
				break;

			default:
				/*
				 * This should be an ERROR, but it's too useful to be able to
				 * dump structures that _outNode only understands part of.
				 */
				deb_node = newDebugNodeByPointer(graph, (void*)obj,
						"UNRECOGNIZED");
				addDebugNodeAttributeArgs(deb_node, "pointer", "%p", obj);
				addDebugNodeAttributeArgs(deb_node, "type", "%d",
						(int) nodeTag(obj));
				break;
		}
	}

	*out_node = deb_node;
	*out_str  = (deb_node) ? NULL : str->data;

	if (!*out_str)
		pfree(str->data);
	pfree(str);
}

static DebugGraph *
nodeToGraph(const void *obj, const char *name)
{
	DebugGraph *graph = createDebugGraph(name);

	DebugNode *out_node;
	char *out_str;

	_outNode(graph, obj, &out_node, &out_str);

	if (!out_node)
	{
		out_node = newDebugNode(graph, "Node", "Node");
		addDebugNodeAttribute(out_node, "value", out_str);
		pfree(out_str);
	}

	return graph;
}

void
printDebugGraphNode(const void *obj, const char *name)
{
	DebugGraph *graph = nodeToGraph(obj, name);

	printDebugGraph(graph);

	destroyDebugGraph(graph);
}
