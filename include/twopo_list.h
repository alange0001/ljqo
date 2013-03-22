/*
 * twopo_lists.h
 *
 *   Some functions and structures to manipulate arrays.
 *
 * Copyright (C) 2009-2013, Adriano Lange
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
