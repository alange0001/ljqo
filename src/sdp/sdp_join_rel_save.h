/*------------------------------------------------------------------------
 *
 * sdp_join_rel_save.h
 *	  Utilities for sdp optimizer.
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

#ifndef SDP_JOIN_REL_SAVE_H
#define SDP_JOIN_REL_SAVE_H

#include "ljqo.h"
#include <nodes/relation.h>

/**
 * root_join_rel_save_type:
 *    root->join_rel_list and root->join_rel_hash must be cleared after
 *    sampling paths. The clear process consists on restoring the original
 *    values.
 */
typedef struct root_join_rel_save_type
{
	int length;
	struct HTAB *hash;
} root_join_rel_save_type;

void save_root_join_rel(root_join_rel_save_type *save, PlannerInfo *root);
void clear_root_join_rel(root_join_rel_save_type *save, PlannerInfo *root);
void restore_root_join_rel(root_join_rel_save_type *save, PlannerInfo *root);

#endif   /* SDP_JOIN_REL_SAVE_H */
