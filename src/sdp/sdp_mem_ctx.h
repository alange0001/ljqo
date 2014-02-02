/*------------------------------------------------------------------------
 *
 * sdp_mem_ctx.h
 *	  Memory context management.
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

#ifndef SDP_MEM_CTX_H
#define SDP_MEM_CTX_H

#include "ljqo.h"
#include <utils/palloc.h>

/**
 * temp_context_type:
 *    The temporary memory contexts are used by initialization and S-phase
 *    to evaluate possible joins using make_join_rel().
 */
typedef struct temp_context_type {
	MemoryContext mycontext;
	MemoryContext oldcxt;
} temp_context_type;

void temporary_context_create(temp_context_type* saved_data);
void temporary_context_enter(temp_context_type* saved_data);
void temporary_context_leave(temp_context_type* saved_data);
void temporary_context_destroy(temp_context_type* saved_data);

#endif   /* SDP_MEM_CTX_H */
