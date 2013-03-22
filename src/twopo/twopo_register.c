/*
 * twopo_register.c
 *
 *   Register TwoPO algorithm on PostgreSQL.
 *
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

#include "twopo.h"
#include <utils/guc.h>

static char *twopo_about_str = "";

#define C_STR( val ) #val
#define R_STR( val ) C_STR(val)

static const char*
show_twopo_about(void)
{
	return
	"Two-Phase Optimization (TwoPO)\n\n"
	"Settings:\n"
	"  twopo_bushy_space = {true|false}       - set it to false if you want only deep trees\n"
	"                                           default=true\n"
	"  twopo_heuristic_states = {true|false}  - enables heuristic for initial states\n"
	"                                           default=true\n"
	"  twopo_ii_stop = Int                    - number of initial states\n"
	"                                           default="R_STR(DEFAULT_TWOPO_II_STOP)"\n"
	"  twopo_ii_improve_states = {true|false} - find local-minimum of each initial state\n"
	"                                           default=true\n"
	"  twopo_sa_phase = {true|false}          - enables Simulated Annealing (SA) phase\n"
	"                                           default=true\n"
	"  twopo_sa_initial_temperature = Float   - initial temperature for SA phase\n"
	"                                           default="R_STR(DEFAULT_TWOPO_SA_INITIAL_TEMPERATURE)"\n"
	"  twopo_sa_temperature_reduction = Float - temperature reduction\n"
	"                                           default="R_STR(DEFAULT_TWOPO_SA_TEMPERATURE_REDUCTION)"\n"
	"  twopo_sa_equilibrium = Int             - number of states generated for each temperature\n"
	"                                           (Int * State Size)\n"
	"                                           default="R_STR(DEFAULT_TWOPO_SA_EQUILIBRIUM)"\n"
	;
}

void
twopo_register(void)
{

	DefineCustomStringVariable("twopo_about",
			"About TwoPO",
			"",
			&twopo_about_str,
			"",
			PGC_USERSET,
			0,
			NULL,
			NULL,
			show_twopo_about);
	DefineCustomBoolVariable("twopo_bushy_space",
			"TwoPO Bushy-tree Space",
			"Search plans in bushy-tree space.",
			&twopo_bushy_space,
			DEFAULT_TWOPO_BUSHY_SPACE,
			PGC_USERSET,
			0,
			NULL,
			NULL,
			NULL);
	DefineCustomBoolVariable("twopo_heuristic_states",
			"TwoPO Heuristic States",
			"Enables heuristic initial states.",
			&twopo_heuristic_states,
			DEFAULT_TWOPO_HEURISTIC_STATES,
			PGC_USERSET,
			0,
			NULL,
			NULL,
			NULL);
	DefineCustomIntVariable("twopo_ii_stop",
			"TwoPO II-phase Stop",
			"Number of randomized initial states in Iterative "
			"Improvement phase.",
			&twopo_ii_stop,
			DEFAULT_TWOPO_II_STOP,
			MIN_TWOPO_II_STOP,
			MAX_TWOPO_II_STOP,
			PGC_USERSET,
			0,
			NULL,
			NULL,
			NULL);
	DefineCustomBoolVariable("twopo_ii_improve_states",
			"TwoPO II Improve States",
			"Enables improvement of plans in Iterative Improvement phase.",
			&twopo_ii_improve_states,
			DEFAULT_TWOPO_II_IMPROVE_STATES,
			PGC_USERSET,
			0,
			NULL,
			NULL,
			NULL);
	DefineCustomBoolVariable("twopo_sa_phase",
			"TwoPO SA Phase",
			"Enables Simulated Annealing phase.",
			&twopo_sa_phase,
			DEFAULT_TWOPO_SA_PHASE,
			PGC_USERSET,
			0,
			NULL,
			NULL,
			NULL);
	DefineCustomRealVariable("twopo_sa_initial_temperature",
			"TwoPO SA Initial Temperature",
			"Initial temperature in SA phase: Ti = X * cost(S0).",
			&twopo_sa_initial_temperature,
			DEFAULT_TWOPO_SA_INITIAL_TEMPERATURE,
			MIN_TWOPO_SA_INITIAL_TEMPERATURE,
			MAX_TWOPO_SA_INITIAL_TEMPERATURE,
			PGC_USERSET,
			0,
			NULL,
			NULL,
			NULL);
	DefineCustomRealVariable("twopo_sa_temperature_reduction",
			"TwoPO SA Temperature Reduction",
			"Temperature reduction in SA phase: Tnew = X * T.",
			&twopo_sa_temperature_reduction,
			DEFAULT_TWOPO_SA_TEMPERATURE_REDUCTION,
			MIN_TWOPO_SA_TEMPERATURE_REDUCTION,
			MAX_TWOPO_SA_TEMPERATURE_REDUCTION,
			PGC_USERSET,
			0,
			NULL,
			NULL,
			NULL);
	DefineCustomIntVariable("twopo_sa_equilibrium",
			"TwoPO SA Equilibrium",
			"Number of generated states for each temperature: N = X * Joins.",
			&twopo_sa_equilibrium,
			DEFAULT_TWOPO_SA_EQUILIBRIUM,
			MIN_TWOPO_SA_EQUILIBRIUM,
			MAX_TWOPO_SA_EQUILIBRIUM,
			PGC_USERSET,
			0,
			NULL,
			NULL,
			NULL);
#	ifdef TWOPO_CACHE_PLANS
	DefineCustomBoolVariable("twopo_cache_plans",
			"TwoPO Cache Plans",
			"Enables TwoPO store plans generated earlier.",
			&twopo_cache_plans,
			DEFAULT_TWOPO_CACHE_PLANS,
			PGC_USERSET,
			0,
			NULL,
			NULL,
			NULL);
	DefineCustomIntVariable("twopo_cache_size",
			"TwoPO Cache Size",
			"Limits the memory used to cache plans (in KB).",
			&twopo_cache_size,
			DEFAULT_TWOPO_CACHE_SIZE,
			MIN_TWOPO_CACHE_SIZE,
			MAX_TWOPO_CACHE_SIZE,
			PGC_USERSET,
			0,
			NULL,
			NULL,
			NULL);
#	endif

}

