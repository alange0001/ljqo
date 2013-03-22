/*
 * opte.c
 *
 *   Optimizer Evaluation (OptE) retains informations and control
 *   structures needed to evaluate the implemented optimizers.
 *
 * Copyright (C) 2009-2010, Adriano Lange
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

#include "opte.h"

#include <optimizer/pathnode.h>
#include <optimizer/paths.h>
#include <optimizer/geqo.h>
#include <parser/parsetree.h>
#include <lib/stringinfo.h>
#include <utils/guc.h>


#define DEFAULT_OPTE_SHOW              true
#define DEFAULT_OPTE_SHOW_CONVERGENCE  false
#define DEFAULT_OPTE_SHOW_SAMPLING     false

static bool   opte_show             = DEFAULT_OPTE_SHOW;
static bool   opte_show_convergence = DEFAULT_OPTE_SHOW_CONVERGENCE;
static bool   opte_show_sampling    = DEFAULT_OPTE_SHOW_SAMPLING;
static char  *opte_about_str        = "";

static List  *opte_list             = NULL;

/* ////////////////////////////////////////////////////////////////////////// */
static float opteGetTime( opteData *opte );

/* ////////////////////////////////////////////////////////////////////////// */
/* ///////////////////////// Guc Functions ////////////////////////////////// */
static const char *
show_opte_about(void)
{
	return
	"Optimizer Evaluation (OptE) provides a control structure for optimizer\n"
	"evaluation. The output of OptE are sent to PostgreSQL's log file.\n\n"
	"Settings:\n"
	"  set opte_show = {true|false};      - Enables (or don't) OptE output.\n"
	"  set opte_show_convergence = true;  - Optimizers' convergence.\n"
	;
}

void
opteRegisterGuc(void)
{
	DefineCustomStringVariable("opte_about",
							"About OptE",
							"About Optimizer Evaluation (OptE).",
							&opte_about_str, /* only to prevent seg. fault */
							"",
							PGC_USERSET,
							0,
							NULL,
							NULL,
							show_opte_about);

	DefineCustomBoolVariable("opte_show",
							"Show OptE",
							"Show informations about optimizers.",
							&opte_show,
							DEFAULT_OPTE_SHOW,
							PGC_USERSET,
							0,
							NULL,
							NULL,
							NULL);
	DefineCustomBoolVariable("opte_show_convergence",
							"OptE Convergence",
							"Show optimizer's convergence.",
							&opte_show_convergence,
							DEFAULT_OPTE_SHOW_CONVERGENCE,
							PGC_USERSET,
							0,
							NULL,
							NULL,
							NULL);
	DefineCustomBoolVariable("opte_show_sampling",
							"OptE Sampling",
							"Show optimizer's sampling.",
							&opte_show_sampling,
							DEFAULT_OPTE_SHOW_SAMPLING,
							PGC_USERSET,
							0,
							NULL,
							NULL,
							NULL);
}

void
opteUnregisterGuc(void)
{
}

/* ////////////////////////////////////////////////////////////////////////// */

static const char*
get_relation_name(PlannerInfo *root, int relid)
{
	RangeTblEntry *rte;

	Assert(relid <= list_length(root->parse->rtable));
	rte = rt_fetch(relid, root->parse->rtable);
	return rte->eref->aliasname;
}

void
opte_print_initial_rels(PlannerInfo *root, List *initial_rels)
{
	ListCell       *lc;
	StringInfoData  str,
	                str2;

    if ( !opte_show || !root || !initial_rels )
    	return;

	initStringInfo(&str);
	initStringInfo(&str2);


	foreach(lc, initial_rels)
	{
		RelOptInfo *rel = (RelOptInfo *) lfirst(lc);
		int         count = 0;
		Relids		tmprelids;
		int			x;

		resetStringInfo(&str2);

		tmprelids = bms_copy(rel->relids);
		while ((x = bms_first_member(tmprelids)) >= 0)
		{
			if (count)
				appendStringInfo(&str2, ", %s", get_relation_name(root, x));
			else
				appendStringInfo(&str2, "%s", get_relation_name(root, x));
			count++;
		}
		bms_free(tmprelids);

		if( str.len )
			appendStringInfoString(&str, ", ");

		if( count > 1 )
			appendStringInfo(&str, "(%s)", str2.data);
		else
			appendStringInfo(&str, "%s", str2.data);
	}

	opte_printf("Initial Rels: %s", str.data);
}

void
opteInit(opteData *opte, PlannerInfo *planner_info)
{
	Assert( opte != NULL );
	Assert( planner_info != NULL );

	gettimeofday(&(opte->start_time), NULL);
	opte->plan_count = 0;
	opte->plan_min_cost = 0;

	opte->planner_info = planner_info;
	opte_list = lappend( opte_list, opte );
}

void
opteFinish(opteData* opte)
{
	if( opte == NULL || opte_list == NULL )
		return;

	opte_printf("Generated Plans: %d",
			(opte->plan_count > 0) ?opte->plan_count :1);
	opte_printf("Optimization Time = %f", opteGetTime( opte ) );

	opte_list = list_delete( opte_list, opte );
}

opteData *
getOpteByPlannerInfo(PlannerInfo *planner_info)
{
	ListCell *x;

	if( opte_list )
	{
		Assert( planner_info != NULL );

		foreach(x, opte_list)
		{
			opteData *opte = (opteData*)lfirst(x);
			if( opte->planner_info == planner_info )
				return opte;
		}
	}

	return NULL;
}

static float
opteGetTime( opteData *opte )
{
	struct timeval stop_time;

	if( opte == NULL )
		return 0;

	gettimeofday(&stop_time, NULL);
	return ((stop_time.tv_sec*1000.0 + stop_time.tv_usec/1000.0)-
	        (opte->start_time.tv_sec*1000.0 +
	         opte->start_time.tv_usec/1000.0));
}

void
opteConvergence( opteData *opte, Cost generated_cost )
{
	struct timeval stop_time;
	float          ms;

	if( opte == NULL || generated_cost <= 0 )
		return;

	opte->plan_count++;

	if ( opte_show_sampling )
		opte_printf("Sample:%d %.2f", opte->plan_count, generated_cost);

	if( opte->plan_min_cost <= 0 || opte->plan_min_cost > generated_cost )
	{

		opte->plan_min_cost = generated_cost;

		if ( !opte_show_convergence )
			return;

		gettimeofday(&stop_time, NULL);
		ms =  ((stop_time.tv_sec*1000.0 + stop_time.tv_usec/1000.0)-
				(opte->start_time.tv_sec*1000.0 +
				 opte->start_time.tv_usec/1000.0));

		opte_printf("Convergence:%.2f %d %.2f", ms, opte->plan_count,
				opte->plan_min_cost);

	}
}
