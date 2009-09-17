/*
 * opte.h
 *
 *   Optimizer Evaluation (OptE) retains informations and control
 *   structures needed to evaluate the implemented optimizers.
 *
 *   Copyright (C) 2009, Adriano Lange
 *
 *   This file is part of LJQO Plugin.
 *
 *   LJQO Plugin is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   LJQO Plugin is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with LJQO Plugin.  If not, see <http://www.gnu.org/licenses/>.
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
             "      You can learn more aboute it typing 'show opte_about;'."



extern void    opte_printf(const char* value,...);
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

#else  // #if ENABLE_OPTE

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

#endif // #if ENABLE_OPTE

#endif   /* OPTE_H */
