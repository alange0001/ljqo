/*
 * debuggraph_rel.h
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

#ifndef DEBUGGRAPH_REL_H_
#define DEBUGGRAPH_REL_H_

#include "ljqo.h"

#if ENABLE_DEBUGGRAPH

#include <optimizer/paths.h>

extern void printDebugGraphRel(PlannerInfo *root, RelOptInfo *rel,
		const char *name);
#define DEBUGGRAPH_PRINT_REL(root, rel, name) \
		printDebugGraphRel(root, rel, name)

#else /*ENABLE_DEBUGGRAPH*/

#define DEBUGGRAPH_PRINT_REL(...)

#endif /*else ENABLE_DEBUGGRAPH*/

#endif /*DEBUGGRAPH_REL_H_*/
