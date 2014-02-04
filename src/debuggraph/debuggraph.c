/*
 * debuggraph.c
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

#include "debuggraph.h"
#include <lib/stringinfo.h>

/* ///////////////////////// GRAPH CONSTRUCTION ///////////////////////////// */

#define INITIAL_LIST_SIZE 100

static char* copyString( const char* source );

DebugGraph*
createDebugGraph(const char *name)
{
	DebugGraph* graph;

	Assert(name);

	graph = (DebugGraph*) palloc0(sizeof(DebugGraph));
	graph->name = copyString(name);

	return graph;
}

static void destroyDebugNode(DebugNode* node);
static void destroyDebugEdge(DebugEdge* edge);

void
destroyDebugGraph(DebugGraph* graph)
{
	int i;

	Assert(graph);

	if (graph->name)
		pfree(graph->name);

	if( graph->nodeMemorySpace )
	{
		for( i=0; i<graph->nodeCount; i++ )
		{
			if( graph->nodes[i] )
				destroyDebugNode( graph->nodes[i] );
		}
		pfree( graph->nodes );
	}

	if( graph->edgeMemorySpace )
	{
		for( i=0; i<graph->edgeCount; i++ )
		{
			if( graph->edges[i] )
				destroyDebugEdge( graph->edges[i] );
		}
		pfree( graph->edges );
	}

	pfree(graph);
}

void renameDebugGraph(DebugGraph* graph, const char* new_name)
{
	char *aux = copyString(new_name);

	Assert(graph);

	pfree(graph->name);
	graph->name = aux;
}

static DebugNode* findDebugNodeByInternalName(DebugGraph *graph,
		const char *internal_name);
static DebugNode* createDebugNode( const char* internal_name,
		const char* name );
static void addDebugNode(DebugGraph* graph, DebugNode* node);

DebugNode*
newDebugNode(DebugGraph* graph, const char* internal_name, const char* name)
{
	DebugNode* node;

	Assert(graph && internal_name && name);

	node = findDebugNodeByInternalName(graph, internal_name);
	if( node )
	{
		node->create_node_again = true;
		return node;
	}

	node = createDebugNode( internal_name, name );
	addDebugNode(graph, node);

	return node;
}

DebugNode*
newDebugNodeByPointer(DebugGraph* graph, void* ptr, const char* name)
{
	DebugNode *node;
	StringInfo str = makeStringInfo();

	Assert(ptr);

	appendStringInfo(str, "%p", ptr);
	node = newDebugNode(graph, str->data, name);

	pfree(str->data);
	pfree(str);
	return node;
}

void renameDebugNode(DebugNode* node, const char* new_name)
{
	char *aux = copyString(new_name);

	Assert(node);

	pfree(node->name);
	node->name = aux;
}

static DebugEdge* createDebugEdge( const char* source,
		const char* destination, const char* label );
static void addDebugEdge(DebugGraph* graph, DebugEdge* edge);
static DebugEdge* findDebugEdge(DebugGraph *graph, const char *source,
		const char *destination, const char *label);

void
newDebugEdgeByName(DebugGraph* graph, const char* source,
		const char* destination, const char* label)
{
	DebugEdge* edge;

	Assert(graph && source && destination);

	edge = findDebugEdge(graph, source, destination, label);
	if( edge )
		return;

	edge = createDebugEdge( source, destination, label );
	addDebugEdge( graph, edge );
}

void newDebugEdgeByNode(DebugGraph* graph, DebugNode* source,
		DebugNode* destination, const char* label)
{
	Assert(graph && source && label);
	if (destination)
		newDebugEdgeByName(graph, source->internal_name,
				destination->internal_name, label);
	else
		addDebugNodeAttribute(source, label, "NULL");
}

void
addDebugNodeAttributeArgs(DebugNode* node, const char* name,
		const char* value,...)
{
	char  *str_value;
	va_list va;
	int len = 100;
	int print_return;

	Assert(node && name && value);

	va_start(va, value);
	for(;;)
	{
		str_value = (char*) palloc( sizeof(char) * len );

		print_return = vsnprintf(str_value, len, value, va);

		if( print_return >= 0 && print_return < len - 1 ) {
			break;
		} else {
			pfree(str_value);
			len *= 2;
		}
	}
	va_end(va);

	addDebugNodeAttribute(node, name, str_value);

	pfree(str_value);
}

static void listCheckSpace2(int count, int *space, size_t element_size,
		void **list1, void **list2);

void
addDebugNodeAttribute(DebugNode* node, const char* name, const char* value)
{
	int    index;

	Assert(node && name && value);

	for( index=0; index<node->attributeCount; index++ )
	{
		if( (!strcmp(node->attributeNames[index], name))
		&&	(!strcmp(node->attributeValues[index], value)) )
			return;
	}

	listCheckSpace2(node->attributeCount, &node->attributeMemorySpace,
			sizeof(char*), (void*) &node->attributeNames,
			(void*) &node->attributeValues);

	index = node->attributeCount++;

	node->attributeNames[index] = copyString(name);
	node->attributeValues[index] = copyString(value);
}

static DebugNode*
findDebugNodeByInternalName( DebugGraph *graph, const char *internal_name )
{
	int i;

	Assert(graph && internal_name);

	for (i=0; i<graph->nodeCount; i++)
	{
		if (!strcmp( graph->nodes[i]->internal_name, internal_name ))
			return graph->nodes[i];
	}
	return NULL;
}

static DebugEdge*
findDebugEdge( DebugGraph *graph, const char *source, const char *destination,
		const char *label )
{
	int i;

	Assert(graph && source && destination && label);

	for (i=0; i<graph->edgeCount; i++)
	{
		if( (!strcmp( graph->edges[i]->source, source ))
		&&  (!strcmp( graph->edges[i]->destination, destination ))
		&&  (!strcmp( graph->edges[i]->label, label )))
			return graph->edges[i];
	}
	return NULL;
}

static DebugNode*
createDebugNode(const char* internal_name, const char* name)
{
	DebugNode* node;

	node = (DebugNode*) palloc0( sizeof(DebugNode) );
	Assert(node);

	node->internal_name = copyString( internal_name );
	node->name = copyString( name );
	node->create_node_again = false;

	return node;
}

static void
destroyDebugNode(DebugNode* node)
{
	int i;

	Assert(node);

	if( node->internal_name )
		pfree( node->internal_name );
	if( node->name )
		pfree( node->name );

	if( node->attributeMemorySpace ) {
		for( i=0; i<node->attributeCount; i++ )
		{
			pfree( node->attributeNames[i] );
			pfree( node->attributeValues[i] );
		}
		pfree( node->attributeNames );
		pfree( node->attributeValues );
	}

	pfree(node);
}

static DebugEdge*
createDebugEdge(const char* source, const char* destination,
		const char* label)
{
	DebugEdge* edge;

	edge = (DebugEdge*) palloc( sizeof(DebugEdge) );

	edge->source = copyString( source );
	edge->destination = copyString( destination );
	edge->label = copyString( label );

	return edge;
}

static void
destroyDebugEdge(DebugEdge* edge)
{
	Assert(edge);

	if( edge->source )
		pfree( edge->source );
	if( edge->destination )
		pfree( edge->destination );
	if( edge->label )
		pfree( edge->label );
	pfree( edge );
}

static void listCheckSpace(int count, int *space, size_t element_size,
		void **list);

static void
addDebugNode(DebugGraph* graph, DebugNode* node)
{
	Assert(graph && node);

	listCheckSpace(graph->nodeCount, &graph->nodeMemorySpace,
			sizeof(DebugNode*), (void*) &graph->nodes);

	graph->nodes[ graph->nodeCount++ ] = node;
}

static void
addDebugEdge(DebugGraph* graph, DebugEdge* edge)
{
	Assert(graph && edge);

	listCheckSpace(graph->edgeCount, &graph->edgeMemorySpace,
			sizeof(DebugEdge*), (void*) &graph->edges);

	graph->edges[ graph->edgeCount++ ] = edge;
}

static char*
copyString(const char* source)
{
	char* str;
	int len;

	Assert(source);

	len = strlen( source );
	str = (char*) palloc( sizeof(char) * (len+1) );
	strcpy(str, source);

	return str;
}

static void
listCheckSpace(int count, int *space, size_t element_size, void **list)
{
	Assert(count >= 0);
	Assert(space && *space >= 0);
	Assert(element_size > 0);
	Assert(list);

	if (count >= *space)
	{
		int new_size;
		void *new_list;

		if (*space)
			new_size = *space *2;
		else
			new_size = INITIAL_LIST_SIZE;

		new_list = palloc( element_size * new_size );

		if (*space)
		{
			memcpy(new_list, *list, element_size * count);
			pfree(*list);
		}

		*space = new_size;
		*list = new_list;
	}
}

static void
listCheckSpace2(int count, int *space, size_t element_size,
		void **list1, void **list2)
{
	int space_tmp;

	Assert(space);
	space_tmp = *space;

	listCheckSpace(count,  space,     element_size, list1);
	listCheckSpace(count, &space_tmp, element_size, list2);
}

/* ///////////////////////// GRAPH PRINTING ///////////////////////////////// */

#define DEBUGGRAPH_PRINTF(graph, format,...) \
	elog(DEBUG1, "DebugGraph (%s): " format, (graph)->name, ##__VA_ARGS__)

static const char* htmlSpecialChars(StringInfo str_ret, const char *str);

void printDebugGraph(DebugGraph* graph)
{
	StringInfoData aux1, aux2;
	int i,j;

	Assert(graph);

	initStringInfo(&aux1);
	initStringInfo(&aux2);

	DEBUGGRAPH_PRINTF(graph, "digraph %s {", graph->name);
	DEBUGGRAPH_PRINTF(graph, "\tgraph [fontsize=30 labelloc=\"t\" label=\"\" splines=true overlap=false rankdir = \"LR\"];");
	DEBUGGRAPH_PRINTF(graph, "\tnode  [style = \"filled\" penwidth = 1 fillcolor = \"white\" fontname = \"Courier New\" shape = \"Mrecord\"];");
	DEBUGGRAPH_PRINTF(graph, "\tedge [ penwidth = 2 fontsize = 18 fontcolor = \"black\" ];");
	DEBUGGRAPH_PRINTF(graph, "\tratio = auto;");
	for( i=0; i<graph->nodeCount; i++ )
	{
		DEBUGGRAPH_PRINTF(graph, "\t\"%s\" [ label =<\\", graph->nodes[i]->internal_name);
		DEBUGGRAPH_PRINTF(graph, "\t\t<table border=\"0\" cellborder=\"0\" cellpadding=\"3\" bgcolor=\"white\">\\");
		DEBUGGRAPH_PRINTF(graph, "\t\t\t<tr><td bgcolor=\"black\" align=\"center\" colspan=\"2\"><font color=\"white\">%s</font></td></tr>\\",
				htmlSpecialChars(&aux1, graph->nodes[i]->name));
		for( j=0; j<graph->nodes[i]->attributeCount; j++ )
		{
			DEBUGGRAPH_PRINTF(graph, "\t\t\t<tr><td bgcolor=\"grey\" align=\"left\">%s:</td><td align=\"left\">%s</td></tr>",
					htmlSpecialChars(&aux1, graph->nodes[i]->attributeNames[j]),
					htmlSpecialChars(&aux2, graph->nodes[i]->attributeValues[j]));
		}
		DEBUGGRAPH_PRINTF(graph, "\t\t</table>> ];");
	}
	for( i=0; i<graph->edgeCount; i++ )
	{
		DEBUGGRAPH_PRINTF(graph, "\t\"%s\" -> \"%s\" [ label = \"%s\" ];",
				graph->edges[i]->source,
				graph->edges[i]->destination,
				graph->edges[i]->label);
	}
	DEBUGGRAPH_PRINTF(graph, "}");

	pfree(aux1.data);
	pfree(aux2.data);
}

static const char*
strreplaceone(StringInfo dest, const char *orign,
		const char *find, const char *replace)
{
	bool found;
	int i;

	Assert(dest && orign && find && replace);
	Assert(orign[0] != '\0');
	Assert(find[0] != '\0');

	for (i=0; orign[i] != '\0' && find[i] != '\0' && orign[i] == find[i]; i++);

	if (find[i] == '\0')
	{
		orign += strlen(find);
		appendStringInfo(dest, "%s", replace);
	}

	return orign;
}

static const char*
find_replace_substrings(StringInfo str_ret, const char *str,
		const char *find_replace[][2])
{
	Assert(str_ret && str);

	resetStringInfo(str_ret);

	while (str[0] != '\0')
	{
		const char **i;
		for( i = find_replace[0]; str[0] != '\0' && i[0] != NULL; )
		{
			const char *aux = strreplaceone(str_ret, str, i[0], i[1]);
			if (aux != str)
			{
				str = aux;
				i = find_replace[0];
			}
			else
				i+=2;
		}
		if (str[0] != '\0')
		{
			appendStringInfo(str_ret, "%c", str[0]);
			str++;
		}
	}
	return str_ret->data;
}

static const char*
htmlSpecialChars(StringInfo str_ret, const char *str)
{
	const char *find_replace[][2] = {
			{">", "&gt;"},
			{"<", "&lt;"},
			{"{", "\\{"},
			{"}", "\\}"},
			{NULL, NULL}
		};
	return find_replace_substrings(str_ret, str, find_replace);
}

/* //////////////// Octave Variable Structure Generation //////////////////// */

#define PRINT_OCTAVE_NAME(graph, node, name_attr) \
		DEBUGGRAPH_PRINTF(graph, "%s.(\"%s\").(\""CppAsString(name_attr)\
				"\") = \"%s\";", \
				graph->name, graph->node->internal_name, \
				graph->node->name_attr)
#define PRINT_OCTAVE_ATTRIBUTE(graph, node, index) \
		DEBUGGRAPH_PRINTF(graph, "%s.(\"%s\").(\"%s\") = correct_type(\"%s\");", \
				graph->name, graph->node->internal_name, \
				graph->node->attributeNames[index], \
				octaveString(&aux1, graph->node->attributeValues[index]))
#define PRINT_OCTAVE_EDGE(graph, edge) \
		DEBUGGRAPH_PRINTF(graph, "%s.(\"%s\").(\"%s\") = \"%s\";", \
				graph->name, graph->edge->source, \
				graph->edge->label, \
				graph->edge->destination)
#define PRINT_OCTAVE_UNNAMED_EDGE(graph, edge) \
		DEBUGGRAPH_PRINTF(graph, "%s.(\"%s\").(\"unnamed_refs\")" \
				"{++%s.(\"%s\").(\"unnamed_refs_count\")} = \"%s\";", \
				graph->name, graph->edge->source, \
				graph->name, graph->edge->source, \
				graph->edge->destination)

static const char* octaveString(StringInfo str_ret,
		const char *str);

void printDebugGraphAsOctaveStruct(DebugGraph* graph)
{
	StringInfoData aux1;
	int i,j;

	Assert(graph);

	initStringInfo(&aux1);

	DEBUGGRAPH_PRINTF(graph, "global %s = struct();", graph->name);

	for( i=0; i<graph->nodeCount; i++ )
	{
		DEBUGGRAPH_PRINTF(graph, "%s.(\"%s\") = struct();", graph->name,
				graph->nodes[i]->internal_name);
		PRINT_OCTAVE_NAME(graph, nodes[i], internal_name);
		PRINT_OCTAVE_NAME(graph, nodes[i], name);
		DEBUGGRAPH_PRINTF(graph, "%s.(\"%s\").(\"unnamed_refs_count\") = 0;",
				graph->name, graph->nodes[i]->internal_name);
		DEBUGGRAPH_PRINTF(graph, "%s.(\"%s\").(\"unnamed_refs\") = cell();",
				graph->name, graph->nodes[i]->internal_name);

		for( j=0; j<graph->nodes[i]->attributeCount; j++ )
		{
			PRINT_OCTAVE_ATTRIBUTE(graph, nodes[i], j);
		}
	}
	for( i=0; i<graph->edgeCount; i++ )
	{
		if (graph->edges[i]->label[0] != '\0')
			PRINT_OCTAVE_EDGE(graph, edges[i]);
		else
			PRINT_OCTAVE_UNNAMED_EDGE(graph, edges[i]);
	}

	pfree(aux1.data);
}

static const char*
octaveString(StringInfo str_ret, const char *str)
{
	const char *find_replace[][2] = {
			{"\"", "\\\""},
			{NULL, NULL}
		};
	return find_replace_substrings(str_ret, str, find_replace);
}

/* //////////////// Python Variable Structure Generation /////////////////// */

#define PRINT_PYTHON_NAME(graph, node, name_attr) \
		DEBUGGRAPH_PRINTF(graph, "%s[\"%s\"][\""CppAsString(name_attr)\
				"\"] = \"%s\"", \
				graph->name, graph->node->internal_name, \
				graph->node->name_attr)
#define PRINT_PYTHON_ATTRIBUTE(graph, node, index) \
		DEBUGGRAPH_PRINTF(graph, "%s[\"%s\"][\"%s\"] = \"%s\"", \
				graph->name, graph->node->internal_name, \
				graph->node->attributeNames[index], \
				pythonString(&aux1, graph->node->attributeValues[index]))
#define PRINT_PYTHON_EDGE(graph, edge) \
		DEBUGGRAPH_PRINTF(graph, "%s[\"%s\"][\"%s\"] = \"%s\"", \
				graph->name, graph->edge->source, \
				graph->edge->label, \
				graph->edge->destination)
#define PRINT_PYTHON_UNNAMED_EDGE(graph, edge) \
		DEBUGGRAPH_PRINTF(graph, "%s[\"%s\"][\"unnamed_refs\"].append(" \
				"\"%s\")", \
				graph->name, graph->edge->source, \
				graph->edge->destination)

static const char* pythonString(StringInfo str_ret,
		const char *str);

void printDebugGraphAsPythonDictionary(DebugGraph* graph)
{
	StringInfoData aux1;
	int i,j;

	Assert(graph);

	initStringInfo(&aux1);

	DEBUGGRAPH_PRINTF(graph, "%s = {}", graph->name);

	for( i=0; i<graph->nodeCount; i++ )
	{
		DEBUGGRAPH_PRINTF(graph, "%s[\"%s\"] = {}", graph->name,
				graph->nodes[i]->internal_name);
		PRINT_PYTHON_NAME(graph, nodes[i], internal_name);
		PRINT_PYTHON_NAME(graph, nodes[i], name);
		DEBUGGRAPH_PRINTF(graph, "%s[\"%s\"][\"unnamed_refs\"] = []",
				graph->name, graph->nodes[i]->internal_name);

		for( j=0; j<graph->nodes[i]->attributeCount; j++ )
		{
			PRINT_PYTHON_ATTRIBUTE(graph, nodes[i], j);
		}
	}
	for( i=0; i<graph->edgeCount; i++ )
	{
		if (graph->edges[i]->label[0] != '\0')
			PRINT_PYTHON_EDGE(graph, edges[i]);
		else
			PRINT_PYTHON_UNNAMED_EDGE(graph, edges[i]);
	}

	pfree(aux1.data);
}

static const char*
pythonString(StringInfo str_ret, const char *str)
{
	const char *find_replace[][2] = {
			{"\"", "\\\""},
			{NULL, NULL}
		};
	return find_replace_substrings(str_ret, str, find_replace);
}
