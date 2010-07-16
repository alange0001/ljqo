/*
 * ljqo.c
 *
 *   Interface of LJQO Plugin with PostgreSQL and control structures for all
 *   optimizers.
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

#include "ljqo.h"

#include <optimizer/paths.h>
#include <optimizer/geqo.h>
#include <utils/guc.h>
#include <lib/stringinfo.h>
#include <limits.h>

#include "opte.h"
#include "twopo.h"

/*
 * ========================================================================
 * ======================== Default Values ================================
 */

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

/*
 * ========================================================================
 * ====================== Control Structures ==============================
 */

typedef void (*ljqo_register_optimizer) (void);
typedef void (*ljqo_unregister_optimizer) (void);

typedef struct ljqo_optimizer
{
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

/*
 * List of registred algorithms
 */
static ljqo_optimizer optimizers[] =
{
	{"geqo","Genetic Query Optimization (compatibility only)",geqo,NULL,NULL},
#	ifdef REGISTER_TWOPO
	REGISTER_TWOPO,
#	endif
	{ NULL, NULL, NULL, NULL, NULL }
};


/*
 * ========================================================================
 * ====================== Control Functions ===============================
 */

PG_MODULE_MAGIC;

void	_PG_init(void);
void	_PG_fini(void);

/*
 * Join order algorithm selector.
 * This functions is registred in PostreSQL as join_search_hook.
 */
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

	if( levels_needed < ljqo_threshold ) /*num of baserels below the threshold*/
	{
		/* call standard dynamic programming */
		OPTE_PRINT_OPTNAME( "standard" );
		result = standard_join_search(root, levels_needed, initial_rels);

	}
	else if ( ljqo_algorithm != NULL ) /* num of baserels above the threshold */
	{
		/* call algorithm registred in ljqo_algorithm */
		OPTE_PRINT_OPTNAME( ljqo_algorithm_str );
		result = ljqo_algorithm(root, levels_needed, initial_rels );

	}
	else /* exception */
	{
		/* call geqo algorithm */
		OPTE_PRINT_OPTNAME( "geqo" );
		elog(WARNING, PACKAGE_NAME" is loaded but no optimizer is defined. "
				"Please set ljqo_algorithm. Calling GEQO...");
		result = geqo(root, levels_needed, initial_rels);

	}

	OPTE_PRINT_OPTCHEAPEST( result->cheapest_total_path->total_cost );
	OPTE_FINISH( &opte );

	return result;
}

/*
 * Assign a algorithm registred in struct ljqo_optimizer.
 * This function is used by ljqo_algorithm GUC parameter.
 */
static const char *
assign_ljqo_algorithm(const char *newval, bool doit, GucSource source)
{
	ljqo_optimizer *opt = optimizers;

	while( opt->name != NULL )
	{
		if( strcmp(opt->name, newval) == 0 )
		{
			if( doit )
				ljqo_algorithm = opt->search_f;

			return newval;
		}

		opt++;
	}

	return NULL;
}

/*
 * Show informations about the plugin.
 *
 * Example (in psql):
 *  database=# show ljqo_about;
 */
static const char *
show_ljqo_about(void)
{
	ljqo_optimizer *opt = optimizers;
	StringInfoData result;
	const char *intro =
		PACKAGE_NAME", version "PACKAGE_VERSION".\n\n"
		"Settings:\n"
		"  ljqo_threshold = N;    - Trigger ljqo algorithm when the number of\n"
		"                           relations is great or equal to N.\n"
		"  ljqo_algorithm = name; - Name of algorithm to call.\n\n"
		"List of available algorithms:\n";

	initStringInfo(&result);
	appendStringInfoString(&result,intro);

	while( opt->name != NULL )
	{
		appendStringInfo(&result,"  %10s   - %s\n",opt->name,opt->description);
		opt++;
	}

	appendStringInfo(&result, OPTE_LJQO_NOTE);

	return (const char*)result.data;
}

/*
 * When the plugin is loaded:
 *  - Register GUC variables in PostgreSQL;
 *  - Set join_search_hook as ljqo_selector.
 */
void
_PG_init(void)
{
	ljqo_optimizer *opt = optimizers;

	elog(NOTICE,PACKAGE_NAME". "
			"Please type 'show ljqo_about;' for more information.");

	DefineCustomStringVariable("ljqo_about",
							"About "PACKAGE_NAME,
							"About "PACKAGE_NAME".",
							&ljqo_about_str, /* only to prevent seg. fault */
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

	/*
	 * Call register function of each algorithm.
	 */
	while( opt->name != NULL )
	{
		if( opt->register_f )
			opt->register_f();

		opt++;
	}

	OPTE_REGISTER;

	join_search_hook = ljqo_selector;
}

/*
 * Unregister plugin.
 */
void
_PG_fini(void)
{
	ljqo_optimizer *opt = optimizers;

	join_search_hook = NULL;

	while( opt->name != NULL )
	{
		if( opt->unregister_f )
			opt->unregister_f();

		opt++;
	}

	OPTE_UNREGISTER;
}
