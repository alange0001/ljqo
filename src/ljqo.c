/*
 * ljqo.c
 *
 *   Plugin interface with PostgreSQL and control structure for all
 *   optimizers.
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

#include "ljqo.h"

#include <optimizer/paths.h>
#include <optimizer/geqo.h>
#include <utils/guc.h>
#include <lib/stringinfo.h>
#include <limits.h>

#include "opte.h"
#include "twopo.h"

///////////////////////////////////////////////////////////////////////////
/////////////////////////////// Defaults //////////////////////////////////

#define DEFAULT_LJQO_THRESHOLD          12
#define     MIN_LJQO_THRESHOLD          2
#define     MAX_LJQO_THRESHOLD          INT_MAX
#ifdef REGISTER_TWOPO
#	define DEFAULT_LJQO_ALGORITHM       twopo
#	define DEFAULT_LJQO_ALGORITHM_STR  "twopo"
#else
#	define DEFAULT_LJQO_ALGORITHM       geqo
#	define DEFAULT_LJQO_ALGORITHM_STR  "geqo"
#endif

///////////////////////////////////////////////////////////////////////////
//////////////////// Optimizers' Control Structure ////////////////////////

typedef void (*ljqo_register_optimizer) (void);
typedef void (*ljqo_unregister_optimizer) (void);

typedef struct ljqo_optimizer {
	const char                *name;
	const char                *description;
	join_search_hook_type      search_f;
	ljqo_register_optimizer    register_f;
	ljqo_unregister_optimizer  unregister_f;
} ljqo_optimizer;

static int                     ljqo_threshold = DEFAULT_LJQO_THRESHOLD;
static join_search_hook_type   ljqo_algorithm = DEFAULT_LJQO_ALGORITHM;
static char                   *ljqo_algorithm_str = DEFAULT_LJQO_ALGORITHM_STR;
static char                   *ljqo_about_str = "";

static ljqo_optimizer optimizers[] =
{
	{"geqo","Genetic Query Optimization (compatibility only)",geqo,NULL,NULL},
#	ifdef REGISTER_TWOPO
	REGISTER_TWOPO,
#	endif
	{ NULL, NULL, NULL, NULL, NULL }
};

///////////////////////////////////////////////////////////////////////////
///////////////////////// Control Functions ///////////////////////////////

PG_MODULE_MAGIC;

void	_PG_init(void);
void	_PG_fini(void);

static
RelOptInfo *
ljqo_selector(PlannerInfo *root, int levels_needed, List *initial_rels)
{
	OPTE_DECLARE( opte );
	RelOptInfo *result;

	OPTE_PRINT_STRING( "=======================" );
	OPTE_INIT( &opte, root );
	OPTE_PRINT_NUMRELS( levels_needed );
	OPTE_PRINT_INITIALRELS( root, initial_rels );

	if( levels_needed < ljqo_threshold ) {

		OPTE_PRINT_OPTNAME( "standard" );
		result = standard_join_search(root, levels_needed, initial_rels);

	} else if ( ljqo_algorithm != NULL ) {

		OPTE_PRINT_OPTNAME( ljqo_algorithm_str );
		result = ljqo_algorithm(root, levels_needed, initial_rels );

	} else { // exception

		OPTE_PRINT_OPTNAME( "geqo" );
		elog(WARNING, PACKAGE_NAME" is loaded but no optimizer is defined. "
				"Please set ljqo_algorithm. Calling GEQO...");
		result = geqo(root, levels_needed, initial_rels);

	}

	OPTE_PRINT_OPTCHEAPEST( result->cheapest_total_path->total_cost );
	OPTE_FINISH( &opte );

	return result;
}

static const char *
assign_ljqo_algorithm(const char *newval, bool doit, GucSource source)
{
	ljqo_optimizer *opt = optimizers;

	while( opt->name != NULL ){
		if( strcmp(opt->name, newval) == 0 ){
			if( doit ) {
				ljqo_algorithm = opt->search_f;
			}
			return newval;
		}

		opt++;
	}

	return NULL;
}

static const char *
show_ljqo_about(void)
{
	ljqo_optimizer *opt = optimizers;
	StringInfoData result;
	const char *intro =
		"This is the "PACKAGE_NAME", version "PACKAGE_VERSION"!\n\n"
		"Settings:\n"
		"  set ljqo_threshold = N;    - Trigger ljqo algorithm when the number of\n"
		"                               relations is great or equal to N.\n"
		"  set ljqo_algorithm = name; - Name of algorithm to call.\n\n"
		"List of available algorithms:\n";

	initStringInfo(&result);
	appendStringInfoString(&result,intro);

	while( opt->name != NULL ) {
		appendStringInfo(&result,"  %10s   - %s\n",opt->name,opt->description);
		opt++;
	}

	appendStringInfo(&result, OPTE_LJQO_NOTE);

	return (const char*)result.data;
}

void
_PG_init(void)
{
	ljqo_optimizer *opt = optimizers;

	elog(NOTICE,"This is the "PACKAGE_NAME"! "
			"Please type 'show ljqo_about;' for more information.");

	DefineCustomStringVariable("ljqo_about",
							"About "PACKAGE_NAME,
							"About "PACKAGE_NAME".",
							&ljqo_about_str, // only to prevent seg. fault
#							if POSTGRES_8_4
							"",
#							endif
							PGC_USERSET,
#							if POSTGRES_8_4
							0,
#							endif
							NULL,
							show_ljqo_about);

	DefineCustomIntVariable("ljqo_threshold",
							"LJQO Threshold",
							"LJQO Threshold.",
							&ljqo_threshold,
#							if POSTGRES_8_4
							DEFAULT_LJQO_THRESHOLD,
#							endif
							MIN_LJQO_THRESHOLD,
							MAX_LJQO_THRESHOLD,
							PGC_USERSET,
#							if POSTGRES_8_4
							0,
#							endif
							NULL,
							NULL);

	DefineCustomStringVariable("ljqo_algorithm",
							"LJQO Algorithm",
							"Defines the algorithm used by "PACKAGE_NAME".",
							&ljqo_algorithm_str,
#							if POSTGRES_8_4
							DEFAULT_LJQO_ALGORITHM_STR,
#							endif
							PGC_USERSET,
#							if POSTGRES_8_4
							0,
#							endif
							assign_ljqo_algorithm,
							NULL);

	while( opt->name != NULL ){
		if( opt->register_f )
			opt->register_f();

		opt++;
	}

	OPTE_REGISTER;

	join_search_hook = ljqo_selector;
}

void
_PG_fini(void)
{
	ljqo_optimizer *opt = optimizers;

	join_search_hook = NULL;

	while( opt->name != NULL ){
		if( opt->unregister_f )
			opt->unregister_f();

		opt++;
	}

	OPTE_UNREGISTER;
}