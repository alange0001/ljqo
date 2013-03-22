/*
 * opte.h
 *
 *   Optimizer Evaluation (OptE) retains informations and control
 *   structures needed to evaluate the implemented optimizers.
 *
 * Copyright (C) 2009-2013, Adriano Lange
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

#ifndef OPTE_H
#define OPTE_H

#include "ljqo.h"

#if ENABLE_OPTE

#include <nodes/relation.h>
#include <optimizer/geqo_gene.h>
#include <sys/time.h>

typedef struct opteData {
	PlannerInfo    *planner_info;
	struct timeval  start_time;
	int             plan_count;
	Cost            plan_min_cost;
} opteData;



extern void  opteRegisterGuc(void);
extern void  opteUnregisterGuc(void);
#define      OPTE_REGISTER opteRegisterGuc()
#define      OPTE_UNREGISTER opteUnregisterGuc()
#define      OPTE_LJQO_NOTE "\n" \
             "NOTE: Optimizer Evaluation (OptE) is enabled.\n" \
             "      You can learn more about it typing 'show opte_about;'."

#define opte_printf(format,...) elog(DEBUG1, "OptEval: " format, ##__VA_ARGS__)

extern void    opte_print_initial_rels(PlannerInfo *root, List *initial_rels);
extern void    opteInit( opteData *opte, PlannerInfo *planner_info );
extern void    opteFinish( opteData* opte );
extern opteData *getOpteByPlannerInfo( PlannerInfo *planner_info );
extern void    opteConvergence( opteData *opte, Cost generated_cost );


#define OPTE_DECLARE( var ) \
	opteData var
#define OPTE_INIT( opte_ptr, root_ptr ) \
	opteInit( opte_ptr, root_ptr )
#define OPTE_FINISH( opte_ptr ) \
	opteFinish( opte_ptr )
#define OPTE_GET_BY_PLANNERINFO( opte_ptr, root_ptr ) \
	opte_ptr = getOpteByPlannerInfo( root_ptr )
#define OPTE_PRINT_STRING( str ) \
	opte_printf( str )
#define OPTE_PRINT_NUMRELS( num ) \
	opte_printf("Number of Relations = %d", num)
#define OPTE_PRINT_INITIALRELS( root, initial_rels ) \
	opte_print_initial_rels(root, initial_rels)
#define OPTE_PRINT_OPTNAME( string ) \
	opte_printf("Calling Optimizer = %s", string)
#define OPTE_PRINT_OPTCHEAPEST( cost ) \
	opte_printf("Cheapest Total Cost = %.2lf", cost)
#define OPTE_CONVERG( opte_ptr, generated_cost ) \
	opteConvergence( opte_ptr, generated_cost )

#else  /* if ENABLE_OPTE is not defined */

#define OPTE_REGISTER
#define OPTE_UNREGISTER
#define OPTE_LJQO_NOTE ""
#define OPTE_DECLARE( var )
#define OPTE_INIT( opte_ptr, root_ptr )
#define OPTE_FINISH( opte_ptr )
#define OPTE_GET_BY_PLANNERINFO( opte_ptr, root_ptr )
#define OPTE_PRINT_STRING( str )
#define OPTE_PRINT_NUMRELS( num )
#define OPTE_PRINT_INITIALRELS( root, initial_rels )
#define OPTE_PRINT_OPTNAME( string )
#define OPTE_PRINT_OPTCHEAPEST( cost )
#define OPTE_CONVERG( opte_ptr, generated_cost )

#endif

#endif   /* OPTE_H */
