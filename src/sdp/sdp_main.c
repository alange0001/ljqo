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


#include "postgres.h"
#include "sdp.h"
#include "nodes/nodes.h"
#include "optimizer/paths.h"
#include "optimizer/pathnode.h"
#include "optimizer/joininfo.h"
#include "utils/memutils.h"
#include "opte.h"

/*---------------------- CONFIGURATION VARIABLES -------------------------*/
#ifndef LJQO /*if SDP is out ljqo library (in PostgreSQL source code) */
bool sdp_enabled          = DEFAULT_SDP_ENABLED;
int sdp_threshold         = DEFAULT_SDP_THRESHOLD;
#endif
int sdp_iteration_factor  = DEFAULT_SDP_ITERATION_FACTOR;
int sdp_min_iterations    = DEFAULT_SDP_MIN_ITERATIONS;
int sdp_max_iterations    = DEFAULT_SDP_MAX_ITERATIONS;

/*------------------------------- DEBUG ----------------------------------*/
/*#define SDP_DEBUG*/
/*#define SDP_DEBUG2*/
#define SDP_DEBUG3
#ifdef SDP_DEBUG
#	include "nodes/print.h"
#	define SDP_DEBUG_MSG(format, ...) \
		elog(DEBUG1, "SDP: " format, ##__VA_ARGS__)
#else
#	define SDP_DEBUG_MSG(...)
#endif /* SDP_DEBUG */
#ifdef SDP_DEBUG2
#	define SDP_DEBUG_MSG2(format, ...) \
		elog(DEBUG1, "SDP: " format, ##__VA_ARGS__)
#else
#	define SDP_DEBUG_MSG2(...)
#endif /* SDP_DEBUG2 */
#ifdef SDP_DEBUG3
#	define SDP_DEBUG_MSG3(format, ...) \
		elog(DEBUG1, "SDP: " format, ##__VA_ARGS__)
static void debug_print_reloptinfo(const char *name,
                                        PlannerInfo *root, RelOptInfo *rel);
#	define SDP_DEBUG_PRINT_RELOPTINFO(name, root, rel) \
	debug_print_reloptinfo(name, root, rel)
#else
#	define SDP_DEBUG_MSG3(...)
#	define SDP_DEBUG_PRINT_RELOPTINFO(...)
#endif /* SDP_DEBUG3 */

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
	OPTE_DECLARE      ( *opte );
} private_data_type;

/**
 * temp_context_type:
 *    This structure stores data about the PlannerInfo and the memory context.
 *    The functions temporary_context_*() use this structure to enter and
 *    leave to/from a temporary memory context. The temporary memory contexts
 *    are used by initialization and S-phase to evaluate possible joins using
 *    make_join_rel().
 */
typedef struct temp_context_type {
	PlannerInfo*  root;
	int           savelength;
	struct HTAB*  savehash;
	MemoryContext mycontext;
	MemoryContext oldcxt;
} temp_context_type;

/*----------------------------- PROTOTYPES -------------------------------*/
static void initiate_private_data(private_data_type* private_data,
		PlannerInfo *root, int number_of_rels, List *initial_rels);
static void finalize_private_data(private_data_type* private_data);
static RelOptInfo** s_phase(private_data_type* private_data);
static RelOptInfo* dp_phase(private_data_type* private_data,
		RelOptInfo** sequence);


/*==========================  MAIN FUNCTIONS =============================*/

/**
 * sdp: Main optimizer function.
 *    This optimization process has two phases: a Sampling (S-phase) and
 *    a Dynamic Programming (DP-phase).
 */
RelOptInfo*
sdp(PlannerInfo* root, int number_of_rels, List* initial_rels)
{
	private_data_type private_data; /*this variable must be initialized using
	                                  initiate_private_data()*/
	RelOptInfo** s_phase_ret;
	RelOptInfo*  ret;

	OPTE_GET_BY_PLANNERINFO( private_data.opte, root );

	SDP_DEBUG_MSG("> sdp(root=%p, number_of_rels=%d, initial_rels=%p)",
			root, number_of_rels, initial_rels);
	Assert(number_of_rels >= MIN_SDP_THRESHOLD);

	initiate_private_data(&private_data, root, number_of_rels, initial_rels);

	/* ------------ calling the optimization phases: ------------ */
	OPTE_PRINT_TIME( private_data.opte, "before_phase_1" );
	s_phase_ret = s_phase(&private_data);       /* phase 1: Sampling */

	OPTE_PRINT_TIME( private_data.opte, "before_phase_2" );

	ret = dp_phase(&private_data, s_phase_ret); /* phase 2: Dynamic Prog. */

	OPTE_PRINT_TIME( private_data.opte, "after_phase_2" );
	/* ------------ end of the optimization phases ------------ */

	finalize_private_data(&private_data);

	Assert(IsA(ret, RelOptInfo));

	SDP_DEBUG_MSG("< sdp()");
	return ret;
}

/*==================  INITIALIZATION AND FINALIZATION ===================*/
static void create_edge_list(private_data_type* private_data);

static void
initiate_private_data(private_data_type* private_data, PlannerInfo *root,
		int number_of_rels, List *initial_rels)
{
	SDP_DEBUG_MSG2("> initiate_private_data(root=%p, number_of_rels=%d, "
			"initial_rels=%p)", root, number_of_rels, initial_rels);
	Assert(IsA(root, PlannerInfo));
	Assert(number_of_rels > 0);

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

	/* this function call depends on node_list and number_of_rels*/
	create_edge_list(private_data);

	SDP_DEBUG_MSG2("< initiate_private_data()");
	/* at this point the private_data is complete */
}

static void destroy_edge_list(edge_list_type* edge_list);

static void
finalize_private_data(private_data_type* private_data)
{
	SDP_DEBUG_MSG2("> destroy_private_data(private_data=%p)", private_data);

	pfree(private_data->node_list);
	destroy_edge_list(&private_data->edge_list);

	SDP_DEBUG_MSG2("< destroy_private_data()");
}

static inline void create_edge(edge_type* out,
		RelOptInfo* rel1, RelOptInfo* rel2);

static temp_context_type* temporary_context_create(PlannerInfo* root);
static temp_context_type* temporary_context_enter(
		temp_context_type* saved_data, PlannerInfo* root);
static void temporary_context_leave(temp_context_type* saved_data);
static void temporary_context_destroy(temp_context_type* saved_data);

/**
 * is_it_a_possible_join:
 *    Evaluates whether rel1 and rel2 may be joined with make_join_rel().
 */
static bool
is_it_a_possible_join(temp_context_type* saved_context,
		RelOptInfo* rel1, RelOptInfo* rel2)
{
	RelOptInfo* new_rel;

	temporary_context_enter(saved_context, NULL);
	new_rel = make_join_rel(saved_context->root, rel1, rel2);
	temporary_context_leave(saved_context);

	return (new_rel != NULL);
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

	SDP_DEBUG_MSG2("> create_edge_list(private_data=%p)", private_data);

	/* we assume number_of_rels^2 as the maximum size for the query graph */
	max_size = (  (unsigned long int)private_data->number_of_rels
	            * (unsigned long int)private_data->number_of_rels );
	Assert(IsA(root, PlannerInfo));
	Assert(private_data->number_of_rels > 0);
	Assert((unsigned long int)UINT_MAX > max_size);

	edge_list = palloc(sizeof(edge_type) * max_size);
	{
		temp_context_type* save_context;
		int i, j;
		int nrels = private_data->number_of_rels;
		bool* used = palloc0(sizeof(bool) * nrels);

		save_context = temporary_context_create(private_data->root);

		for(i=0; i<nrels; i++)
		{
			RelOptInfo* rel1 = private_data->node_list[i];

			for(j=i+1; j<nrels; j++)
			{
				RelOptInfo* rel2 = private_data->node_list[j];

				if ((have_relevant_joinclause(root, rel1, rel2) ||
				       have_join_order_restriction(root, rel1, rel2))
				    /*&& is_it_a_possible_join(save_context, rel1, rel2)*/)
				{
					used[i] = used[j] = true;

					SDP_DEBUG_MSG2("  create_edge_list() edge_list[%u] = "
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

						if( is_it_a_possible_join(save_context, rel1, rel2) )
						{
							SDP_DEBUG_MSG2("  create_edge_list() "
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

		temporary_context_destroy(save_context);
		pfree(used);
	}

	private_data->edge_list.size = list_size;
	private_data->edge_list.list = edge_list;

	SDP_DEBUG_MSG2("  create_edge_list() edge_list_size=%u",
			private_data->edge_list.size);
	SDP_DEBUG_MSG2("< create_edge_list()");
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
 * destroy_edge_list:
 *    Frees all allocated memory used in edge_list.
 */
static void
destroy_edge_list(edge_list_type* edge_list)
{
	int i;
	int n = edge_list->size;

	SDP_DEBUG_MSG2("> destroy_edge_list(edge_list=%p)", edge_list);
	Assert(edge_list->size > 0);

	for( i=0; i<n; i++ )
	{
		bms_free(edge_list->list[i].relids);
	}
	pfree(edge_list->list);

	SDP_DEBUG_MSG2("< destroy_edge_list()");
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

static List* s_phase_get_a_sample(edge_list_type* edge_list,
		RelOptInfo** cur_rels, PlannerInfo* root, int nrels);

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
	RelOptInfo** ret;
	int nrels = private_data->number_of_rels;

	SDP_DEBUG_MSG("> s_phase(private_data=%p)", private_data);
	Assert(IsA(private_data->root, PlannerInfo)); /* sanity check */
	Assert(nrels > 0);
	Assert(private_data->edge_list.size > 0);

	{
		temp_context_type* save_context;
		Cost          min_cost = 0;
		RelOptInfo*   min_r;
		RelOptInfo**  min_rels = palloc(sizeof(RelOptInfo*) * nrels);
		RelOptInfo**  cur_rels = palloc(sizeof(RelOptInfo*) * nrels);
		PlannerInfo*  root = private_data->root;
		int           loop;
		int           end_loop = nrels * sdp_iteration_factor;

		/* this array of List* isn't used by SDP */
		Assert(root->join_rel_level == NULL);

		/* creating a new memory context */
		save_context = temporary_context_enter(NULL, root);

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

			SDP_DEBUG_MSG2("  s_phase(): loop=%d", loop);

			/* root->join_rel_list must be cleaned before a new sample. */
			root->join_rel_list = list_truncate(
					root->join_rel_list, save_context->savelength);
			/* It's also expected that root->join_rel_hash = NULL. */
			root->join_rel_hash = NULL;

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

			OPTE_CONVERG( private_data->opte,
			              cur_rel->cheapest_total_path->total_cost );

			if( !min_cost ||
				 min_cost > cur_rel->cheapest_total_path->total_cost)
			{
				/* swap cur_rels <--> min_rels */
				RelOptInfo** aux = cur_rels;
				cur_rels = min_rels;
				min_rels = aux;

				SDP_DEBUG_MSG2("  s_phase(): min_cost=%lf --> %lf",
						min_cost, cur_rel->cheapest_total_path->total_cost);

				min_r = cur_rel;
				min_cost = cur_rel->cheapest_total_path->total_cost;
			}
		} /* end for(loop) */

		if( min_cost == 0 )
			elog(ERROR, "SDP: S-phase could not get any valid sample "
			            "for the query");

		SDP_DEBUG_MSG("  s_phase(): min_cost=%lf", min_cost);
		opte_printf("Phase1 Cost = %.2lf", min_cost);
		SDP_DEBUG_PRINT_RELOPTINFO("  s_phase():", root, min_r);

		/* restore old memory context */
		temporary_context_leave(save_context);
		temporary_context_destroy(save_context);

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

		pfree(cur_rels); /* we don't need this array any more */
		ret = min_rels; /* min_rels array will be returned by this function */
	}


	SDP_DEBUG_MSG("< s_phase()");
	return ret;
}

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

	SDP_DEBUG_MSG2("> s_phase_get_a_sample(edge_list(%d), nrels=%d)",
					edge_list->size, nrels);
#	ifdef SDP_DEBUG2
	fprintf(stderr, "DEBUG:  SDP:   s_phase_get_a_sample(): edge_list: ");
	for( i=0; i<edge_list_size; i++ )
		fprintf(stderr, "(%u,%u), ", edge_list->list[i].node1->relid,
				edge_list->list[i].node2->relid);
	fprintf(stderr, "\n");
#	endif

	for( i=0, rel_count=0; i < edge_list_size && rel_count < nrels; )
	{
		Relids intersection;
		int j = i;
		int r;

		SDP_DEBUG_MSG2("  s_phase_get_a_sample(): i=%d, rel_count=%d",
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
		SDP_DEBUG_MSG2("  s_phase_get_a_sample(): i=%d, rel_count=%d, j=%d",
				i, rel_count, j);

		if( j < edge_list_size && i != j )
			/* swap [i] <--> [j] */
			swap_edge(&edge_list->list[i], &edge_list->list[j]);

		SDP_DEBUG_MSG2("  s_phase_get_a_sample(): cur_rel=%p, "
				"edge_list->list[i] = (%u,%u)", cur_rel,
				edge_list->list[i].node1->relid,
				edge_list->list[i].node2->relid);

		/* First join in the sample. Add both [i].node1 and [i].node2 in
		 * cur_rels vector. */
		if( cur_rel == NULL )
		{
			SDP_DEBUG_MSG2("  s_phase_get_a_sample(): edge_list->list[i]: "
					"cur_rel == NULL");
			cur_rel = make_join_rel(root, edge_list->list[i].node1,
			                              edge_list->list[i].node2);
			if( cur_rel )
			{
				SDP_DEBUG_MSG2("  s_phase_get_a_sample(): edge_list->list[i]: "
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

			SDP_DEBUG_MSG2("  s_phase_get_a_sample(): edge_list->list[i]: "
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
				SDP_DEBUG_MSG2("  s_phase_get_a_sample(): edge_list->list[i]: "
						"joining %u", edge_list->list[i].node2->relid);
				join = make_join_rel(root, cur_rel,
						edge_list->list[i].node2);
				if( join )
				{
					SDP_DEBUG_MSG2("  s_phase_get_a_sample(): edge_list->list[i]: "
							"joined %u", edge_list->list[i].node2->relid);
					cur_rel = join;
					cur_rels[rel_count++] = edge_list->list[i].node2;
				}
			}
			else
			if( bms_overlap(cur_rel->relids,
			                edge_list->list[i].node2->relids) )
			{
				SDP_DEBUG_MSG2("  s_phase_get_a_sample(): edge_list->list[i]: "
						"joining %u", edge_list->list[i].node1->relid);
				join = make_join_rel(root, cur_rel,
						edge_list->list[i].node1);
				if( join )
				{
					SDP_DEBUG_MSG2("  s_phase_get_a_sample(): edge_list->list[i]: "
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
			SDP_DEBUG_MSG2("  s_phase_get_a_sample(): edge_list->list[i]: "
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

		SDP_DEBUG_MSG2("  s_phase_get_a_sample(): calling recursively: "
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

	SDP_DEBUG_MSG2("< s_phase_get_a_sample() = %p (length=%d, nrels=%d)",
			ret_list, list_length(ret_list), nrels);
	return ret_list;
}

/*============================= DP-PHASE ================================*/

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
dp_phase(private_data_type* private_data, RelOptInfo** sequence)
{
	RelOptInfo* ret;
	PlannerInfo* root = private_data->root;
	int nrels = private_data->number_of_rels;
	int level;

	RelOptInfo*** matrix = palloc(sizeof(RelOptInfo**) * nrels);

	SDP_DEBUG_MSG("> dp_phase(private_data=%p, sequence=%p)",
			private_data, sequence);
	Assert(IsA(root, PlannerInfo)); /* sanity checks */
	Assert(nrels > 1);

	for( level = 0; level < nrels; level++ )
	{
		int p;

		SDP_DEBUG_MSG2("  dp_phase(): level=%d", level);

		if( level > 0 )
			matrix[level] = palloc(sizeof(RelOptInfo*) * nrels -level);
		else
		{
			matrix[level] = sequence;
			continue;
		}

		for( p=0; p < nrels -level; p++ )
		{
			int i;
			matrix[level][p] = NULL; /* no initial RelOptInfo for [level][p] */

			for( i=0; i<level; i++ )
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
					Assert(IsA(rel1, RelOptInfo));
					Assert(IsA(rel2, RelOptInfo));
					Assert(!bms_overlap(rel1->relids, rel2->relids));

					matrix[level][p] = make_join_rel(root, rel1, rel2);
				}
			}

			if( matrix[level][p] )
			{
				set_cheapest(matrix[level][p]);
				SDP_DEBUG_MSG2("  dp_phase(): matrix[level=%d][p=%d] = %lf",
						level, p,
						matrix[level][p]->cheapest_total_path->total_cost);
			}
		}

	}

	if( !matrix[nrels-1][0] )
		elog(ERROR, "SDP: DP-phase could not generate any complete plan for"
		            "the query");

	Assert(IsA(matrix[nrels-1][0], RelOptInfo));

	ret = matrix[nrels-1][0];
	SDP_DEBUG_MSG("  dp_phase(): best plan found! cost=%lf",
			ret->cheapest_total_path->total_cost);
	SDP_DEBUG_PRINT_RELOPTINFO("  dp_phase():", root, ret);

	for( level = 0; level < nrels; level++ )
		pfree(matrix[level]);
	pfree(matrix);

	SDP_DEBUG_MSG("< dp_phase()");
	return ret;
}

/*============================== UTILITIES =================================*/

static temp_context_type*
temporary_context_create(PlannerInfo* root)
{
	temp_context_type* ret = palloc(sizeof(temp_context_type));

	SDP_DEBUG_MSG2("> temporary_context_create(root=%p)", root);
	Assert(IsA(root, PlannerInfo));

	ret->root = root;
	ret->mycontext = AllocSetContextCreate(CurrentMemoryContext,
									  "SDP Temp",
									  ALLOCSET_DEFAULT_MINSIZE,
									  ALLOCSET_DEFAULT_INITSIZE,
									  ALLOCSET_DEFAULT_MAXSIZE);
	ret->savelength = list_length(root->join_rel_list);
	ret->savehash = root->join_rel_hash;
	root->join_rel_hash = NULL;

	SDP_DEBUG_MSG2("< temporary_context_create()");
	return ret;
}

static temp_context_type*
temporary_context_enter(temp_context_type* saved_data, PlannerInfo* root)
{
	SDP_DEBUG_MSG2("> temporary_context_enter(saved_data=%p, root=%p)",
			saved_data, root);
	Assert( (saved_data && !root) || (!saved_data && root) );

	if( !saved_data )
		saved_data = temporary_context_create(root);

	saved_data->oldcxt = MemoryContextSwitchTo(saved_data->mycontext);

	SDP_DEBUG_MSG2("< temporary_context_enter()");
	return saved_data;
}

static void
temporary_context_leave(temp_context_type* saved_data)
{
	SDP_DEBUG_MSG2("> temporary_context_leave(saved_data=%p)", saved_data);
	Assert(IsA(saved_data->root, PlannerInfo));

	MemoryContextSwitchTo(saved_data->oldcxt);

	SDP_DEBUG_MSG2("< temporary_context_leave()");
}

static void
temporary_context_destroy(temp_context_type* saved_data)
{
	SDP_DEBUG_MSG2("> temporary_context_destroy(saved_data=%p)", saved_data);
	Assert(IsA(saved_data->root, PlannerInfo));

	saved_data->root->join_rel_list = list_truncate(
			saved_data->root->join_rel_list, saved_data->savelength);
	saved_data->root->join_rel_hash = saved_data->savehash;
	saved_data->root = NULL;

	MemoryContextDelete(saved_data->mycontext);

	SDP_DEBUG_MSG2("< temporary_context_destroy()");
}

/*============================== UTILITIES =================================*/
#ifdef SDP_DEBUG3

static char*
debug_get_relids(Relids relids)
{
	char       *ret = palloc(sizeof(char) * 500);
	//TODO: alloc ret appropriately
	Relids		tmprelids;
	int			x;
	bool		first = true;

	ret[0] = '\0';

	tmprelids = bms_copy(relids);
	while ((x = bms_first_member(tmprelids)) >= 0)
	{
		if (first)
		{
			sprintf(&ret[strlen(ret)], "%d", x);
			first = false;
		}
		else
			sprintf(&ret[strlen(ret)], " %d", x);
	}
	bms_free(tmprelids);

	return ret;
}

static char*
debug_get_path(PlannerInfo *root, Path *path)
{
	char       *ret = palloc(sizeof(char) * 2 * 1024 * 1024);
	//TODO: alloc ret appropriately
	const char *ptype;
	bool		join = false;
	Path	   *subpath = NULL;
	int			i;

	switch (nodeTag(path))
	{
		case T_Path:
			ptype = "SeqScan";
			break;
		case T_IndexPath:
			ptype = "IdxScan";
			break;
		case T_BitmapHeapPath:
			ptype = "BitmapHeapScan";
			break;
		case T_BitmapAndPath:
			ptype = "BitmapAndPath";
			break;
		case T_BitmapOrPath:
			ptype = "BitmapOrPath";
			break;
		case T_TidPath:
			ptype = "TidScan";
			break;
		case T_ForeignPath:
			ptype = "ForeignScan";
			break;
		case T_AppendPath:
			ptype = "Append";
			break;
		case T_MergeAppendPath:
			ptype = "MergeAppend";
			break;
		case T_ResultPath:
			ptype = "Result";
			break;
		case T_MaterialPath:
			ptype = "Material";
			subpath = ((MaterialPath *) path)->subpath;
			break;
		case T_UniquePath:
			ptype = "Unique";
			subpath = ((UniquePath *) path)->subpath;
			break;
		case T_NestPath:
			ptype = "NestLoop";
			join = true;
			break;
		case T_MergePath:
			ptype = "MergeJoin";
			join = true;
			break;
		case T_HashPath:
			ptype = "HashJoin";
			join = true;
			break;
		default:
			ptype = "???Path";
			break;
	}

	sprintf(ret, "%s", ptype);

	if (!join && !subpath && path->parent)
	{
		char *tmp = debug_get_relids(path->parent->relids);
		sprintf(&ret[strlen(ret)], "(%s)", tmp);
		pfree(tmp);
	}


	if (join)
	{
		char *tmp1, *tmp2;
		JoinPath   *jp = (JoinPath *) path;

		tmp1 = debug_get_path(root, jp->outerjoinpath);
		tmp2 = debug_get_path(root, jp->innerjoinpath);

		sprintf(&ret[strlen(ret)], "(%s, %s)", tmp1, tmp2);

		pfree(tmp1);
		pfree(tmp2);
	}
	else
	if (subpath)
	{
		char *tmp = debug_get_path(root, subpath);
		sprintf(&ret[strlen(ret)], "(%s)", tmp);
		pfree(tmp);
	}

	return ret;
}

static void
debug_print_reloptinfo(const char *name, PlannerInfo *root, RelOptInfo *rel)
{
	char* str = debug_get_path(root, rel->cheapest_total_path);
	//char* r = nodeToString(rel);
	//char* f = pretty_format_node_dump(r);
	SDP_DEBUG_MSG3("%s cheapest_total_path: %s", name, str);
	//SDP_DEBUG_MSG3("%s", f);
	//pfree(r);
	//pfree(f);
	pfree(str);
}

#endif /* SDP_DEBUG3 */
