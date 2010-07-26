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
#define DEFAULT_OPTE_SHOW_GEQO_POOLS   false
#define DEFAULT_OPTE_SHOW_CONVERGENCE  false
#define DEFAULT_OPTE_SHOW_SAMPLING     false
#define DEFAULT_OPTE_OUTPUT            stderr
#define DEFAULT_OPTE_OUTPUT_STR        "stderr"

static bool   opte_show             = DEFAULT_OPTE_SHOW;
static bool   opte_show_geqo_pools  = DEFAULT_OPTE_SHOW_GEQO_POOLS;
static bool   opte_show_convergence = DEFAULT_OPTE_SHOW_CONVERGENCE;
static bool   opte_show_sampling    = DEFAULT_OPTE_SHOW_SAMPLING;
static char  *opte_about_str        = "";
static FILE  *opte_output_file      = NULL;
static char  *opte_output_str       = DEFAULT_OPTE_OUTPUT_STR;

static List  *opte_list             = NULL;

/* ////////////////////////////////////////////////////////////////////////// */
static float opteGetTime( opteData *opte );
static void optePrintGEQOPool( int generation, Pool *pool );

/* ////////////////////////////////////////////////////////////////////////// */
/* ///////////////////////// Guc Functions ////////////////////////////////// */
static const char *
show_opte_about(void)
{
	return
	"Optimizer Evaluation (OptE) provides a control structure for optimizer\n"
	"evaluation. Outputs of OptE are sent to PostgreSQL's log file.\n\n"
	"Settings:\n"
	"  set opte_show = {true|false};      - Enables (or don't) OptE output.\n"
	"  set opte_show_convergence = true;  - Optimizers' convergence.\n"
	/*"  set opte_show_geqo_pools = true;"*/
	;
}

static const char *
assign_opte_output(const char *newval, bool doit, GucSource source)
{
	if( !strcmp(newval, "") || !strcmp(newval, DEFAULT_OPTE_OUTPUT_STR) )
	{
		if( doit )
			opte_output_file = DEFAULT_OPTE_OUTPUT;
		return DEFAULT_OPTE_OUTPUT_STR;
	}
	else
	{
		if( doit )
		{
			FILE *new_file;
			new_file = fopen( newval, "a" );
			if( !new_file )
			{
				elog(WARNING, "OptE cannot open file '%s'.", newval);
				return NULL;
			}
			elog(NOTICE, "OptE output using file '%s'.", newval);
			opte_output_file = new_file;
		}
		return newval;
	}
}

void
opteRegisterGuc(void)
{
	opte_output_file = DEFAULT_OPTE_OUTPUT; /* stderr is not a constant */

	DefineCustomStringVariable("opte_about",
							"About OptE",
							"About Optimizer Evaluation (OptE).",
							&opte_about_str, /* only to prevent seg. fault */
#							if POSTGRES_8_4 || POSTGRES_9_0
							"",
#							endif
							PGC_USERSET,
#							if POSTGRES_8_4 || POSTGRES_9_0
							0,
#							endif
							NULL,
							show_opte_about);

	DefineCustomBoolVariable("opte_show",
							"Show OptE",
							"Show informations about optimizers.",
							&opte_show,
#							if POSTGRES_8_4 || POSTGRES_9_0
							DEFAULT_OPTE_SHOW,
#							endif
							PGC_USERSET,
#							if POSTGRES_8_4 || POSTGRES_9_0
							0,
#							endif
							NULL,
							NULL);
	DefineCustomBoolVariable("opte_show_convergence",
							"OptE Convergence",
							"Show optimizer's convergence.",
							&opte_show_convergence,
#							if POSTGRES_8_4 || POSTGRES_9_0
							DEFAULT_OPTE_SHOW_CONVERGENCE,
#							endif
							PGC_USERSET,
#							if POSTGRES_8_4 || POSTGRES_9_0
							0,
#							endif
							NULL,
							NULL);
	DefineCustomBoolVariable("opte_show_sampling",
							"OptE Sampling",
							"Show optimizer's sampling.",
							&opte_show_sampling,
#							if POSTGRES_8_4 || POSTGRES_9_0
							DEFAULT_OPTE_SHOW_SAMPLING,
#							endif
							PGC_USERSET,
#							if POSTGRES_8_4 || POSTGRES_9_0
							0,
#							endif
							NULL,
							NULL);
	DefineCustomBoolVariable("opte_show_geqo_pools",
							"GEQO's pool",
							"Show GEQO's pool evolution.",
							&opte_show_geqo_pools,
#							if POSTGRES_8_4 || POSTGRES_9_0
							DEFAULT_OPTE_SHOW_GEQO_POOLS,
#							endif
							PGC_USERSET,
#							if POSTGRES_8_4 || POSTGRES_9_0
							0,
#							endif
							NULL,
							NULL);
	DefineCustomStringVariable("opte_output",
							"OptE output",
							"Output file for OptE.",
							&opte_output_str,
#							if POSTGRES_8_4 || POSTGRES_9_0
							DEFAULT_OPTE_OUTPUT_STR,
#							endif
							PGC_USERSET,
#							if POSTGRES_8_4 || POSTGRES_9_0
							0,
#							endif
							assign_opte_output,
							NULL);

#	if GEQO_OPTE
	geqoGetOpte_hook       = getOpteByPlannerInfo;
	geqoOpteConverg_hook   = opteConvergence;
	geqoOptePrintPool_hook = optePrintGEQOPool;
#	endif
}

void
opteUnregisterGuc(void)
{
	if( opte_output_file != NULL && opte_output_file != DEFAULT_OPTE_OUTPUT )
	{
		fclose( opte_output_file );
		opte_output_file = DEFAULT_OPTE_OUTPUT;
	}
#	if GEQO_OPTE
	geqoGetOpte_hook       = NULL;
	geqoOpteConverg_hook   = NULL;
	geqoOptePrintPool_hook = NULL;
#	endif
}

/* ////////////////////////////////////////////////////////////////////////// */
static char*
alloc_vsprintf(const char* str, va_list va)
{
    char   *str_value;
    int     len = 100;
    int     print_return;

    if( !str )
        return NULL;

    for(;;)
	{
        str_value = (char*) palloc( sizeof(char) * (len+1) );

        print_return = vsnprintf(str_value, len, str, va);

        if( print_return >= 0 && print_return <= len - 1 )
            break;
        else
		{
            pfree(str_value);
            len *= 2;
        }
    }

    return str_value;
}

static char*
alloc_sprintf(const char* value,...)
{
    char    *str_value;
    va_list  va;

    if( !value )
        return NULL;

    va_start(va, value);
    str_value = alloc_vsprintf(value, va);
    va_end(va);

    return str_value;
}

static const char*
get_renation_name(PlannerInfo *root, int relid)
{
	RangeTblEntry *rte;

	Assert(relid <= list_length(root->parse->rtable));
	rte = rt_fetch(relid, root->parse->rtable);
	return rte->eref->aliasname;
}

void
opte_printf(const char* value,...)
{
    char  *str_1 = NULL;
    char  *str_2 = NULL;
    va_list va;

    if ( !opte_show || !value )
    	return;

	str_1 = alloc_sprintf("OptEval: %s", value);

	if( str_1 )
	{
		va_start(va, value);
		str_2 = alloc_vsprintf(str_1, va);
		va_end(va);
	}

	if ( str_1 )
		pfree(str_1);

	if ( str_2 )
	{
		fprintf(opte_output_file, "%s\n", str_2);
		pfree(str_2);
	}
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
				appendStringInfo(&str2, ", %s", get_renation_name(root, x));
			else
				appendStringInfo(&str2, "%s", get_renation_name(root, x));
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

	fprintf(opte_output_file, "OptEval: Initial Rels: %s\n", str.data);
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

static void
optePrintGEQOPool( int generation, Pool *pool )
{
	int i;

	if ( !opte_show_geqo_pools )
		return;

	for( i=0; i<pool->size; i++)
	{
		opte_printf("GEQO Pool:%d %d %.2f", generation, i, pool->data[i].worth);
	}
}
