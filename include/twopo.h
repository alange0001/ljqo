/*
 * twopo.h
 *
 *   Two Phase Optimization for PostgreSQL
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

#ifndef TWOPO_H
#define TWOPO_H

#include "ljqo.h"
#include <limits.h>
#include <nodes/relation.h>
#include <nodes/memnodes.h>

#ifdef HAVE_MEMSTATS
#define TWOPO_CACHE_PLANS
#endif

#define DEFAULT_TWOPO_BUSHY_SPACE               true
#define DEFAULT_TWOPO_HEURISTIC_STATES          true
#define DEFAULT_TWOPO_II_STOP                   10
#define     MIN_TWOPO_II_STOP                   1
#define     MAX_TWOPO_II_STOP                   INT_MAX
#define DEFAULT_TWOPO_II_IMPROVE_STATES         true
#define DEFAULT_TWOPO_SA_PHASE                  true
#define DEFAULT_TWOPO_SA_INITIAL_TEMPERATURE    0.1
#define     MIN_TWOPO_SA_INITIAL_TEMPERATURE    0.01
#define     MAX_TWOPO_SA_INITIAL_TEMPERATURE    2.0
#define DEFAULT_TWOPO_SA_TEMPERATURE_REDUCTION  0.95
#define     MIN_TWOPO_SA_TEMPERATURE_REDUCTION  0.1
#define     MAX_TWOPO_SA_TEMPERATURE_REDUCTION  0.95
#define DEFAULT_TWOPO_SA_EQUILIBRIUM            16
#define     MIN_TWOPO_SA_EQUILIBRIUM            1
#define     MAX_TWOPO_SA_EQUILIBRIUM            INT_MAX
#ifdef TWOPO_CACHE_PLANS
#define DEFAULT_TWOPO_CACHE_PLANS               true
#define DEFAULT_TWOPO_CACHE_SIZE                51200
#define     MIN_TWOPO_CACHE_SIZE                512
#define     MAX_TWOPO_CACHE_SIZE                INT_MAX
#endif

extern bool   twopo_bushy_space;
extern bool   twopo_heuristic_states;
extern int    twopo_ii_stop;
extern bool   twopo_ii_improve_states;
extern bool   twopo_sa_phase;
extern double twopo_sa_initial_temperature;    /* T = X * cost(S0) */
extern double twopo_sa_temperature_reduction;  /* Tnew = X * Told */
extern int    twopo_sa_equilibrium;            /* E * Joins */
#ifdef TWOPO_CACHE_PLANS
extern bool   twopo_cache_plans;
extern int    twopo_cache_size;  /* limit the size of temporary mem ctx (KB) */
#endif

extern RelOptInfo *twopo(PlannerInfo *root,
		int number_of_rels, List *initial_rels);

#ifdef LJQO
#define REGISTER_TWOPO \
	{ \
		"twopo", \
		"Two-Phase Optimization (experimental)", \
		twopo, \
		twopo_register, \
		NULL \
	}
extern void twopo_register(void);
#endif

#endif   /* TWOPO_H */
