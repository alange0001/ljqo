/*
 * twopo.c
 *
 *   Two Phase Optimization for PostgreSQL
 *
 *   Implementation based on:
 *   [1] Y. E. Ioannidis and Y. Kang, "Randomized algorithms for optimizing
 *       large join queries," SIGMOD Rec., vol. 19, no. 2, pp. 312–321, 1990.
 *
 *   All adaptations and design decisions were made by Adriano Lange.
 *
 *
 *   Copyright (C) 2009, Adriano Lange
 *
 *   This file is part of LJQO Plugin.
 *
 *   LJQO Plugin is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   LJQO Plugin is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with LJQO Plugin.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "twopo.h"

#include <math.h>
#include <optimizer/paths.h>
#include <utils/memutils.h>
#include "twopo_list.h"
#include "opte.h"

//#define TWOPO_DEBUG

#define COST_UNGENERATED 0
#define nodeCost(node) node->rel->cheapest_total_path->total_cost

#define swapValues(type,v1,v2) \
	{ \
		type aux = v1; \
		v1 = v2; \
		v2 = aux; \
	}

#define XOR(a,b) (a || b) && !(a && b)

// defines if twopo will search plans in edgeList or nodeList
bool   twopo_bushy_space               = DEFAULT_TWOPO_BUSHY_SPACE;
// heuristic initial states (see makeInitialState())
bool   twopo_heuristic_states          = DEFAULT_TWOPO_HEURISTIC_STATES;
// number of initial states in Iterative Improvement (II) phase
int    twopo_ii_stop                   = DEFAULT_TWOPO_II_STOP;
// improve states in II phase (r-local minimum)
bool   twopo_ii_improve_states         = DEFAULT_TWOPO_II_IMPROVE_STATES;
// enable Simulated Annealing (SA) phase
bool   twopo_sa_phase                  = DEFAULT_TWOPO_SA_PHASE;
// SA initial temperature: T = X * cost( min_state_from_ii_phase )
double twopo_sa_initial_temperature    = DEFAULT_TWOPO_SA_INITIAL_TEMPERATURE;
// SA temperature reduction: Tnew = X * Told
double twopo_sa_temperature_reduction  = DEFAULT_TWOPO_SA_TEMPERATURE_REDUCTION;
// SA inner loop equilibrium: for( i=0; i < E * Joins ; i++ )
int    twopo_sa_equilibrium            = DEFAULT_TWOPO_SA_EQUILIBRIUM;
#ifdef TWOPO_CACHE_PLANS
// uses cache structure for to minimize optimization time (more memory)
bool   twopo_cache_plans               = DEFAULT_TWOPO_CACHE_PLANS;
// limit cache size (KB)
int    twopo_cache_size                = DEFAULT_TWOPO_CACHE_SIZE;
#endif


/**
 * treeNode:
 *    Optimizer's main struct.
 *    Represent either a base relation or a binary join operation.
 *    It has cache support (see joinNodes()).
 */
typedef struct treeNode {
	RelOptInfo         *rel;
#	ifdef TWOPO_CACHE_PLANS
	List               *parents;
	struct treeNode    *inner_child;
	struct treeNode    *outer_child;
#	endif
} treeNode;

/**
 * tempCtx:
 *    Temporary memory context struct.
 *    Store main informations needed context switch.
 */
typedef struct tempCtx {
	MemoryContext  mycontext;
	MemoryContext  oldcxt;
	int            savelength;
	struct HTAB   *savehash;
} tempCtx;


typedef struct Edge {
	int node[2];
} Edge;

/**
 * twopoEssentials:
 */
typedef struct twopoEssentials {
	PlannerInfo *root;
	treeNode    *nodeList;  // list of initial rels
	int          numNodes;  // number of initial rels
	Edge        *edgeList;
	int          numEdges;
	bool       **adj;       // adjacency matrix
	// Temporary Memory Context
	tempCtx     *ctx;
#	if ENABLE_OPTE
	OPTE_DECLARE( *opte );
	int          opteCreatedNodes;
	int          opteReusedNodes;
#	endif
} twopoEssentials;

/**
 * Element:
 *   Abstraction of search space.
 *   Usado para a construção de planos.
 *
 *   Para left-deep trees, apenas uma lista de relações base:
 *      0, 1, 2, 4 ...
 *
 *   Para bushy-trees, uma lista de operadores de junção:
 *      (0,1), (-1,2), (-2,4)...
 *
 *      Onde x >= 0 representa uma relação base.
 *      E x < 0 o índice de outro operador de junção em forma da função:
 *         idx(x) = -x -1
 *
 *      O exemplo acima representa: Join(Join(Join(0,1),2),4)
 */
typedef union Element {
	int child[2];
	int rel;
} Element;

/**
 * StateType:
 *   Indica qual forma de interpretação será usada em State para elementList.
 */
typedef enum StateType {stLeftDeep, stBushy} StateType;

/**
 * State:
 *   Represents a state in a search space.
 */
typedef struct State {
	twopoEssentials *essentials;   // Informações essenciais para a
	                               // construção de planos
	StateType        type;         // Como será interpretado o elementList
	Element         *elementList;  // Lista de elementos que formam um estado
	                               // (plano)
	int              size;         // Tamanho do estado (elementList)
	Cost             cost;         // Custo estimado do plano
} State;

/**
 * HeuristicStruct:
 *    Estrutura usada para a construção de planos heurísticos baseados nas
 *    junções entre pares de relações base.
 */
typedef struct HeuristicStruct {
	Edge     *edge;
	Cost      cost;
} HeuristicStruct;

#ifdef TWOPO_DEBUG
static void debugPrintState(State *state);
static void debugPrintEdgeList( Edge *edgeList, int joinListSize );
#endif

//////////////////////////////////////////////////////////////////////////////
////////////////////// Memory Context Functions //////////////////////////////

/**
 * createTemporaryContext:
 *    Cria um contexto de memória temporário para a avaliação de planos
 *    durante a fase de seleção e melhoria.
 *
 *    Os dados desse contexto de memória serão salvos em "essentials".
 *    Se essentials->ctx é NULL, então não existe nenhum contexto temporário
 *    ativo.
 */
static void
createTemporaryContext( twopoEssentials *essentials )
{
	tempCtx *ctx;

	Assert( essentials != NULL );
	Assert( essentials->ctx == NULL );

	ctx = (tempCtx*) palloc(sizeof(tempCtx));

	ctx->mycontext = AllocSetContextCreate(CurrentMemoryContext,
									  "TwoPO Memory Context",
									  ALLOCSET_DEFAULT_MINSIZE,
									  ALLOCSET_DEFAULT_INITSIZE,
									  ALLOCSET_DEFAULT_MAXSIZE);
	ctx->oldcxt = MemoryContextSwitchTo(ctx->mycontext);
	ctx->savelength = list_length(essentials->root->join_rel_list);
	ctx->savehash = essentials->root->join_rel_hash;

	essentials->root->join_rel_hash = NULL;

	essentials->ctx = ctx;
}

static void
resetTemporaryContext( twopoEssentials *essentials )
{
#	ifdef TWOPO_CACHE_PLANS
	int i;
#	endif

	Assert( essentials != NULL );

	if ( ! essentials->ctx )
		return;

#	ifdef TWOPO_DEBUG
#	ifdef TWOPO_CACHE_PLANS
	if( twopo_cache_plans ){
		fprintf(stderr, "TwoPO DEBUG: resetting temporary memory context.\n");
		//MemoryContextStats(essentials->ctx->mycontext);
	}
#	endif
#	endif

	essentials->root->join_rel_list =
		list_truncate(essentials->root->join_rel_list,
			essentials->ctx->savelength);
	essentials->root->join_rel_hash = NULL;

#	ifdef TWOPO_CACHE_PLANS
	/*
	 * Cleaning parent nodes in nodeList deleted by MemoryContextResetAndDeleteChildren()
	 */
	for( i=0; i<essentials->numNodes; i++ ){
		essentials->nodeList[i].parents = NULL;
	}
#	endif

	MemoryContextResetAndDeleteChildren(essentials->ctx->mycontext);
}

static void
restoreOldContext( twopoEssentials *essentials )
{
#	ifdef TWOPO_CACHE_PLANS
	int i;
#	endif

	Assert( essentials != NULL );

	if ( ! essentials->ctx )
		return;

#	ifdef TWOPO_DEBUG
	MemoryContextStats(essentials->ctx->mycontext);
#	endif

	essentials->root->join_rel_list =
		list_truncate(essentials->root->join_rel_list,
			essentials->ctx->savelength);
	essentials->root->join_rel_hash = essentials->ctx->savehash;

	MemoryContextSwitchTo(essentials->ctx->oldcxt);
	MemoryContextDelete(essentials->ctx->mycontext);

	pfree(essentials->ctx);
	essentials->ctx = NULL;

#	ifdef TWOPO_CACHE_PLANS
	/*
	 * Cleaning parent nodes in nodeList deleted by MemoryContextDelete()
	 */
	for( i=0; i<essentials->numNodes; i++ ){
		essentials->nodeList[i].parents = NULL;
	}
#	endif
}

static void *
safeContextAlloc(twopoEssentials *essentials, size_t size)
{
	Assert( essentials != NULL );
	Assert( size > 0 );
	// if temporary memory context is active
	if( essentials->ctx )
		// do not allocate memory in temporary context
		return MemoryContextAlloc( essentials->ctx->oldcxt, size );
	else
		return palloc( size );
}

#ifdef TWOPO_CACHE_PLANS
static bool
contextSizeIsExcedded( twopoEssentials *essentials )
{
	memStats stats;

	Assert( essentials != NULL );

	if ( ! essentials->ctx )
		return false;

	getMemoryContextStats( essentials->ctx->mycontext, &stats );

	return (stats.used >= (twopo_cache_size * 1024));
}
#endif


///////////////////////////////////////////////////////////////////////////////
/////////////////////////// State Functions ///////////////////////////////////

/**
 * createState:
 *    Alloca a memória necessária para o estado, atribuindo também seu tipo
 *    e tamanho corretos.
 *
 *    A alocação de memória é feita fora do contexto de memória temporário.
 */
static State *
createState(twopoEssentials *essentials, StateType type)
{
	State *result;

	Assert( essentials != NULL );
	Assert( type == stBushy || type == stLeftDeep );

	result = (State*)safeContextAlloc(essentials, sizeof(State));

	result->type = type;
	if( type == stBushy )
		result->size = essentials->numNodes -1; // (N-1) - list of joins
	else
		result->size = essentials->numNodes;    // N     - list of baserels

	result->elementList = (Element*)safeContextAlloc(essentials,
			sizeof(Element) * result->size);
	result->essentials = essentials;
	result->cost = COST_UNGENERATED;

	return result;
}

/**
 * destroyState:
 */
static void
destroyState(State *state)
{
	if( !state )
		return;

	if( state->elementList ){
		pfree(state->elementList);
	}

	pfree(state);
}

/**
 * copyState:
 *   Copia o estado "input" para "output".
 *   Caso "output" seja NULL, um novo estado é criado.
 *
 *   Retorna "output".
 */
static State *
copyState(State *output, State *input)
{
	Assert( input != NULL );
	Assert( input != output );

	if( !output ) {
		output = createState(input->essentials, input->type);
	}
#	ifdef USE_ASSERT_CHECKING
	else {
		Assert( output->type == input->type );
		Assert( output->size == input->size );
	}
#	endif

	output->cost = input->cost;

	memcpy(output->elementList, input->elementList,
			sizeof(Element) * input->size);

	return output;
}

/**
 * convertIndex:
 *   f(x) = -x -1
 *
 *   A bushy-join is represented as a list of pair of integers (Element[]),
 *   where non-negative ([0,..]) values represent an index of
 *   essentials->nodeList (base relations), and negative values (-1,-2,-3...)
 *   point to another join element of this list using convertIndex(x) function.
 *
 *   This function is used both to encode and decode indexes in an Element list.
 *
 *   f(-1) = 0, f(0) = -1
 *   f(-2) = 1, f(1) = -2
 *
 * @see struct Element
 */
static inline int
convertIndex( int idx )
{
	return -idx -1;
}

static inline bool
isJoinIndex(int index)
{
	return index < 0;
}

/**
 * join_trees:
 *    Parte do algoritmo de Kruskal.
 */
static void
join_trees(int *root1, int *root2, int *weight, int *parent, int *numTrees)
{
	if( weight[*root2]>weight[*root1] ){
		swapValues(int, *root1, *root2 );
	}
	weight[*root1] += weight[*root2];
	parent[*root2] = parent[*root1];
	(*numTrees)--;
}

/**
 * find_root:
 *    Parte do algoritmo de Kruskal.
 */
static inline int
find_root(int idx, int *parent_list)
{
	while( parent_list[idx] != idx )
		idx = parent_list[idx];

	return idx;
}

static void
encodeBushyTree(Element *elementList/*OUT*/,
		Edge *edgeList, int numEdges, int numNodes)
{
	int    i;
	int    numSubtrees; /* number of trees */
	int   *parent; /* parent list. Used for tree detection */
	int   *weight; /* weight list. Used to decide the new root in join
	                      * trees */
	int    elementCount;
	int   *subtrees;

	Assert( elementList != NULL );
	Assert( edgeList != NULL );
	Assert( numEdges > 0 );
	Assert( numNodes > 0 );

	/*
	 * Preparing environment
	 */
	parent =   (int*) palloc(numNodes * sizeof(int));
	weight =   (int*) palloc(numNodes * sizeof(int));
	subtrees = (int*) palloc(numNodes * sizeof(int));
	/*
	 * Initializing values...
	 */
	elementCount = 0;
	numSubtrees = numNodes;
	for (i=0; i < numNodes; i++)
	{
		parent[i]   = i; // todos os vértices são raízes de árvores
		weight[i]   = 1; // todas as árvores têm 1 vértice
		subtrees[i] = i;
	}
	/*
	 * For each edge or while exists more than 1 sub-tree.
	 * Verify whether the edge belong to minimal spanning tree.
	 */
	for (i=0; i < numEdges && numSubtrees > 1; i++)
		// edge-by-edge loop
	{
		int root1, root2;

		/*
		 * Test the root of each relation in selected edge.
		 */
		root1 = find_root(edgeList[i].node[0], parent);
		root2 = find_root(edgeList[i].node[1], parent);
		/*
		 * If both roots is not the same, the edge belong to the minimal
		 * spanning tree. Join the trees in parent[] and the execution plan
		 * in subplan_list[].
		 */
		if (root1 != root2)
		{
			/*
			 * Join two trees. root1 is the root of new tree.
			 */
			join_trees(&root1, &root2, weight, parent, &numSubtrees);

			Assert( elementCount < numNodes -1 );

			elementList[elementCount].child[0] = subtrees[root1];
			elementList[elementCount].child[1] = subtrees[root2];

			subtrees[root1] = convertIndex( elementCount );

			elementCount++;
		}
	}

	pfree(parent);
	pfree(weight);
	pfree(subtrees);
}

static void
encodeLeftDeepTree(Element *elementList/*OUT*/,
		Edge *edgeList, int numEdges, int numNodes)
{
	int    i, j, k;
	int    count       = 0;
	bool  *used_nodes;
	Edge  *edges;

	Assert( elementList != NULL );
	Assert( edgeList != NULL );
	Assert( numEdges > 0 );
	Assert( numNodes > 0 );

	used_nodes = (bool*)palloc0(sizeof(bool)*numNodes);
	edges = (Edge*)palloc(sizeof(Edge)*numEdges);
	memcpy(edges,edgeList,sizeof(Edge)*numEdges);

#	ifdef TWOPO_DEBUG_2
	fprintf(stderr,"TwoPO DEBUG: encodeLeftDeep(): ");
	debugPrintEdgeList(edgeList,numEdges);
#	endif

	elementList[count++].rel = edges[0].node[0];
	used_nodes[edges[0].node[0]] = true;
	elementList[count++].rel = edges[0].node[1];
	used_nodes[edges[0].node[1]] = true;
	// edge-by-edge loop
	for( i=1; i<numEdges; i++ )
	{
		for( j=i; j<numEdges; j++ ){
			int rel0 = edges[j].node[0];
			int rel1 = edges[j].node[1];
			Assert( rel0 >= 0 && rel0 < numNodes );
			Assert( rel1 >= 0 && rel1 < numNodes );

			if( XOR( used_nodes[rel0], used_nodes[rel1]) ){
				if( used_nodes[rel0] )
					rel0 = rel1;
				elementList[count++].rel = rel0;
				used_nodes[rel0] = true;
				break;
			}
		}
		if( count == numNodes ||  j == numEdges ) {
			break;
		} else {
			for( k=j-1; k>=i; k-- ){
				if( !used_nodes[edges[k].node[0]] ||
					!used_nodes[edges[k].node[1]])
				{
					edges[j--] = edges[k];
				}
			}
			i=j;
		}
	}

	pfree(used_nodes);
	pfree(edges);
}


//////////////////////////////////////////////////////////////////////////////
/////////////////////////// Join Function ////////////////////////////////////

/**
 * joinNodes:
 *    Realiza a junção de dois treeNode's e retorna o treeNode resultante.
 */
static treeNode*
joinNodes( twopoEssentials *essentials,
		treeNode *inner_node, treeNode *outer_node )
{
	treeNode    *new_node = NULL;
	RelOptInfo  *jrel;

	Assert( essentials != NULL );
	Assert( inner_node != NULL );
	Assert( outer_node != NULL );
	Assert(!bms_overlap(inner_node->rel->relids,outer_node->rel->relids));

#	ifdef TWOPO_CACHE_PLANS
	if ( inner_node->parents ) {
		ListCell *x;
		treeNode *node;
		foreach( x, inner_node->parents )
		{
			node = lfirst(x);
			if( node->inner_child == outer_node
			 || node->outer_child == outer_node )
			{
				new_node = node;
#				if ENABLE_OPTE
				essentials->opteReusedNodes++;
#				endif
				break;
			}
		}
	}

	if ( ! new_node ) {
#	endif
		jrel = make_join_rel(essentials->root, inner_node->rel,
				outer_node->rel);
		if (jrel) {
#			if ENABLE_OPTE
			essentials->opteCreatedNodes++;
#			endif
			set_cheapest( jrel );
			new_node = (treeNode*)palloc0(sizeof(treeNode));
			new_node->rel = jrel;
#			ifdef TWOPO_CACHE_PLANS
			new_node->inner_child = inner_node;
			new_node->outer_child = outer_node;
			if ( twopo_cache_plans ) {
				inner_node->parents = lcons(new_node,
						inner_node->parents);
				outer_node->parents = lcons(new_node,
						outer_node->parents);
			}
#			endif
		}

#	ifdef TWOPO_CACHE_PLANS
	}
#	endif

	Assert( new_node != NULL );
	return new_node;
}

//////////////////////////////////////////////////////////////////////////////
////////////////////// Building Join Trees from states ///////////////////////

/**
 * buildBushyTree:
 */
static treeNode*
joinSubplans(State *state, treeNode **subplans, int joinIndex)
{
	Assert( state != NULL );
	Assert( state->type == stBushy );
	Assert( subplans != NULL );

	if( !isJoinIndex(joinIndex) ) {
		Assert( joinIndex < state->essentials->numNodes );
		return &(state->essentials->nodeList[joinIndex]);
	} else {
		int idx = convertIndex(joinIndex);
		Assert( idx >= 0 && idx < state->size );
		if( subplans[idx] == NULL ) {
			treeNode *sub0, *sub1;
			sub0 = joinSubplans(state,subplans,
					state->elementList[idx].child[0]);
			sub1 = joinSubplans(state,subplans,
					state->elementList[idx].child[1]);

			subplans[idx] = joinNodes(state->essentials, sub0, sub1);
		}
		return subplans[idx];
	}

}

static treeNode *
buildBushyTree( State *state )
{
	int        i;
	treeNode **subplans;
	treeNode  *result = NULL;

	Assert( state != NULL );
	Assert( state->type == stBushy );

	subplans = (treeNode**)palloc0(sizeof(treeNode*)*state->size);
	for( i=0; i< state->size; i++ ){
		if( subplans[i] == NULL )
			result = joinSubplans(state,subplans,convertIndex(i));
	}
	pfree(subplans);

	return result;
}

static treeNode *
buildLeftDeepTree( State *state )
{
	int        i;
	treeNode  *result = NULL;

	Assert( state != NULL );
	Assert( state->type == stLeftDeep );

	Assert( state->elementList[0].rel >= 0 &&
			state->elementList[0].rel < state->essentials->numNodes );

	result = &(state->essentials->nodeList[ state->elementList[0].rel ]);
	for( i=1; i< state->size; i++ ){
		Assert( state->elementList[i].rel >= 0 &&
				state->elementList[i].rel < state->essentials->numNodes );

		result = joinNodes(
				state->essentials,
				result,
				&(state->essentials->nodeList[ state->elementList[i].rel ]));
	}

	return result;
}

static treeNode *
buildTree( State *state )
{
	treeNode  *result = NULL;

	Assert( state != NULL );
	Assert( state->type == stLeftDeep || state->type == stBushy );

	/*
	 * Controlling temporary memory context size
	 */
#	ifdef TWOPO_CACHE_PLANS
	if( !twopo_cache_plans || contextSizeIsExcedded(state->essentials) )
#	endif
		resetTemporaryContext(state->essentials);

#	ifdef TWOPO_DEBUG_2
	fprintf(stderr,"TwoPO DEBUG: Building State   = ");
	debugPrintState( state );
#	endif

	if( state->type == stBushy )
		result = buildBushyTree( state );
	else
		result = buildLeftDeepTree( state );

	Assert( result != NULL );
	Assert( result->rel != NULL );
	state->cost = nodeCost(result);

	OPTE_CONVERG( state->essentials->opte, state->cost );

	return result;
}

//////////////////////////////////////////////////////////////////////////////
/////////////////////// Initial States Functions /////////////////////////////

static int
heuristicState_1_qsort(const void* x, const void* y)
{
	if (((HeuristicStruct*)x)->cost > ((HeuristicStruct*)y)->cost)
		return 1;
	else if (((HeuristicStruct*)x)->cost < ((HeuristicStruct*)y)->cost)
		return -1;

	return 0;
}

static Edge *
heuristicState_1(twopoEssentials *essentials)
{
	Edge            *edgeList;
	HeuristicStruct *elements;
	int              numEdges;
	int              i;

	Assert( essentials != NULL );
	Assert( essentials->numEdges > 0 );

	numEdges = essentials->numEdges;
	elements = (HeuristicStruct*)palloc(sizeof(HeuristicStruct)*numEdges);

	for( i=0; i<numEdges; i++ ){
		treeNode *node;

		elements[i].edge = &(essentials->edgeList[i]);

		Assert( essentials->edgeList[i].node[0] >= 0 &&
				essentials->edgeList[i].node[0] < essentials->numNodes );
		Assert( essentials->edgeList[i].node[1] >= 0 &&
				essentials->edgeList[i].node[1] < essentials->numNodes );
		node = joinNodes(
			essentials,
			&(essentials->nodeList[ essentials->edgeList[i].node[0] ]),
			&(essentials->nodeList[ essentials->edgeList[i].node[1] ]));

		Assert( node != NULL );
		elements[i].cost = nodeCost(node);
	}
    //sort state using heuristic 1
	qsort(elements,numEdges,sizeof(HeuristicStruct),
			heuristicState_1_qsort);

	edgeList = (Edge*)palloc(sizeof(Edge)*numEdges);
	for( i=0; i<numEdges; i++ ){
		edgeList[i] = *(elements[i].edge);
	}
#	ifdef TWOPO_DEBUG_2
	fprintf(stderr,"TwoPO DEBUG: edgeList (heuristic): ");
	debugPrintEdgeList(edgeList, numEdges);
#	endif

	pfree(elements);

	return edgeList;
}

static Edge *
randomState(twopoEssentials *essentials)
{
	int        i;
	Edge      *edgeList;
	int        numEdges;

	Assert( essentials != NULL );
	Assert( essentials->numEdges > 0 );

	numEdges = essentials->numEdges;
	edgeList = (Edge*)palloc(sizeof(Edge) * numEdges);
	memcpy(edgeList, essentials->edgeList, sizeof(Edge)*numEdges);

	for ( i=0; i<numEdges; i++ ){
		int item = random() % (numEdges - i);
		if( item != i )
			swapValues(Edge, edgeList[i], edgeList[item+i] );
	}
#	ifdef TWOPO_DEBUG_2
	fprintf(stderr,"TwoPO DEBUG: edgeList (rand): ");
	debugPrintEdgeList(edgeList, numEdges);
#	endif

	return edgeList;
}

static State *
makeInitialState(State *output, twopoEssentials *essentials,
		int iteratorIndex)
{
	Edge       *edgeList;
	StateType   type;

	if( twopo_heuristic_states && iteratorIndex == 0 ) { // initial state bias:
		edgeList = heuristicState_1( essentials );
	} else { // random states:
		edgeList = randomState( essentials );
	}

	if( twopo_bushy_space )
		type = stBushy;
	else
		type = stLeftDeep;

	if( !output )
		output = createState( essentials, type );
#	ifdef USE_ASSERT_CHECKING
	else
		Assert( output->type == type );
#	endif

	if( type == stBushy )
		encodeBushyTree( output->elementList, edgeList, essentials->numEdges,
				essentials->numNodes );
	else
		encodeLeftDeepTree(output->elementList, edgeList, essentials->numEdges,
				essentials->numNodes );

	pfree(edgeList);

#	ifdef TWOPO_DEBUG_2
	fprintf(stderr,"TwoPO DEBUG: Initial State    = ");
	debugPrintState(output);
#	endif
	buildTree( output );

	return output;
}

//////////////////////////////////////////////////////////////////////////////
////////////////////// State's Transformation Functions //////////////////////

static twopoList*
baserelsOfSubtree(twopoList *output, int value,
		Element *joinList, int joinListSize)
{
	Assert( value >= 0 || convertIndex(value) < joinListSize );

	if( !output )
		output = listCreate(sizeof(int), joinListSize+1 , NULL);

	if( isJoinIndex(value) ) {
		baserelsOfSubtree(
				output,
				joinList[convertIndex(value)].child[0],
				joinList, joinListSize);
		baserelsOfSubtree(
				output,
				joinList[convertIndex(value)].child[1],
				joinList, joinListSize);
	} else {
		listAdd(output, &value);
	}

	return output;
}

static bool
hasEdgeBetweenSubtrees(State *state, int subIdx1, int subIdx2)
{
	bool       result = false;
	twopoList *list1;
	twopoList *list2;
	size_t     listsize1;
	size_t     listsize2;
	size_t     i,j;

	list1 = baserelsOfSubtree(NULL, subIdx1, state->elementList, state->size);
	list2 = baserelsOfSubtree(NULL, subIdx2, state->elementList, state->size);

	listsize1 = listSize(list1);
	listsize2 = listSize(list2);

	for( i=0; i<listsize1; i++ ){
		int val1;
		listGetElement(&val1,list1,i);
		Assert( val1 >= 0 && val1 < state->essentials->numNodes );

		for( j=0; j<listsize2; j++ ){
			int val2;
			listGetElement(&val2,list2,j);
			Assert( val2 >=0 && val2 < state->essentials->numNodes );

			if( state->essentials->adj[val1][val2] ){
				result = true;
				break;
			}
		}

		if( result )
			break;
	}

	listDestroy(list1);
	listDestroy(list2);

	return result;
}

static State *
neighbordStateBushy(State *output, State *input)
{
	bool     ok = false;
	Element *join;
	int      i;
	int      j;
	int     *father;
	int     *uncle;
	int     *child;
	int     *brother;

	Assert( input != NULL );
	Assert( input->type == stBushy );

	output = copyState(output,input);
#	ifdef TWOPO_DEBUG
	//fprintf(stderr,"TwoPO DEBUG: neighbordStateBushy = ");
	//debugPrintState(output);
#	endif

	while( !ok ) {

		join = &(output->elementList[ random() % output->size ]);
		//fprintf(stderr,"join=(%d,%d)\n", join->edge[0], join->edge[1]);
		father = &(join->child[0]);
		uncle  = &(join->child[1]);

		for( i=0; i<2; i++ ){

			if( isJoinIndex(*father) ){
				Assert( convertIndex(*father) >=0 &&
						convertIndex(*father) < output->size );

				//fprintf(stderr,"father=%d\n", *father);
				//fprintf(stderr,"uncle=%d\n", *uncle);
				child   = &(output->elementList[convertIndex(*father)].
						child[0]);
				brother = &(output->elementList[convertIndex(*father)].
						child[1]);

				for( j=0; j<2; j++ ){
					if( hasEdgeBetweenSubtrees(output,*uncle,*brother) ){
						//fprintf(stderr,"child=%d\n", *child);
						//fprintf(stderr,"brother=%d\n", *brother);
						// swapping uncle <--> child
						swapValues(int, *child, *uncle);
						ok = true;
						break;
					}
					swapValues(int*, child, brother);
				}
			} else {
				swapValues(int*, father, uncle);
			}

			if( ok )
				break;
		}
	}

	return output;
}

static bool
canRelPushedDown(int rel, int pos, State* state)
{
	int i;

	Assert( rel >= 0 && rel < state->essentials->numNodes );
	Assert( pos >= 0 && pos < state->size );

	for( i=pos-1; i>=0; i-- ){
		Assert( state->elementList[i].rel >= 0 &&
				state->elementList[i].rel < state->essentials->numNodes);

		if( state->essentials->adj[rel][ state->elementList[i].rel ] )
			return true;
	}

	return false;
}

static State *
neighbordStateLeftDeep(State *output, State *input, bool *fail)
{
	int idx;

	Assert( input != NULL );
	Assert( input->type == stLeftDeep );

	output = copyState(output,input);

	*fail = true;
	if( input->size == 2 || random()%2 ){ ///// swap method ////
		idx = random()%(input->size -1);
		if(canRelPushedDown(output->elementList[idx+1].rel, idx, output)){
			swapValues(int,
					output->elementList[idx].rel,
					output->elementList[idx+1].rel);
			*fail = false;
		}
	} else { ///////////////////////////////// 3-cycle method //
		idx = random()%(input->size -2);
		if(canRelPushedDown(output->elementList[idx+2].rel, idx, output)){
			swapValues(int,
					output->elementList[idx].rel,
					output->elementList[idx+1].rel);
			swapValues(int,
					output->elementList[idx+1].rel,
					output->elementList[idx+2].rel);
			*fail = false;
		}
	}

	return output;
}

//TODO: faltando left-deep
static State *
neighbordState(State *output, State *input)
{
	Assert( input != NULL );
	Assert( input->type == stBushy || input->type == stLeftDeep );

	if( input->type == stBushy ){
		output = neighbordStateBushy(output, input);
#		ifdef TWOPO_DEBUG_2
		fprintf(stderr,"TwoPO DEBUG: Neighbord State  = ");
		debugPrintState(output);
#		endif
		buildTree( output );
	} else {
		bool fail;
		output = neighbordStateLeftDeep(output, input, &fail);
#		ifdef TWOPO_DEBUG_2
		fprintf(stderr,"TwoPO DEBUG: Neighbord State  = ");
#		endif
		if( !fail ) {
#			ifdef TWOPO_DEBUG_2
			debugPrintState(output);
#			endif
			buildTree( output );
#		ifdef TWOPO_DEBUG_2
		} else {
			fprintf(stderr,"failed\n");
#		endif
		}
	}

	return output;
}

//////////////////////////////////////////////////////////////////////////////
////////////////////// essentials structure construction /////////////////////

/**
 * isDesirableEdge:
 *    Evita produtos cartesianos na construção da lista de arestas.
 */
static bool
isDesirableEdge( PlannerInfo *root, RelOptInfo *rel1, RelOptInfo *rel2 )
{
	return (
	   !bms_overlap(rel1->relids, rel2->relids)
	   && ( have_relevant_joinclause(root, rel1, rel2) ||
			have_join_order_restriction(root, rel1, rel2))
	   );
}

/**
 * createEdges:
 *    Constroi a lista de arestas e a matriz de adjacência da consulta e
 *    guarda em "essentials".
 */
static void
createEdges(twopoEssentials *essentials)
{
	int          i,j;
	bool        *has_adj;
	twopoList   *edgeList;
	treeNode    *nodeList;
	int          numNodes;
	bool       **adj;

	Assert( essentials != NULL );
	Assert( essentials->nodeList != NULL );
	Assert( essentials->numNodes > 1 );

	numNodes = essentials->numNodes;
	nodeList = essentials->nodeList;

	edgeList = listCreate(sizeof(Edge), numNodes -1, NULL);
	/*
	 * Criando matriz de adjacencia
	 */
	adj = (bool**)palloc(sizeof(bool*)*numNodes);
	for( i=0; i<numNodes; i++ ){
		adj[i] = (bool*)palloc0(sizeof(bool)*numNodes);
	}

	has_adj = (bool*)palloc0(sizeof(bool) * numNodes);
	for( i=0; i<numNodes; i++ ) {
		for( j=i; j<numNodes; j++ ) {
			if( isDesirableEdge(essentials->root,
					nodeList[i].rel,nodeList[j].rel) )
			{
				Edge edge;
				edge.node[0] = i;
				edge.node[1] = j;
				listAdd(edgeList, &edge);
				has_adj[i] = true;
				has_adj[j] = true;
				adj[i][j] = true;
				adj[j][i] = true;
			}
		}

		if( ! has_adj[i] ) {
#			ifdef TWOPO_DEBUG
			fprintf(stderr, "TwoPO DEBUG: createEdgeSpace(): "
					"creating cross-products.\n");
#			endif
			for( j=0; j<numNodes; j++ ) {
				Edge edge;
				if( i == j )
					continue;
				edge.node[0] = i;
				edge.node[1] = j;
				listAdd(edgeList, &edge);
				has_adj[i] = true;
				has_adj[j] = true;
				adj[i][j] = true;
				adj[j][i] = true;
			}
		}
	}

	pfree(has_adj);

#	if ENABLE_OPTE
	opte_printf("Number of Edges: %d", listSize(edgeList));
#	endif
#	ifdef TWOPO_DEBUG
	fprintf(stderr,"TwoPO DEBUG: edgeList: ");
	debugPrintEdgeList((Edge*)listElementPos(edgeList,0),listSize(edgeList));
#	endif

	essentials->numEdges = listSize(edgeList);
	essentials->edgeList = (Edge*)listDestroyControlOnly(edgeList);
	essentials->adj      = adj;
}

static treeNode*
buildNodeList( List *initial_rels, int levels_needed )
{
	int          i = 0;
	ListCell    *x;
	RelOptInfo  *rel;
	treeNode    *nodeList = (treeNode*)palloc0(sizeof(treeNode)*levels_needed);

	foreach(x, initial_rels)
	{
		rel = (RelOptInfo *) lfirst(x);
		nodeList[i++].rel = rel;
	}

	return nodeList;
}

static twopoEssentials *
createEssentials( PlannerInfo *root, int levels_needed, List *initial_rels)
{
	twopoEssentials *essentials;

	essentials = (twopoEssentials *) palloc0( sizeof(twopoEssentials) );

	OPTE_GET_BY_PLANNERINFO( essentials->opte, root );
	/*
	 * Construção da lista de relações base (vértices) da consulta.
	 */
	essentials->root = root;
	essentials->numNodes = levels_needed;
	essentials->nodeList  = buildNodeList(initial_rels,levels_needed);

	/*
	 * Construção da lista de arestas que ligam as relações base.
	 * Construção da matriz de adjacência dessas dessas arestas.
	 */
	createEdges( essentials );

	return essentials;
}

/**
 * destroyEssentials:
 */
static void
destroyEssentials( twopoEssentials *essentials )
{
	if( !essentials )
		return;

	if( essentials->nodeList )
		pfree(essentials->nodeList);

	if( essentials->edgeList )
		pfree(essentials->edgeList);

	if( essentials->adj ) {
		int i;
		for( i=0; i<essentials->numNodes; i++ ){
			if( essentials->adj[i] )
				pfree( essentials->adj[i] );
		}
		pfree( essentials->adj );
	}

	pfree(essentials);
}

//////////////////////////////////////////////////////////////////////////////
////////////////////////// Optimization Functions ////////////////////////////

static State *
iiImprove(State *output, State *input)
{
	State       *new_state = NULL;
	Cost         new_cost;
	Cost         cheapest_cost;
	int          i;
	int          local_minimum;

	output = copyState(output, input);
	cheapest_cost = output->cost;

	i = 0;
	local_minimum = input->size;
	while( i < local_minimum ){
		new_state = neighbordState(new_state, output);
		new_cost = new_state->cost;
		if( new_cost < cheapest_cost ){
			output = copyState(output, new_state);
			cheapest_cost = new_cost;
			i=0;
		} else {
			i++;
		}
	}

	destroyState(new_state);

	return output;
}

static State *
iiPhase( twopoEssentials *essentials )
{
	int     i;
	State  *min_state      = NULL;
	State  *new_state      = NULL;
	State  *improved_state = NULL;
	Cost    min_cost       = COST_UNGENERATED;

	Assert( essentials != NULL );

	for( i=0; i<twopo_ii_stop; i++ ){
		if( twopo_ii_improve_states ) {
			new_state      = makeInitialState(new_state, essentials, i);
			improved_state = iiImprove(improved_state, new_state);
		} else {
			improved_state = makeInitialState(improved_state, essentials, i);
		}
		if( !i || min_cost > improved_state->cost ) {
			swapValues( State*, improved_state, min_state );
			min_cost = min_state->cost;
		}
	}

	destroyState(new_state);
	destroyState(improved_state);

	return min_state;
}

inline static bool
saProbability( Cost delta, double temperature )
{
	double e = exp( - delta / temperature );
	int    r = random() % 100;
#	ifndef TWOPO_DEBUG
	return r <= ((int)(100.0*e));
#	else
	if ( r <= ((int)(100.0*e)) ) {
		fprintf(stderr, "TwoPO DEBUG: sa_prob_ok, "
				"temp=%02.2lf, delta=%02.2lf, r=%2d, e=%2d\n",
				temperature, delta, r, (int)(100.0*e));
		return true;
	}
	return false;
#	endif
}

static State *
saPhase( State *initial_state )
{
	int     i;
	double  temperature;
	int     equilibrium;
	int     stage_count           = 0;
	State  *min_state             = NULL;
	State  *improved_state        = NULL;
	State  *new_state             = NULL;
	Cost    min_cost;
	Cost    improved_cost;
	Cost    new_cost              = COST_UNGENERATED;
	Cost    delta_cost;

	Assert( initial_state != NULL );
	Assert( initial_state->cost != COST_UNGENERATED );

	min_state      = copyState(min_state,      initial_state);
	improved_state = copyState(improved_state, initial_state);
	min_cost       =
    improved_cost  = initial_state->cost;
	temperature    = twopo_sa_initial_temperature * (double) min_cost;
	equilibrium    = twopo_sa_equilibrium * initial_state->size;

#	ifdef TWOPO_DEBUG
	fprintf(stderr, "TwoPO DEBUG: SA phase, min_cost=%.2lf\n", min_cost);
#	endif

	while( temperature >= 1 && stage_count < 5 ){ // frozen condition

		for( i=0; i<equilibrium; i++ ){
			new_state = neighbordState(new_state, improved_state);
			new_cost = new_state->cost;
			delta_cost = new_cost - improved_cost;

			if( delta_cost <= 0 || saProbability(delta_cost,temperature) ){

				swapValues(State*,new_state,improved_state);
				improved_cost = new_cost;

				if( improved_cost < min_cost ){
					min_state   = copyState(min_state, improved_state);
					min_cost    = improved_cost;
					stage_count = 0;

#					ifdef TWOPO_DEBUG
					fprintf(stderr, "TwoPO DEBUG: sa_new_min_cost:%.2lf\n",
							min_cost);
#					endif
				}
			}
		}

		stage_count++;
		temperature *= twopo_sa_temperature_reduction; //reducing temperature
	}

	destroyState( improved_state );
	destroyState( new_state );

	return min_state;
}

RelOptInfo *
twopo(PlannerInfo *root, int levels_needed, List *initial_rels)
{
	twopoEssentials  *essentials;
	State            *min_state   = NULL;
	treeNode         *node;

	Assert( levels_needed > 1 );
	Assert( root != NULL );
	Assert( initial_rels != NULL );

	essentials = createEssentials(root, levels_needed, initial_rels);

	if( essentials->numNodes == 2 ) {
		RelOptInfo *result = make_join_rel(
				root,
				essentials->nodeList[0].rel,
				essentials->nodeList[1].rel);
		set_cheapest( result );
		destroyEssentials( essentials );
		return result;
	}

	///////////////// Temporary memory context area ////////////////////////
#	ifdef TWOPO_DEBUG
	fprintf(stderr, "TwoPO DEBUG: inicio do contexto de memoria temporario\n");
#	endif
	createTemporaryContext( essentials );

	////////////// II phase //////////////
	min_state = iiPhase( essentials );

	////////////// SA phase //////////////
	if( twopo_sa_phase ) {
		State *S0 = min_state;
		min_state = saPhase( S0 );
		destroyState( S0 );
	}

	restoreOldContext( essentials );
	//////////////// end of temporary memory context area //////////////////
#	ifdef TWOPO_DEBUG
	fprintf(stderr, "TwoPO DEBUG: fim do contexto de memoria temporario\n");
#	endif

#	if ENABLE_OPTE
	opte_printf("Created Nodes: %d", essentials->opteCreatedNodes);
	opte_printf("Reused Nodes: %d", essentials->opteReusedNodes);
#	endif

	// rebuild best state in correct memory context
	node = buildTree( min_state );

	Assert( node != NULL );
	Assert( node->rel != NULL );

#	ifdef TWOPO_DEBUG
	fprintf(stderr, "TwoPO DEBUG: melhor plano reconstruido: %2lf.\n",
			nodeCost(node));
#	endif

	destroyState(min_state);
	destroyEssentials(essentials);

	return node->rel;
}


/////////////////////////////////////////////////////////////////////////////
///////////////////////// Debug Functions ///////////////////////////////////
#ifdef TWOPO_DEBUG
static void
debugPrintState(State *state)
{
	int i;
	for( i=0; i<state->size; i++ ){
		if( state->type == stBushy ) {
			fprintf(stderr, "(%d,%d) ",
					state->elementList[i].child[0],
					state->elementList[i].child[1]);
		}else {
			if(i)
				fprintf(stderr, ", ");
			fprintf(stderr, "%d",
					state->elementList[i].rel);
		}

	}
	fprintf(stderr,"\n");
}

static void
debugPrintEdgeList( Edge *edgeList, int joinListSize )
{
	int i;
	for ( i=0; i<joinListSize; i++ ){
		fprintf(stderr, "(%d,%d) ",
				edgeList[i].node[0],
				edgeList[i].node[1]);
	}
	fprintf(stderr,"\n");
}

#endif  // TWOPO_DEBUG
