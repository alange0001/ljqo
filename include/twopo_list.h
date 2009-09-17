/*
 * twopo_lists.h
 *
 *   Some functions and structures to manipulate arrays.
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

#ifndef TWOPO_LIST_H_
#define TWOPO_LIST_H_

#include <postgres.h>

typedef struct twopoList {
	size_t   size;
	size_t   buffersize;    // depends on elementsize
	size_t   elementsize;
	void    *buffer;        // real size = buffersize * elementsize

	MemoryContext context; // if NULL, use current context
} twopoList;


twopoList *listCreate(size_t elementsize, size_t initialbuffersize,
                     MemoryContext context);
void       listDestroy(twopoList *list);
void      *listDestroyControlOnly(twopoList *list);
void       listReset(twopoList *list);

size_t     listSize(twopoList *list);
void      *listElementPos(twopoList *list, size_t index);
void       listGetElement(void *dest, twopoList *list, size_t index);

void       listAdd(twopoList *list, void *value_ptr);
twopoList *listCopy(twopoList *dest, twopoList *src);

#endif /* TWOPO_LIST_H_ */
