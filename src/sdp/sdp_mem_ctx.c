/*------------------------------------------------------------------------
 *
 * sdp_mem_ctx.c
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

#include "sdp_mem_ctx.h"
#include "sdp_debug.h"
#include <utils/memutils.h>

void
temporary_context_create(temp_context_type* saved_data)
{
	SDP_DEBUG_MSG2_MC("> temporary_context_create()");
	Assert(saved_data);

	saved_data->mycontext = AllocSetContextCreate(CurrentMemoryContext,
									  "SDP Temp",
									  ALLOCSET_DEFAULT_MINSIZE,
									  ALLOCSET_DEFAULT_INITSIZE,
									  ALLOCSET_DEFAULT_MAXSIZE);

	SDP_DEBUG_MSG2_MC("< temporary_context_create()");
}

void
temporary_context_enter(temp_context_type* saved_data)
{
	SDP_DEBUG_MSG2_MC("> temporary_context_enter(saved_data=%p)",
			saved_data);
	Assert(saved_data);

	if (CurrentMemoryContext != saved_data->mycontext)
		saved_data->oldcxt = MemoryContextSwitchTo(saved_data->mycontext);

	SDP_DEBUG_MSG2_MC("< temporary_context_enter()");
}

void
temporary_context_leave(temp_context_type* saved_data)
{
	SDP_DEBUG_MSG2_MC("> temporary_context_leave(saved_data=%p)", saved_data);
	Assert(saved_data);

	MemoryContextSwitchTo(saved_data->oldcxt);

	SDP_DEBUG_MSG2_MC("< temporary_context_leave()");
}

void
temporary_context_destroy(temp_context_type* saved_data)
{
	SDP_DEBUG_MSG2_MC("> temporary_context_destroy(saved_data=%p)", saved_data);
	Assert(saved_data);

	if (CurrentMemoryContext == saved_data->mycontext)
		temporary_context_leave(saved_data);

	MemoryContextDelete(saved_data->mycontext);
	saved_data->mycontext = NULL;

	SDP_DEBUG_MSG2_MC("< temporary_context_destroy()");
}
