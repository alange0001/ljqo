/*
 * twopo_lists.c
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

#include "twopo_list.h"

#define DEFAULT_BUFFER_SIZE 30

static void
resizeBuffer(twopoList *list, size_t buffersize)
{
	void *new_buffer;

	Assert( list->size < buffersize );

	if( list->context )
		new_buffer = MemoryContextAlloc(list->context,
				buffersize * list->elementsize);
	else
		new_buffer = palloc(buffersize * list->elementsize);

	if( list->size ){
		Assert( list->buffer != NULL );
		memcpy(new_buffer, list->buffer, list->elementsize * list->size);
		pfree(list->buffer);
	}

	list->buffer = new_buffer;
	list->buffersize = buffersize;
}

twopoList*
listCreate(size_t elementsize, size_t initialbuffersize, MemoryContext context)
{
	twopoList *result;

	Assert( elementsize > 0 );

	if( context ){
		result = (twopoList*)MemoryContextAllocZero(context, sizeof(twopoList));
	} else {
		result = (twopoList*)palloc0(sizeof(twopoList));
	}

	result->elementsize = elementsize;
	result->context = context;

	if( initialbuffersize ){
		resizeBuffer( result, initialbuffersize );
	}

	return result;
}

void
listDestroy(twopoList *list)
{
	if( ! list )
		return;

	if( list->buffersize ) {
		Assert( list->buffer != NULL );
		pfree( list->buffer );
	}

	pfree( list );
}

void *
listDestroyControlOnly(twopoList *list)
{
	void *buffer;

	Assert( list != NULL );

	buffer = list->buffer;

	pfree( list );

	return buffer;
}

void
listReset(twopoList *list)
{
	Assert( list != NULL );
	list->size = 0;
}

size_t
listSize(twopoList *list)
{
	Assert( list != NULL );
	return list->size;
}

void *
listElementPos(twopoList *list, size_t index)
{
	Assert( list != NULL );
	Assert( index < list->size );
	return list->buffer + (index * list->buffersize);
}

void
listGetElement(void *dest, twopoList *list, size_t index)
{
	Assert( dest != NULL );
	Assert( list != NULL );
	Assert( index >= 0 && index < list->size );

	memcpy(
			dest,
			list->buffer + (index * list->elementsize),
			list->elementsize );
}

void
listAdd(twopoList *list, void *value_ptr)
{
	Assert( list != NULL );
	Assert( value_ptr != NULL );

	if( ! list->buffersize )
		resizeBuffer(list, DEFAULT_BUFFER_SIZE);
	else if( list->size >= list->buffersize ){
		resizeBuffer(list, list->size * 2);
	}

	memcpy(
			list->buffer + (list->size * list->elementsize),
			value_ptr,
			list->elementsize);

	list->size++;
}

twopoList*
listCopy(twopoList *dest, twopoList *src)
{
	Assert( src != NULL );

	if( !dest ){
		dest = listCreate(src->elementsize, src->size, src->context);
	} else {
		Assert( dest->elementsize == src->elementsize );
		if( dest->buffersize < src->size )
			resizeBuffer(dest,src->size);
	}

	if( src->size ){
		memcpy(dest->buffer, src->buffer, src->size * src->elementsize);
	}

	return dest;
}
