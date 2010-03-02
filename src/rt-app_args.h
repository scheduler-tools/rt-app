/* 
This file is part of rt-app - https://launchpad.net/rt-app
Copyright (C) 2010  Giacomo Bagnoli <g.bagnoli@asidev.com>

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

#ifndef _RTAPP_ARGS_H_
#define _RTAPP_ARGS_H_

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "rt-app_utils.h"
#include "rt-app_types.h"

#define DEFAULT_THREAD_PRIORITY 10

void
usage (const char* msg);

void
parse_thread_args(char *arg, struct thread_data *tdata, policy_t def_policy);

#endif // _RTAPP_ARGS_H_
