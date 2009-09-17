/*
 * twopo_register.c
 *
 *   Register TwoPO algorithm on PostgreSQL.
 *
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

#include "twopo.h"
#include <utils/guc.h>

static char *twopo_about_str = "";

#define C_STR( val ) #val
#define R_STR( val ) C_STR(val)

static const char*
show_twopo_about(void)
{
	return
	"Two-Phase Optimizer (TwoPO)\n\n"
	"Settings:\n"
	"  twopo_bushy_space = {true|false}       - set it to false if you want only left-deep trees\n"
	"                                           default=true\n"
	"  twopo_heuristic_states = {true|false}  - enables heuristic to initial states\n"
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
#			if POSTGRES_8_4
			"",
#			endif
			PGC_USERSET,
#			if POSTGRES_8_4
			0,
#			endif
			NULL,
			show_twopo_about);
	DefineCustomBoolVariable("twopo_bushy_space",
			"TwoPO Bushy-tree Space",
			"Search plans in bushy-tree space.",
			&twopo_bushy_space,
#			if POSTGRES_8_4
			DEFAULT_TWOPO_BUSHY_SPACE,
#			endif
			PGC_USERSET,
#			if POSTGRES_8_4
			0,
#			endif
			NULL,
			NULL);
	DefineCustomBoolVariable("twopo_heuristic_states",
			"TwoPO Heuristic States",
			"Enables heuristic initial states.",
			&twopo_heuristic_states,
#			if POSTGRES_8_4
			DEFAULT_TWOPO_HEURISTIC_STATES,
#			endif
			PGC_USERSET,
#			if POSTGRES_8_4
			0,
#			endif
			NULL,
			NULL);
	DefineCustomIntVariable("twopo_ii_stop",
			"TwoPO II-phase Stop",
			"Number of randomized initial states in Iterative "
			"Improvement phase.",
			&twopo_ii_stop,
#			if POSTGRES_8_4
			DEFAULT_TWOPO_II_STOP,
#			endif
			MIN_TWOPO_II_STOP,
			MAX_TWOPO_II_STOP,
			PGC_USERSET,
#			if POSTGRES_8_4
			0,
#			endif
			NULL,
			NULL);
	DefineCustomBoolVariable("twopo_ii_improve_states",
			"TwoPO II Improve States",
			"Enables improvement of plans in Iterative Improvement phase.",
			&twopo_ii_improve_states,
#			if POSTGRES_8_4
			DEFAULT_TWOPO_II_IMPROVE_STATES,
#			endif
			PGC_USERSET,
#			if POSTGRES_8_4
			0,
#			endif
			NULL,
			NULL);
	DefineCustomBoolVariable("twopo_sa_phase",
			"TwoPO SA Phase",
			"Enables Simulated Annealing phase.",
			&twopo_sa_phase,
#			if POSTGRES_8_4
			DEFAULT_TWOPO_SA_PHASE,
#			endif
			PGC_USERSET,
#			if POSTGRES_8_4
			0,
#			endif
			NULL,
			NULL);
	DefineCustomRealVariable("twopo_sa_initial_temperature",
			"TwoPO SA Initial Temperature",
			"Initial temperature in SA phase: Ti = X * cost(S0).",
			&twopo_sa_initial_temperature,
#			if POSTGRES_8_4
			DEFAULT_TWOPO_SA_INITIAL_TEMPERATURE,
#			endif
			MIN_TWOPO_SA_INITIAL_TEMPERATURE,
			MAX_TWOPO_SA_INITIAL_TEMPERATURE,
			PGC_USERSET,
#			if POSTGRES_8_4
			0,
#			endif
			NULL,
			NULL);
	DefineCustomRealVariable("twopo_sa_temperature_reduction",
			"TwoPO SA Temperature Reduction",
			"Temperature reduction in SA phase: Tnew = X * T.",
			&twopo_sa_temperature_reduction,
#			if POSTGRES_8_4
			DEFAULT_TWOPO_SA_TEMPERATURE_REDUCTION,
#			endif
			MIN_TWOPO_SA_TEMPERATURE_REDUCTION,
			MAX_TWOPO_SA_TEMPERATURE_REDUCTION,
			PGC_USERSET,
#			if POSTGRES_8_4
			0,
#			endif
			NULL,
			NULL);
	DefineCustomIntVariable("twopo_sa_equilibrium",
			"TwoPO SA Equilibrium",
			"Number of generated states for each temperature: N = X * Joins.",
			&twopo_sa_equilibrium,
#			if POSTGRES_8_4
			DEFAULT_TWOPO_SA_EQUILIBRIUM,
#			endif
			MIN_TWOPO_SA_EQUILIBRIUM,
			MAX_TWOPO_SA_EQUILIBRIUM,
			PGC_USERSET,
#			if POSTGRES_8_4
			0,
#			endif
			NULL,
			NULL);
#	ifdef TWOPO_CACHE_PLANS
	DefineCustomBoolVariable("twopo_cache_plans",
			"TwoPO Cache Plans",
			"Enables TwoPO store plans generated earlier.",
			&twopo_cache_plans,
#			if POSTGRES_8_4
			DEFAULT_TWOPO_CACHE_PLANS,
#			endif
			PGC_USERSET,
#			if POSTGRES_8_4
			0,
#			endif
			NULL,
			NULL);
	DefineCustomIntVariable("twopo_cache_size",
			"TwoPO Cache Size",
			"Limits the memory used to cache plans (in KB).",
			&twopo_cache_size,
#			if POSTGRES_8_4
			DEFAULT_TWOPO_CACHE_SIZE,
#			endif
			MIN_TWOPO_CACHE_SIZE,
			MAX_TWOPO_CACHE_SIZE,
			PGC_USERSET,
#			if POSTGRES_8_4
			0,
#			endif
			NULL,
			NULL);
#	endif

}
