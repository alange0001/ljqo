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

#define INITIAL_LIST_SIZE 100

static void addDebugNode(DebugGraph* graph, DebugNode* node);
static void addDebugEdge(DebugGraph* graph, DebugEdge* edge);
static DebugNode* createDebugNode( const char* internal_name,
		const char* name );
static void destroyDebugNode(DebugNode* node);
static DebugEdge* createDebugEdge( const char* source,
		const char* destination, const char* label );
static void destroyDebugEdge(DebugEdge* edge);
static char* copyString( const char* source );
static void listCheckSpace(int count, int *space, size_t element_size,
		void **list);
static void listCheckSpace2(int count, int *space, size_t element_size,
		void **list1, void **list2);


DebugGraph*
createDebugGraph()
{
	DebugGraph* graph;

	graph = (DebugGraph*) palloc0(sizeof(DebugGraph));

	return graph;
}

void
destroyDebugGraph(DebugGraph* graph)
{
	int i;

	Assert(graph);

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

DebugNode*
newDebugNode(DebugGraph* graph, const char* internal_name, const char* name)
{
	DebugNode* node;

	Assert(graph && internal_name && name);

	node = findDebugNodeByInternalName(graph, internal_name);
	if( node )
		return node;

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

void
newDebugEdgeByName(DebugGraph* graph, const char* source, const char* destination,
		const char* label)
{
	DebugEdge* edge;

	Assert(graph && source && destination);

	edge = findDebugEdge(graph, source, destination, label);
	if( edge )
		return;

	edge = createDebugEdge( source, destination, label );
	addDebugEdge( graph, edge );
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

static const char* htmlSpecialChars(StringInfo str_ret, const char *str);

void
printGraphvizToFile( DebugGraph* graph, FILE* file )
{
	StringInfoData aux;
	int i,j;

	Assert(graph && file);

	initStringInfo(&aux);

	fprintf(file, "digraph g {\n");
	fprintf(file, "\tgraph [fontsize=30 labelloc=\"t\" label=\"\" splines=true overlap=false rankdir = \"LR\"];\n");
	fprintf(file, "\tnode  [style = \"filled\" penwidth = 1 fillcolor = \"white\" fontname = \"Courier New\" shape = \"Mrecord\"];\n");
	fprintf(file, "\tedge [ penwidth = 2 fontsize = 18 fontcolor = \"black\" ];\n");
	fprintf(file, "\tratio = auto;\n");
	for( i=0; i<graph->nodeCount; i++ )
	{
		fprintf(file, "\t\"%s\" [ label =<\\\n", graph->nodes[i]->internal_name);
		fprintf(file, "\t\t<table border=\"0\" cellborder=\"0\" cellpadding=\"3\" bgcolor=\"white\">\\\n");
		fprintf(file, "\t\t\t<tr><td bgcolor=\"black\" align=\"center\" colspan=\"2\"><font color=\"white\">%s</font></td></tr>\\\n",
				htmlSpecialChars(&aux, graph->nodes[i]->name));
		for( j=0; j<graph->nodes[i]->attributeCount; j++ )
		{
			fprintf(file, "\t\t\t<tr><td bgcolor=\"grey\" align=\"left\">%s:</td><td align=\"left\">%s</td></tr>\n",
					htmlSpecialChars(&aux, graph->nodes[i]->attributeNames[j]),
					htmlSpecialChars(&aux, graph->nodes[i]->attributeValues[j]));
		}
		fprintf(file, "\t\t</table>> ];\n");
	}
	for( i=0; i<graph->edgeCount; i++ )
	{
		fprintf(file, "\t\"%s\" -> \"%s\" [ label = \"%s\" ];\n",
				graph->edges[i]->source,
				graph->edges[i]->destination,
				graph->edges[i]->label);
	}
	fprintf(file, "}\n");

	pfree(aux.data);
}

DebugNode*
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

DebugEdge*
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

static DebugNode*
createDebugNode(const char* internal_name, const char* name)
{
	DebugNode* node;

	node = (DebugNode*) palloc0( sizeof(DebugNode) );
	Assert(node);

	node->internal_name = copyString( internal_name );
	node->name = copyString( name );

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
htmlSpecialChars(StringInfo str_ret, const char *str)
{
	const char *find_replace[][2] = {
			{">", "&gt;"},
			{"<", "&lt;"},
			{NULL, NULL}
		};

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