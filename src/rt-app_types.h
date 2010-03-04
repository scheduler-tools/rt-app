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

#ifndef _RTAPP_TYPES_H_
#define _RTAPP_TYPES_H_

#include <sched.h>
#include <time.h>
#include <stdio.h>
#include <sched.h>
#include "config.h"
#ifdef AQUOSA
#include <aquosa/qres_lib.h>
#endif /* AQUOSA */

#ifdef DEADLINE
#include "dl_syscalls.h"
#endif

typedef enum policy_t 
{ 
	other = SCHED_OTHER, 
	rr = SCHED_RR, 
	fifo = SCHED_FIFO
#ifdef AQUOSA
	, aquosa = 1000 
#endif
#ifdef DEADLINE
	, deadline = SCHED_DEADLINE
#endif
} policy_t;

struct thread_data {
	int ind;
	int lock_pages;
	int duration;
	cpu_set_t *cpuset;
	char *cpuset_str;
	unsigned long wait_before_start;
	struct timespec min_et, max_et;
	struct timespec period, deadline;
	struct timespec main_app_start;
    
	FILE *log_handler;
	policy_t sched_policy;
	char sched_policy_descr[16];
	int sched_prio;
	
#ifdef AQUOSA
	int fragment;
	int sid;
	qres_params_t params;
#endif

#ifdef DEADLINE
	struct sched_param_ex dl_params;
#endif
};

typedef struct _timing_point_t {
	int ind;
	unsigned long period;
	unsigned long min_et;
	unsigned long max_et;
	unsigned long rel_start_time;
	unsigned long abs_start_time;
	unsigned long end_time;
	unsigned long deadline;
	unsigned long duration;
	long slack;
#ifdef AQUOSA
	qres_time_t budget;
	qres_time_t used_budget;
#endif
} timing_point_t;

#endif // _RTAPP_TYPES_H_ 
