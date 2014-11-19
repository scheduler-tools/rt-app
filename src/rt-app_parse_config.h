/* 
This file is part of rt-app - https://launchpad.net/rt-app
Copyright (C) 2010  Giacomo Bagnoli <g.bagnoli@asidev.com>
Copyright (C) 2014  Juri Lelli <juri.lelli@gmail.com>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/ 

#ifndef _RTAPP_PARSE_CONFIG_H
#define _RTAPP_PARSE_CONFIG_H 
/* for CPU_SET macro */
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sched.h>
#include <string.h>
#include "rt-app_types.h"
#include "rt-app_utils.h"
#ifdef DLSCHED
#include "dl_syscalls.h"
#endif
#include <json.h>

#define DEFAULT_THREAD_PRIORITY 10
#define PATH_LENGTH 256

void
parse_config(const char *filename, rtapp_options_t *opts);

#endif // _RTAPP_PARSE_CONFIG_H
