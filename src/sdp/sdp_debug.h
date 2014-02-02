/*------------------------------------------------------------------------
 *
 * sdp_debug.h
 *	  Debug utilities for sdp optimizer.
 *
 * Copyright (c) 2014, PostgreSQL Global Development Group
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

#ifndef SDP_DEBUG_H
#define SDP_DEBUG_H

#include "ljqo.h"

/*#define SDP_DEBUG*/
/*#define SDP_DEBUG2*/
#ifdef SDP_DEBUG
#	define SDP_DEBUG_MSG(format, ...) \
		elog(DEBUG1, "SDP: " format, ##__VA_ARGS__)
#else
#	define SDP_DEBUG_MSG(...)
#endif /* SDP_DEBUG */
#ifdef SDP_DEBUG2
#	define SDP_DEBUG_MSG2(format, ...) \
		elog(DEBUG1, "SDP: " format, ##__VA_ARGS__)
	/*SDP_DEBUG_MSG2_IN: Initialization and Destruction */
#	define SDP_DEBUG_MSG2_IN(...)
	/*SDP_DEBUG_MSG2_MC: Memory Context */
#	define SDP_DEBUG_MSG2_MC(...)
	/*SDP_DEBUG_MSG2_SS: S-Phase sampling */
#	define SDP_DEBUG_MSG2_SS(...)
	/*SDP_DEBUG_MSG2_SR: S-Phase reconstruction */
#	define SDP_DEBUG_MSG2_SR(...)
	/*SDP_DEBUG_MSG2_DM: DP-Phase matrix */
#	define SDP_DEBUG_MSG2_DP(...)
#else
#	define SDP_DEBUG_MSG2(...)
#	define SDP_DEBUG_MSG2_IN(...)
#	define SDP_DEBUG_MSG2_MC(...)
#	define SDP_DEBUG_MSG2_SS(...)
#	define SDP_DEBUG_MSG2_SR(...)
#	define SDP_DEBUG_MSG2_DP(...)
#endif /* SDP_DEBUG2 */

#endif   /* SDP_DEBUG_H */
