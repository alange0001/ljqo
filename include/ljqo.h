/*
 * ljqo.h
 *
 *   Plugin interface with PostgreSQL and control structure for all
 *   optimizers.
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

#ifndef LJQO_H_
#define LJQO_H_

#include <postgres.h>
#include "ljqo_config.h"

#if PG_VERSION_NUM/100 == 803
#	define POSTGRES_8_3 1
#elif PG_VERSION_NUM/100 == 804
#	define POSTGRES_8_4 1
#else // exception
#	error "PostgreSQL version not supported!"
#endif

#endif /* LJQO_H_ */
