/*------------------------------------------------------------------------
 *
 * sdp_main.c
 *	  SDP: sampling and dynamic programming query optimizer
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
#include "sdp_join_rel_save.h"
#include "sdp_mem_ctx.h"
#include "sdp_debug.h"
#include "opte.h"
#include "debuggraph_rel.h"

#include <nodes/nodes.h>
#include <optimizer/paths.h>
#include <optimizer/pathnode.h>
#include <optimizer/joininfo.h>
#include <utils/memutils.h>
#include <lib/stringinfo.h>

/*---------------------- CONFIGURATION VARIABLES -------------------------*/
#ifndef LJQO /*if SDP is out ljqo library (in PostgreSQL source code) */
bool sdp_enabled          = DEFAULT_SDP_ENABLED;
int sdp_threshold         = DEFAULT_SDP_THRESHOLD;
#endif
int sdp_iteration_slope   = DEFAULT_SDP_ITERATION_SLOPE;
int sdp_iteration_const   = DEFAULT_SDP_ITERATION_CONST;
int sdp_min_iterations    = DEFAULT_SDP_MIN_ITERATIONS;
int sdp_max_iterations    = DEFAULT_SDP_MAX_ITERATIONS;

/*------------------------ MAIN INTERNAL TYPES ---------------------------*/
/**
 * edge_type:
 *    Represents an edge in a query graph. Each edge is a possible join
 *    between two RelOptInfo's (node1 and node2). An array of this type
 *    forms a query graph.
 */
typedef struct edge_type {
	RelOptInfo* node1;
	RelOptInfo* node2;
	Relids      relids;
} edge_type;

/**
 * edge_list_type:
 *    Represents an edge list or a query graph.
 *
 *    The query graph is created by function create_edge_list() at the
 *    optimizer's initialization process.
 */
typedef struct edge_list_type {
	edge_type*    list;
	unsigned int  size;
} edge_list_type;

/**
 * private_data_type:
 *    Main optimizer's data structure.
 */
typedef struct private_data_type {
	PlannerInfo*      root;
	int               number_of_rels;
	List*             initial_rels;
	RelOptInfo**      node_list; /* initial_rels in the form of array */
	edge_list_type    edge_list; /* list of edges in a query graph */
	root_join_rel_save_type save_root_join_rel;
	Cost              s_phase_rel_cost;
	OPTE_DECLARE      ( *opte );
} private_data_type;

/*----------------------------- PROTOTYPES -------------------------------*/
#define cheapest_total(rel) ((rel)->cheapest_total_path->total_cost)

static void initiate_private_data(private_data_type* private_data,
		PlannerInfo *root, int number_of_rels, List *initial_rels);
static void finalize_private_data(private_data_type* private_data);
static RelOptInfo** s_phase(private_data_type* private_data);
static RelOptInfo* dp_phase(private_data_type* private_data,
		RelOptInfo **sequence);
static RelOptInfo* reconstruct_s_phase_rel(PlannerInfo *root,
		RelOptInfo **sequence, int nrels);

/*==========================  MAIN FUNCTIONS =============================*/

/**
 * sdp: Main optimizer function.
 *    This optimization process has two phases: a Sampling (S-phase) and
 *    a Dynamic Programming (DP-phase).
 */
RelOptInfo*
sdp(PlannerInfo* root, int number_of_rels, List* initial_rels)
{
	private_data_type pdata; /*this variable must be initialized using
	                                  initiate_private_data()*/
	RelOptInfo** s_phase_sequence;
	RelOptInfo*  ret;

	OPTE_GET_BY_PLANNERINFO( pdata.opte, root );

	SDP_DEBUG_MSG("> sdp(root=%p, number_of_rels=%d, initial_rels=%p)",
			root, number_of_rels, initial_rels);
	Assert(number_of_rels >= MIN_SDP_THRESHOLD);

	initiate_private_data(&pdata, root, number_of_rels, initial_rels);

	/* ------------ calling the optimization phases: ------------ */
	OPTE_PRINT_TIME( pdata.opte, "before_phase_1" );
	s_phase_sequence = s_phase(&pdata);       /* phase 1: Sampling */

	OPTE_PRINT_TIME( pdata.opte, "before_phase_2" );

	ret = dp_phase(&pdata, s_phase_sequence); /* phase 2: Dynamic Prog. */
	Assert(ret && IsA(ret, RelOptInfo) && ret->cheapest_total_path);

	OPTE_PRINT_TIME( pdata.opte, "after_phase_2" );
	/* ------------ end of the optimization phases ------------ */

	/* Check if s_phase's cheapest_total_path is cheaper than dp_phase's.
	 * This fact should never happen. However, the fuzzy comparisons in
	 * add_path function can permit such cases. */
	if (pdata.s_phase_rel_cost && pdata.s_phase_rel_cost < cheapest_total(ret))
	{
		RelOptInfo *aux;
		elog(WARNING, "sdp's sampling phase generated the cheapest path."
				" Trying to reconstruct it");
		restore_root_join_rel(&pdata.save_root_join_rel, root);
		aux = reconstruct_s_phase_rel(root, s_phase_sequence, number_of_rels);
		if (aux && aux->cheapest_total_path
				&& cheapest_total(aux) < cheapest_total(ret))
			ret = aux;
		else
			elog(WARNING, "s-phase path reconstruction failed");
	}

	/* ------------ finalizations ------------ */
	finalize_private_data(&pdata);
	Assert(s_phase_sequence);
	pfree(s_phase_sequence);

	Assert(ret && IsA(ret, RelOptInfo) && ret->cheapest_total_path);

	SDP_DEBUG_MSG("< sdp()");
	return ret;
}

/*==================  INITIALIZATION AND FINALIZATION ===================*/

/**
 * is_it_a_possible_join:
 *    Evaluates whether rel1 and rel2 may be joined with make_join_rel().
 */
static bool
is_it_a_possible_join(temp_context_type *saved_context, PlannerInfo *root,
		RelOptInfo *rel1, RelOptInfo *rel2)
{
	RelOptInfo* new_rel;
	Assert(saved_context);
	Assert(root && IsA(root, PlannerInfo));
	Assert(rel1 && IsA(rel1, RelOptInfo) && rel1->cheapest_total_path);
	Assert(rel2 && IsA(rel2, RelOptInfo) && rel2->cheapest_total_path);

	temporary_context_enter(saved_context);
	new_rel = make_join_rel(root, rel1, rel2);
	temporary_context_leave(saved_context);

	return (new_rel != NULL);
}

/**
 * create_edge:
 *    This function only sets the values of "out" variable according to
 *    rel1 and rel2.
 */
static inline void
create_edge(edge_type* out, RelOptInfo* rel1, RelOptInfo* rel2)
{
	Assert(IsA(rel1, RelOptInfo));
	Assert(IsA(rel2, RelOptInfo));
	Assert(!bms_overlap(rel1->relids, rel2->relids));

	out->relids = bms_union(rel1->relids, rel2->relids);
	out->node1 = rel1;
	out->node2 = rel2;
}

/**
 * create_edge_list:
 *    Creates a graph for the query based on its join predicates and possible
 *    joins. This query graph is represented here by a list of edges. This
 *    graph may be disconnected. However, it must contain at least one edge
 *    for each relation.
 */
static void
create_edge_list(private_data_type* private_data)
{
	PlannerInfo* root = private_data->root;
	unsigned long int  max_size;
	unsigned int       list_size = 0;
	edge_type*    edge_list;

	SDP_DEBUG_MSG2_IN("> create_edge_list(private_data=%p)", private_data);

	/* we assume number_of_rels^2 as the maximum size for the query graph */
	max_size = (  (unsigned long int)private_data->number_of_rels
	            * (unsigned long int)private_data->number_of_rels );
	Assert(IsA(root, PlannerInfo));
	Assert(private_data->number_of_rels > 0);
	Assert((unsigned long int)UINT_MAX > max_size);

	edge_list = palloc(sizeof(edge_type) * max_size);
	{
		temp_context_type save_context;
		int i, j;
		int nrels = private_data->number_of_rels;
		bool* used = palloc0(sizeof(bool) * nrels);

		temporary_context_create(&save_context);
		clear_root_join_rel(&private_data->save_root_join_rel, root);

		for(i=0; i<nrels; i++)
		{
			RelOptInfo* rel1 = private_data->node_list[i];

			for(j=i+1; j<nrels; j++)
			{
				RelOptInfo* rel2 = private_data->node_list[j];

				if ((have_relevant_joinclause(root, rel1, rel2) ||
				       have_join_order_restriction(root, rel1, rel2))
				    /*&& is_it_a_possible_join(&save_context, rel1, rel2)*/)
				{
					used[i] = used[j] = true;

					SDP_DEBUG_MSG2_IN("  create_edge_list() edge_list[%u] = "
							"(%u,%u)", list_size, rel1->relid, rel2->relid);

					create_edge(&edge_list[list_size++], rel1, rel2);
				}
			}
		}
		/*
		 * Search for not used rels. All rels must be in the query graph.
		 * This is one condition for S-phase. However, the query graph may
		 * be disconnected.
		 */
		for(i=0; i<nrels; i++)
		{
			if( !used[i] )
			{
				RelOptInfo* rel1 = private_data->node_list[i];

				for(j=0; j<nrels; j++)
				{
					if( i != j )
					{
						RelOptInfo* rel2 = private_data->node_list[j];

						if (is_it_a_possible_join(&save_context, root,
								rel1, rel2))
						{
							SDP_DEBUG_MSG2_IN("  create_edge_list() "
									"edge_list[%u] = (%u,%u)",
									list_size, rel1->relid, rel2->relid);

							used[i] = true;
							create_edge(&edge_list[list_size++], rel1, rel2);
						}
					}
				}
				if( ! used[i] )
					elog(ERROR, "SDP: s-phase could not create a correct "
					            "join graph for the query");
			}
		}

		pfree(used);
		restore_root_join_rel(&private_data->save_root_join_rel, root);
		temporary_context_destroy(&save_context);
	}

	private_data->edge_list.size = list_size;
	private_data->edge_list.list = edge_list;

	SDP_DEBUG_MSG2_IN("  create_edge_list() edge_list_size=%u",
			private_data->edge_list.size);
	SDP_DEBUG_MSG2_IN("< create_edge_list()");
}

/**
 * destroy_edge_list:
 *    Frees all allocated memory used in edge_list.
 */
static void
destroy_edge_list(edge_list_type* edge_list)
{
	int i;
	int n = edge_list->size;

	SDP_DEBUG_MSG2_IN("> destroy_edge_list(edge_list=%p)", edge_list);
	Assert(edge_list->size > 0);

	for( i=0; i<n; i++ )
	{
		bms_free(edge_list->list[i].relids);
	}
	pfree(edge_list->list);

	SDP_DEBUG_MSG2_IN("< destroy_edge_list()");
}

static void
initiate_private_data(private_data_type* private_data, PlannerInfo *root,
		int number_of_rels, List *initial_rels)
{
	Assert(private_data);
	Assert(IsA(root, PlannerInfo));
	Assert(number_of_rels > 0);
	SDP_DEBUG_MSG2_IN("> initiate_private_data(root=%p, number_of_rels=%d, "
			"initial_rels=%p)", root, number_of_rels, initial_rels);

	private_data->root = root;
	private_data->number_of_rels = number_of_rels;
	private_data->initial_rels = initial_rels;

	private_data->node_list = palloc(sizeof(RelOptInfo*) * number_of_rels);
	{
		int i = 0;
		ListCell* cell;
		RelOptInfo* rel;

		foreach(cell, initial_rels)
		{
			rel = (RelOptInfo*) lfirst(cell);
			Assert(IsA(rel, RelOptInfo));
			private_data->node_list[i++] = rel;
		}
		Assert(i == number_of_rels);
	}

	/* save initial conditions of root->join_rel_list and join_rel_hash*/
	save_root_join_rel(&private_data->save_root_join_rel, root);

	/* this function call depends on node_list and number_of_rels*/
	create_edge_list(private_data);

	private_data->s_phase_rel_cost = 0;

	SDP_DEBUG_MSG2_IN("< initiate_private_data()");
	/* at this point the private_data is complete */
}

static void
finalize_private_data(private_data_type* private_data)
{
	SDP_DEBUG_MSG2_IN("> destroy_private_data(private_data=%p)", private_data);

	pfree(private_data->node_list);
	destroy_edge_list(&private_data->edge_list);

	SDP_DEBUG_MSG2_IN("< destroy_private_data()");
}

/*============================= S-PHASE =================================*/
/**
 * sample_return_type:
 *    This structure is used only for communication between s_phase and
 *    s_phase_get_a_sample. s_phase_get_a_sample also uses it to get the
 *    results from its recursive calls.
 */
typedef struct sample_return_type
{
	RelOptInfo*  rel;
	RelOptInfo** list_position;
	int          rel_count;
} sample_return_type;

static inline void
swap_edge(edge_type* e1, edge_type* e2)
{
	edge_type aux;
	memcpy(&aux, e1, sizeof(edge_type));
	memcpy(e1, e2, sizeof(edge_type));
	memcpy(e2, &aux, sizeof(edge_type));
}

/**
 * s_phase_get_a_sample:
 *    This is the most complex function in SDP. Its objective is to generate
 *    a RelOptInfo* from a random sequence of joins. Indeed, the sequence is
 *    more important than the RelOptInfo*. The later is used only to compare
 *    its cost with the cost of other generated sequences in s_phase().
 */
static List*
s_phase_get_a_sample(edge_list_type* edge_list, RelOptInfo** cur_rels,
		PlannerInfo* root, int nrels)
{
	sample_return_type* return_item = palloc(sizeof(sample_return_type));
	RelOptInfo* cur_rel = NULL;
	List* ret_list = NULL;
	int i;
	int rel_count;
	int edge_list_size = edge_list->size;

	SDP_DEBUG_MSG2_SS("> s_phase_get_a_sample(edge_list(%d), nrels=%d)",
					edge_list->size, nrels);

	for( i=0, rel_count=0; i < edge_list_size && rel_count < nrels; )
	{
		Relids intersection;
		int j = i;
		int r;

		SDP_DEBUG_MSG2_SS("  s_phase_get_a_sample(): i=%d, rel_count=%d",
				i, rel_count);

		/* This while is only an increment for both i and j. */
		while(j<edge_list_size)
		{
			r = random() % (edge_list_size - j);

			if( r )
				/* swap [j] <--> [j+r] */
				swap_edge(&edge_list->list[j], &edge_list->list[j+r]);

			if( cur_rel == NULL ) /* or rel_count == 0 */
				break; /* we don't need select other r for now */

			if(!bms_overlap(cur_rel->relids,edge_list->list[j].relids))
			{
				j++;
				continue;
			}

			intersection = bms_intersect(cur_rel->relids,
					edge_list->list[j].relids);
			if( bms_equal(intersection, edge_list->list[j].relids) )
			{ /* we don't need edge_list->list[j]. Discarding it.. */
				if( i != j )
					/* swap [i] <--> [j] */
					swap_edge(&edge_list->list[i], &edge_list->list[j]);

				j = ++i;
				bms_free(intersection);
				continue;
			}

			/* [j] doesn't have complete overlap with cur_rel.
			 * This is a good choice. */
			bms_free(intersection);
			break;
		}
		SDP_DEBUG_MSG2_SS("  s_phase_get_a_sample(): i=%d, rel_count=%d, j=%d",
				i, rel_count, j);

		if( j < edge_list_size && i != j )
			/* swap [i] <--> [j] */
			swap_edge(&edge_list->list[i], &edge_list->list[j]);

		SDP_DEBUG_MSG2_SS("  s_phase_get_a_sample(): cur_rel=%p, "
				"edge_list->list[i] = (%u,%u)", cur_rel,
				edge_list->list[i].node1->relid,
				edge_list->list[i].node2->relid);

		/* First join in the sample. Add both [i].node1 and [i].node2 in
		 * cur_rels vector. */
		if( cur_rel == NULL )
		{
			SDP_DEBUG_MSG2_SS("  s_phase_get_a_sample(): edge_list->list[i]: "
					"cur_rel == NULL");
			cur_rel = make_join_rel(root, edge_list->list[i].node1,
			                              edge_list->list[i].node2);
			if( cur_rel )
			{
				SDP_DEBUG_MSG2_SS("  s_phase_get_a_sample(): edge_list->list[i]: "
						"joined (%u,%u)", edge_list->list[i].node1->relid,
						edge_list->list[i].node2->relid);
				cur_rels[rel_count++] = edge_list->list[i].node1;
				cur_rels[rel_count++] = edge_list->list[i].node2;
				set_cheapest(cur_rel);
			}
			i++;
		}
		else
		if( j < edge_list_size ) /* [i] has a partial overlap with cur_rel */
		{
			RelOptInfo* join = NULL;

			SDP_DEBUG_MSG2_SS("  s_phase_get_a_sample(): edge_list->list[i]: "
					"j < edge_list->size");
			Assert(cur_rel && IsA(cur_rel, RelOptInfo));
			Assert(bms_overlap(cur_rel->relids,
			                   edge_list->list[i].relids));
			Assert(!(bms_overlap(cur_rel->relids,
			                     edge_list->list[i].node1->relids)
			      && bms_overlap(cur_rel->relids,
			                     edge_list->list[i].node2->relids)));

			if( bms_overlap(cur_rel->relids,
			                edge_list->list[i].node1->relids) )
			{
				SDP_DEBUG_MSG2_SS("  s_phase_get_a_sample(): edge_list->list[i]: "
						"joining %u", edge_list->list[i].node2->relid);
				join = make_join_rel(root, cur_rel,
						edge_list->list[i].node2);
				if( join )
				{
					SDP_DEBUG_MSG2_SS("  s_phase_get_a_sample(): edge_list->list[i]: "
							"joined %u", edge_list->list[i].node2->relid);
					cur_rel = join;
					cur_rels[rel_count++] = edge_list->list[i].node2;
				}
			}
			else
			if( bms_overlap(cur_rel->relids,
			                edge_list->list[i].node2->relids) )
			{
				SDP_DEBUG_MSG2_SS("  s_phase_get_a_sample(): edge_list->list[i]: "
						"joining %u", edge_list->list[i].node1->relid);
				join = make_join_rel(root, cur_rel,
						edge_list->list[i].node1);
				if( join )
				{
					SDP_DEBUG_MSG2_SS("  s_phase_get_a_sample(): edge_list->list[i]: "
							"joined %u", edge_list->list[i].node1->relid);
					cur_rel = join;
					cur_rels[rel_count++] = edge_list->list[i].node1;
				}
			}
			set_cheapest(cur_rel);
			i++;
		}
		else /* j was exhausted. [i] is not connected to cur_rel */
		{
			SDP_DEBUG_MSG2_SS("  s_phase_get_a_sample(): edge_list->list[i]: "
					"j >= edge_list->size");
			Assert(cur_rel && IsA(cur_rel, RelOptInfo));
			Assert(!bms_overlap(cur_rel->relids, edge_list->list[i].relids));

			/* Put [i] in the end of list and reduce the size of the list.
			 * For this, [i] is swapped with [edge_list_size-1] and
			 * edge_list_size is decremented. */
			if( i != edge_list_size-1 )
				swap_edge(&edge_list->list[i],
						&edge_list->list[edge_list_size-1]);
			edge_list_size--;
		}
	} /* end for i, rel_count */

	Assert(cur_rel);
	Assert(IsA(cur_rel, RelOptInfo));

	/* Deal with non joined edges. Call recursively and merge later. */
	if( rel_count < nrels )
	{
		edge_list_type edge_list2;

		SDP_DEBUG_MSG2_SS("  s_phase_get_a_sample(): calling recursively: "
				"rel_count=%d, edge_list_size=%d", rel_count, edge_list_size);
		Assert( edge_list_size > 0 && edge_list_size < edge_list->size );

		edge_list2.list = &edge_list->list[edge_list_size];
		edge_list2.size = edge_list->size - edge_list_size;

		ret_list = s_phase_get_a_sample(&edge_list2, &cur_rels[rel_count], root,
				nrels - rel_count);

	}

	/* Preparing return: ---------------------------------------*/

	if( ret_list ) /* merge results returned by recursive calls */
	{
		RelOptInfo** cur_rels_aux = palloc(sizeof(RelOptInfo*) * nrels);
		ListCell* c;
		ListCell* prev = NULL;
		int rel_count_after = 0;

		memcpy(cur_rels_aux, cur_rels, sizeof(RelOptInfo*) * nrels);

		while(1) /* for each merged result, ret_list must be reviewed again */
		{
			bool stop = true;

			foreach(c, ret_list)
			{
				sample_return_type* ret_list_item = (sample_return_type*)
				                                    lfirst(c);
				RelOptInfo* cur_rel2 = ret_list_item->rel;
				RelOptInfo* join;

				Assert(IsA(cur_rel2, RelOptInfo));
				Assert(!bms_overlap(cur_rel->relids, cur_rel2->relids));

				join = make_join_rel(root, cur_rel, cur_rel2);
				if( join ) /* join is possible between cur_rel and cur_rel2 */
				{          /* perform a merge of them */
					/* new cur_rel */
					cur_rel = join;
					set_cheapest(cur_rel);

					/* add merged list of RelOptInfos to cur_rels_aux */
					memcpy(&cur_rels_aux[rel_count],
					       ret_list_item->list_position,
					       sizeof(RelOptInfo*) * ret_list_item->rel_count);
					rel_count += ret_list_item->rel_count;

					/* remove ret_list_item from ret_list */
					ret_list = list_delete_cell(ret_list, c, prev);
					pfree(ret_list_item);

					stop = false; /* restart foreach */
					break;
				}
				prev = c;
			} /* foreach */
			if( stop ) /* foreach passed entirely without any merge */
				break;
		} /* while(1) */

		/* Copy the remain not joined items to cur_rels_aux.
		 * It's needed to recalculate the position of each item in the list. */
		foreach(c, ret_list)
		{
			sample_return_type* ret_list_item = (sample_return_type*)
			                                    lfirst(c);

			Assert(rel_count + rel_count_after < nrels);

			memcpy(&cur_rels_aux[rel_count+rel_count_after],
			       ret_list_item->list_position,
			       sizeof(RelOptInfo*) * ret_list_item->rel_count);

			/* note: the new list_position is relative to cur_rels because
			 * cur_rels_aux will be copied back to it */
			ret_list_item->list_position = &cur_rels[rel_count+rel_count_after];
			rel_count_after += ret_list_item->rel_count;
		}

		memcpy(cur_rels, cur_rels_aux, sizeof(RelOptInfo*) * nrels);
		pfree(cur_rels_aux);

#		ifdef USE_ASSERT_CHECKING
		Assert(rel_count + rel_count_after == nrels);
		foreach(c, ret_list)
		{
			sample_return_type* ret_list_item = (sample_return_type*)
												lfirst(c);
			Assert(ret_list_item->list_position > cur_rels);
			Assert(ret_list_item->list_position < &cur_rels[nrels]);
		}
#		endif
	}
	else /* the function wasn't recursively called */
		Assert(rel_count == nrels);

	return_item->rel = cur_rel;
	return_item->list_position = cur_rels;
	return_item->rel_count = rel_count;
	ret_list = lappend(ret_list, return_item);

	SDP_DEBUG_MSG2_SS("< s_phase_get_a_sample() = %p (length=%d, nrels=%d)",
			ret_list, list_length(ret_list), nrels);
	return ret_list;
}

/**
 * s_phase:
 *    Main function of S-Phase. This is a randomized algorithm which randomly
 *    generates a number of possible left-deep trees (samples) for the query
 *    based in its query-graph. This graph is represented by private_data->
 *    edge_list. The cost of each random sample is evaluated, and the cheapest
 *    one is elected for the next phase (DP-phase).
 *
 *    The return of this function is a vector of RelOptInfo*, which consists
 *    on the join order of the cheapest generated sample.
 */
static RelOptInfo**
s_phase(private_data_type* private_data)
{
	int nrels = private_data->number_of_rels;
	RelOptInfo**  ret = NULL;

	SDP_DEBUG_MSG("> s_phase(private_data=%p)", private_data);
	Assert(IsA(private_data->root, PlannerInfo)); /* sanity check */
	Assert(nrels > 0);
	Assert(private_data->edge_list.size > 0);

	{
		temp_context_type save_context;
		Cost          min_cost = 0;
		RelOptInfo*   min_r;
		RelOptInfo**  min_rels = palloc(sizeof(RelOptInfo*) * nrels);
		RelOptInfo**  cur_rels = palloc(sizeof(RelOptInfo*) * nrels);
		PlannerInfo*  root = private_data->root;
		int           loop;
		int           end_loop = nrels * sdp_iteration_slope
		                       + sdp_iteration_const;

		/* this array of List* isn't used by SDP */
		Assert(root->join_rel_level == NULL);

		/* creating a new memory context */
		temporary_context_create(&save_context);
		temporary_context_enter(&save_context);

		/* set the number of samples generated in this phase */
		if( end_loop < sdp_min_iterations )
			end_loop = sdp_min_iterations;
		else if( end_loop > sdp_max_iterations )
			end_loop = sdp_max_iterations;

		/* S-phase's main loop:
		 *    Get end_loop samples from the query and elect the one with
		 *    cheapest cost */
		for( loop=0; loop < end_loop; loop++ )
		{
			List*                returned_list;
			sample_return_type*  returned_item;
			RelOptInfo*          cur_rel;

			SDP_DEBUG_MSG2_SS("  s_phase(): loop=%d", loop);

			/* root->join_rel_list must be cleaned before a new sample. */
			/* It's also expected that root->join_rel_hash = NULL. */
			clear_root_join_rel(&private_data->save_root_join_rel, root);

			/* get a new sample:
			 *   returned_list and cur_rels are outputs from the function call */
			returned_list = s_phase_get_a_sample(&private_data->edge_list,
					cur_rels, root, nrels);

			/* it's expected only one returned_item* in returned_list */
			Assert(list_length(returned_list) == 1);
			returned_item = (sample_return_type*)
			                lfirst(list_head(returned_list));
			Assert(returned_item->list_position == cur_rels);
			cur_rel = returned_item->rel;

			Assert(IsA(cur_rel, RelOptInfo));

			OPTE_CONVERG( private_data->opte, cheapest_total(cur_rel) );

			if( !min_cost ||
				 min_cost > cheapest_total(cur_rel))
			{
				/* swap cur_rels <--> min_rels */
				RelOptInfo** aux = cur_rels;
				cur_rels = min_rels;
				min_rels = aux;

				SDP_DEBUG_MSG2("  s_phase(): loop=%d, min_cost=%lf --> %lf",
						loop, min_cost, cheapest_total(cur_rel));

				min_r = cur_rel;
				min_cost = cheapest_total(cur_rel);
			}
		} /* end for(loop) */

		if( min_cost == 0 )
			elog(ERROR, "SDP: S-phase could not get any valid sample "
			            "for the query");

		SDP_DEBUG_MSG("  s_phase(): min_cost=%lf", min_cost);
		opte_printf("Phase1 Cost = %.2lf", min_cost);

		/* restore old memory context */
		temporary_context_leave(&save_context);
		restore_root_join_rel(&private_data->save_root_join_rel, root);
		temporary_context_destroy(&save_context);

#		ifdef USE_ASSERT_CHECKING
		{
			int i;
			for( i=0; i<nrels; i++ )
			{
				Assert(IsA(min_rels[i], RelOptInfo));
				SDP_DEBUG_MSG2("  s_phase(): min_rels[%d] = %u",
						i, min_rels[i]->relid);
			}
		}
#		endif

		/* we don't need this array any more */
		pfree(cur_rels);
		/* this cost permits a comparison between s-phase and dp-phase */
		private_data->s_phase_rel_cost = min_cost;
		/* min_rels array need to be returned by this function */
		ret = min_rels;
	}

	Assert(ret);
	SDP_DEBUG_MSG("< s_phase()");
	return ret;
}

/*===========================================================================*/
/*=============================== DP-PHASE ==================================*/

/**
 * dp_phase:
 *    Dynamic Programming phase (DP-phase). This phase only evaluates
 *    possible associative combinations between the relations of the
 *    query. The commutative evaluation was previously performed by
 *    S-phase.
 *
 *    Given a sequence of relations:
 *             A, B, C, D, ..., X
 *    find the best way to put parenthesis on these relations, e.g.
 *             (A Join B) Join C ... or A Join (B Join C) ...
 *
 *    The return of this function is the result of SDP optimization process.
 */
static RelOptInfo*
dp_phase(private_data_type *private_data, RelOptInfo **sequence)
{
	RelOptInfo* ret; /*return*/
	PlannerInfo* root = private_data->root;
	int nrels = private_data->number_of_rels;
	int level;
	RelOptInfo*** matrix = palloc(sizeof(RelOptInfo**) * nrels);

	SDP_DEBUG_MSG("> dp_phase(private_data=%p, sequence=%p)",
			private_data, sequence);
	Assert(IsA(root, PlannerInfo)); /* sanity checks */
	Assert(sequence);
	Assert(nrels > 1);

	for( level = 0; level < nrels; level++ )
	{
		int p;

		SDP_DEBUG_MSG2_DP("  dp_phase(): level=%d", level);

		if( level > 0 )
			matrix[level] = palloc(sizeof(RelOptInfo*) * nrels -level);
		else
		{
			matrix[level] = sequence;
			continue;
		}

		root->join_cur_level = level +1;

		for( p=0; p < nrels -level; p++ )
		{
			int i;
			matrix[level][p] = NULL; /* no initial RelOptInfo for [level][p] */

			for( i=level-1; i>=0; i-- )
			{
				RelOptInfo* rel1;
				RelOptInfo* rel2;
				int o_i = level-i-1;
				int o_p = p+i+1;

				Assert(o_i >= 0 && o_i < level);
				Assert(o_p > p && o_p < nrels);

				rel1 = matrix[i][p];
				rel2 = matrix[o_i][o_p];

				if( rel1 && rel2 )
				{
					RelOptInfo *join;
					Assert(IsA(rel1, RelOptInfo));
					Assert(IsA(rel2, RelOptInfo));
					Assert(!bms_overlap(rel1->relids, rel2->relids));

					join = make_join_rel(root, rel1, rel2);
					if( join ) {
						if( ! matrix[level][p] )
							matrix[level][p] = join;
						else
						{
							Assert(matrix[level][p] == join);
						}
					}
				}
			}

			if( matrix[level][p] )
			{
				set_cheapest(matrix[level][p]);
				SDP_DEBUG_MSG2_DP("  dp_phase(): matrix[level=%d][p=%d] = %lf",
						level, p,
						cheapest_total(matrix[level][p]));
			}
		}

	}

	if( !matrix[nrels-1][0] )
		elog(ERROR, "SDP: DP-phase could not generate any complete plan for"
		            "the query");

	Assert(IsA(matrix[nrels-1][0], RelOptInfo));
	ret = matrix[nrels-1][0];

	Assert(ret->cheapest_total_path);
	SDP_DEBUG_MSG("  dp_phase(): best plan found! cost=%lf",
			cheapest_total(ret));

	for( level = 1; level < nrels; level++ ) /* do not free level=0 here */
		pfree(matrix[level]);
	pfree(matrix);

	SDP_DEBUG_MSG("< dp_phase()");
	return ret;
}

/*===========================================================================*/
/*======================= S-Phase path reconstruction =======================*/

/**
 * s_phase_reconstruct:
 *    Reconstruct the RelOptInfo obtained by s-phase. This function is used
 *    when s-phase generates a better RelOptInfo than dp-phase.
 *    Theoretically, db-phase's search space includes the plan generated by
 *    s-phase. However, fuzzy comparisons in add_path() may discard such plans.
 */
static RelOptInfo*
reconstruct_s_phase_rel(PlannerInfo *root, RelOptInfo **sequence, int nrels)
{
	RelOptInfo *ret = NULL;

	Assert(root && IsA(root, PlannerInfo));
	Assert(nrels > 0);
	Assert(sequence);

	SDP_DEBUG_MSG2_SR("> s_phase_reconstruct(root=%p, nrels=%d)",
			root, nrels);

	{
		RelOptInfo **vector;
		int vector_size, i;

		vector = (RelOptInfo**) palloc(sizeof(RelOptInfo*) * nrels);
		memcpy(vector, sequence, sizeof(RelOptInfo*) * nrels);

		for(i = 0, vector_size = nrels; vector_size > 1;)
		{
			int j = i+1;
			RelOptInfo *aux;

			SDP_DEBUG_MSG2_SR("  s_phase_reconstruct(): i=%d, j=%d, nrets=%d", i, j, vector_size);

			aux = make_join_rel(root, vector[i], vector[j]);
			if (aux)
			{
				int k;
				set_cheapest(aux);
				vector[i] = aux;
				for (k=j; k<vector_size-1; k++)
					vector[k] = vector[k+1];
				vector_size--;
				i = 0;
			}
			else
			{
				i++;
			}
		}
		Assert(vector_size == 1);
		ret = vector[0];
		pfree(vector);
	}

	Assert(ret && IsA(ret, RelOptInfo) && ret->cheapest_total_path);
	SDP_DEBUG_MSG("  reconstructed s_phase plan: cost = %lf",
			cheapest_total(ret));
	SDP_DEBUG_MSG2_SR("< s_phase_reconstruct()");
	return ret;
}
