/*
 * debuggraph.h
 *
 *   DebugGraph structures and functions. They are used to generate directed
 *   graphs for debug purposes.
 *
 * Copyright (C) 2009-2014, Adriano Lange
 *
 * This file is part of LJQO Plugin.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without a written agreement
 * is hereby granted, provided that the above copyright notice and this
 * paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL THE AUTHOR OR DISTRIBUTORS BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
 * LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS
 * DOCUMENTATION, EVEN IF THE AUTHOR OR DISTRIBUTORS HAVE BEEN ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * THE AUTHOR AND DISTRIBUTORS SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE AUTHOR AND DISTRIBUTORS HAS NO OBLIGATIONS TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

#ifndef DEBUGGRAPH_H_
#define DEBUGGRAPH_H_

#include "ljqo.h"

#if ENABLE_DEBUGGRAPH

/* //////////////////////////// STRUCTURES ////////////////////////////////// */

typedef struct DebugNode {
	char* internal_name;
	char* name;
	int attributeCount;
	int attributeMemorySpace;
	char** attributeNames;
	char** attributeValues;
	bool create_node_again; /* true if tried to recreate the node */
} DebugNode;

typedef struct DebugEdge {
	char* source;
	char* destination;
	char* label;
} DebugEdge;

typedef struct DebugGraph {
	char         *name;
	DebugNode**   nodes;
	DebugEdge**   edges;
	int           nodeCount;
	int           nodeMemorySpace;
	int           edgeCount;
	int           edgeMemorySpace;
} DebugGraph;

/* ///////////////////////// GRAPH CONSTRUCTION ///////////////////////////// */

extern DebugGraph* createDebugGraph(const char *name);
extern void destroyDebugGraph(DebugGraph* graph);
extern DebugNode* newDebugNode(DebugGraph* graph, const char* internal_name,
		const char* name);
extern DebugNode* newDebugNodeByPointer(DebugGraph* graph, void* ptr,
		const char* name);
extern void renameDebugNode(DebugNode* node, const char* new_name);
extern void addDebugNodeAttribute(DebugNode* node, const char* name,
		const char* value);
extern void addDebugNodeAttributeArgs(DebugNode* node, const char* name,
		const char* value,...);
extern void newDebugEdgeByName(DebugGraph* graph, const char* source,
		const char* destination, const char* label);
extern void newDebugEdgeByNode(DebugGraph* graph, DebugNode* source,
		DebugNode* destination, const char* label);

/* ///////////////////////// GRAPH PRINTING ///////////////////////////////// */

extern void printDebugGraph(DebugGraph* graph);

#endif /*ENABLE_DEBUGGRAPH*/

#endif /*DEBUGGRAPH_H_*/
