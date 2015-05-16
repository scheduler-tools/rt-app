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

#ifndef RTAPP_TYPES_H
#define RTAPP_TYPES_H

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <time.h>

#include "config.h"
#ifdef DLSCHED
#include "dl_syscalls.h"
#endif
#ifdef AQUOSA
#include <aquosa/qres_lib.h>
#endif

#define RTAPP_POLICY_DESCR_LENGTH 16
#define RTAPP_FTRACE_PATH_LENGTH 256

/* exit codes */
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define EXIT_INV_CONFIG 2
#define EXIT_INV_COMMANDLINE 3

typedef enum policy_t {
	other = SCHED_OTHER,
	rr = SCHED_RR,
	fifo = SCHED_FIFO
#ifdef DLSCHED
	, deadline = SCHED_DEADLINE
#endif
#ifdef AQUOSA
	, aquosa = 1000
#endif
} policy_t;

/* Shared resources */
typedef struct _rtapp_resource_t {
	pthread_mutex_t mtx;
	pthread_mutexattr_t mtx_attr;
	int index;
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
	const char *cpuset_str;
	unsigned long wait_before_start;
	struct timespec min_et, max_et;
	struct timespec period, deadline;
	struct timespec main_app_start;

	FILE *log_handler;
	policy_t sched_policy;
	char sched_policy_descr[RTAPP_POLICY_DESCR_LENGTH];
	int sched_prio;

	rtapp_tasks_resource_list_t *blockages;
	int nblockages;

#ifdef DLSCHED
	struct sched_attr dl_params;
#endif

#ifdef AQUOSA
	int fragment;
	int sid;
	qres_params_t params;
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

#endif /* RTAPP_TYPES_H */
