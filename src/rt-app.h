/*
 * This file is part of rt-app - https://launchpad.net/rt-app
 * Copyright (C) 2010  Giacomo Bagnoli <g.bagnoli@asidev.com>
 * Copyright (C) 2014  Juri Lelli <juri.lelli@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#ifndef RT_APP_H
#define RT_APP_H

#include <error.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>	/* for memlock */

#include "config.h"
#include "rt-app_types.h"
#include "rt-app_args.h"

#ifdef AQUOSA
#include <aquosa/qres_lib.h>
#endif

#define BUDGET_OVERP 5

void *thread_body(void *arg);

#endif /* RT_APP_H */
