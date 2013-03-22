/*------------------------------------------------------------------------
 *
 * sdp.h
 *	  prototypes and configuration variables for SDP query optimizer
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

#ifndef SDP_H
#define SDP_H

#include "ljqo.h"
#include "nodes/relation.h"

#include <limits.h>

/* routines in sdp_main.c */
extern RelOptInfo *sdp(PlannerInfo *root,
	 int number_of_rels, List *initial_rels);


#ifdef LJQO
#define REGISTER_SDP \
	{ \
		"sdp", \
		"Sampling and Dynamic Programming", \
		sdp, \
		sdp_register, \
		NULL \
	}
extern void sdp_register(void);
#else
/*
 * Configuration variables
 */
extern bool sdp_enabled;
extern int  sdp_threshold;
/*
 * Configuration options:
 *    If you change these, update backend/utils/misc/postgresql.conf.sample
 */
#define DEFAULT_SDP_ENABLED true
#define DEFAULT_SDP_THRESHOLD 10
#define MIN_SDP_THRESHOLD 2
#endif

/*
 * Configuration variables
 */
extern int sdp_iteration_factor;
extern int sdp_min_iterations;
extern int sdp_max_iterations;

/*
 * Configuration options:
 */
#define DEFAULT_SDP_MIN_ITERATIONS    5
#define     MIN_SDP_MIN_ITERATIONS    2
#define     MAX_SDP_MIN_ITERATIONS    INT_MAX
#define DEFAULT_SDP_MAX_ITERATIONS    240
#define     MIN_SDP_MAX_ITERATIONS    10
#define     MAX_SDP_MAX_ITERATIONS    INT_MAX
#define DEFAULT_SDP_ITERATION_FACTOR  1
#define     MIN_SDP_ITERATION_FACTOR  1
#define     MAX_SDP_ITERATION_FACTOR  100

#endif   /* SDP_H */
