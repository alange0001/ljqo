/*------------------------------------------------------------------------
 *
 * sdp_join_rel_save.c
 *	  Utilities for SDP optimizer.
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

#include "sdp_join_rel_save.h"

void
save_root_join_rel(root_join_rel_save_type *save, PlannerInfo *root)
{
	Assert(save);
	Assert(root && IsA(root, PlannerInfo));
	save->length = list_length(root->join_rel_list);
	save->hash = root->join_rel_hash;
}

void
clear_root_join_rel(root_join_rel_save_type *save, PlannerInfo *root)
{
	Assert(save);
	Assert(root && IsA(root, PlannerInfo));
	root->join_rel_list = list_truncate(
			root->join_rel_list, save->length);
	root->join_rel_hash = NULL;
}

void
restore_root_join_rel(root_join_rel_save_type *save, PlannerInfo *root)
{
	Assert(save);
	Assert(root && IsA(root, PlannerInfo));
	root->join_rel_list = list_truncate(
				root->join_rel_list, save->length);
	root->join_rel_hash = save->hash;
}
