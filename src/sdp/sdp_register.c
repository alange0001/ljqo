/*------------------------------------------------------------------------
 *
 * sdp_register.c
 *	  Information data about SDP for LJQO Plugin.
 *
 * Copyright (c) 2013, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
/* contributed by:
   ========================================================================
   *  Adriano Lange	             * Federal University of Paraná (UFPR)    *
   *  alange0001@gmail.com       * State University of Mato Grosso do Sul *
   *                             * (UEMS)                                 *
   *                             *                Brazil                  *
   ========================================================================
 */

#include "sdp.h"
#include <utils/guc.h>

static char *sdp_about_str = "";

#define C_STR( val ) #val
#define R_STR( val ) C_STR(val)

static const char*
show_sdp_about(void)
{
	return
	"Sampling and Dynamic Programming (SDP) optimizer\n\n"
	"Settings:\n"
	"  sdp_iteration_factor = Int   - factor that defines the number of \n"
	"                                 iterations performed by S-Phase\n"
	"    sdp_min_iterations = Int   - Minimum number of iterations in S-Phase\n"
	"    sdp_max_iterations = Int   - Maximum number of iterations in S-Phase"
	;
}

void
sdp_register(void)
{
	DefineCustomStringVariable("sdp_about",
			"About SDP",
			"",
			&sdp_about_str,
			"",
			PGC_USERSET,
			0,
			NULL,
			NULL,
			show_sdp_about);
	DefineCustomIntVariable("sdp_iteration_slope",
			"Iteration factor slope",
			"SDP S-Phase's iteration slope",
			&sdp_iteration_slope,
			DEFAULT_SDP_ITERATION_SLOPE,
			MIN_SDP_ITERATION_SLOPE,
			MAX_SDP_ITERATION_SLOPE,
			PGC_USERSET,
			0,
			NULL,
			NULL,
			NULL);
	DefineCustomIntVariable("sdp_iteration_const",
			"Iteration factor constant",
			"SDP S-Phase's iteration constant",
			&sdp_iteration_const,
			DEFAULT_SDP_ITERATION_CONST,
			MIN_SDP_ITERATION_CONST,
			MAX_SDP_ITERATION_CONST,
			PGC_USERSET,
			0,
			NULL,
			NULL,
			NULL);
	DefineCustomIntVariable("sdp_min_iterations",
			"Minimum number of iterations",
			"Minimum number of iterations in S-Phase",
			&sdp_min_iterations,
			DEFAULT_SDP_MIN_ITERATIONS,
			MIN_SDP_MIN_ITERATIONS,
			MAX_SDP_MIN_ITERATIONS,
			PGC_USERSET,
			0,
			NULL,
			NULL,
			NULL);
	DefineCustomIntVariable("sdp_max_iterations",
			"Maximum number of iterations",
			"Maximum number of iterations in S-Phase",
			&sdp_max_iterations,
			DEFAULT_SDP_MAX_ITERATIONS,
			MIN_SDP_MAX_ITERATIONS,
			MAX_SDP_MAX_ITERATIONS,
			PGC_USERSET,
			0,
			NULL,
			NULL,
			NULL);
}
