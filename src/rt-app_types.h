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

#ifndef _RTAPP_TYPES_H_
#define _RTAPP_TYPES_H_

#include "config.h"
#include "dl_syscalls.h"
#include <sched.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <sched.h>
#ifdef AQUOSA
#include <aquosa/qres_lib.h>
#endif /* AQUOSA */

#define RTAPP_POLICY_DESCR_LENGTH 16
#define RTAPP_RESOURCE_DESCR_LENGTH 16
#define RTAPP_FTRACE_PATH_LENGTH 256
/* exit codes */

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define EXIT_INV_CONFIG 2
#define EXIT_INV_COMMANDLINE 3

typedef enum policy_t
{
	other = SCHED_OTHER,
	rr = SCHED_RR,
	fifo = SCHED_FIFO
#ifdef AQUOSA
	, aquosa = 1000
#endif
#ifdef DLSCHED
	, deadline = SCHED_DEADLINE
#endif
} policy_t;

typedef enum resource_t
{
	rtapp_mutex = 0,
	rtapp_wait,
	rtapp_signal,
	rtapp_broadcast,
	rtapp_sleep,
	rtapp_run
} resource_t;

struct _rtapp_mutex {
		pthread_mutex_t obj;
		pthread_mutexattr_t attr;
} ;

struct _rtapp_cond {
	pthread_cond_t obj;
	pthread_condattr_t attr;
};

struct _rtapp_signal {
	pthread_cond_t *target;
};

/* Shared resources */
typedef struct _rtapp_resource_t {
	union {
		struct _rtapp_mutex mtx;
		struct _rtapp_cond cond;
		struct _rtapp_signal signal;
	} res;
	int index;
	resource_t type;
	char *name;
} rtapp_resource_t;

typedef struct _rtapp_resource_access_list_t {
	rtapp_resource_t *res;
	struct _rtapp_resource_access_list_t *next;
	struct _rtapp_resource_access_list_t *prev;
} rtapp_resource_access_list_t;

typedef struct _rtapp_tasks_resource_list_t {
	struct timespec usage;
	struct _rtapp_resource_access_list_t *acl;
} rtapp_tasks_resource_list_t;

typedef struct _thread_data_t {
	int ind;
	char *name;
	int lock_pages;
	int duration;
	cpu_set_t *cpuset;
	char *cpuset_str;
	unsigned long wait_before_start;
	struct timespec min_et, max_et;
	struct timespec period, deadline;
	struct timespec main_app_start;
	int loop, sleep;

	FILE *log_handler;
	policy_t sched_policy;
	char sched_policy_descr[RTAPP_POLICY_DESCR_LENGTH];
	int sched_prio;

	rtapp_tasks_resource_list_t *blockages;
	int nblockages;

#ifdef AQUOSA
	int fragment;
	int sid;
	qres_params_t params;
#endif

#ifdef DLSCHED
	struct sched_attr dl_params;
#endif
} thread_data_t;

typedef struct _ftrace_data_t {
	char *debugfs;
	int trace_fd;
	int marker_fd;
} ftrace_data_t;

typedef struct _rtapp_options_t {
	int lock_pages;

	thread_data_t *threads_data;
	int nthreads;

	policy_t policy;
	int duration;
	unsigned long spacing;

	char *logdir;
	char *logbasename;
	int gnuplot;
	int calib_cpu;
	int calib_ns_per_loop;

	rtapp_resource_t *resources;
	int nresources;
	int pi_enabled;

	int ftrace;
	int die_on_dmiss;

#ifdef AQUOSA
	int fragment;
#endif
} rtapp_options_t;

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
