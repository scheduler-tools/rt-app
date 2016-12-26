/*
This file is part of rt-app - https://launchpad.net/rt-app
Copyright (C) 2010  Giacomo Bagnoli <g.bagnoli@asidev.com>
Copyright (C) 2014  Juri Lelli <juri.lelli@gmail.com>
Copyright (C) 2014  Vincent Guittot <vincent.guittot@linaro.org>

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
#ifdef DLSCHED
	, deadline = SCHED_DEADLINE
#endif
} policy_t;

typedef enum resource_t
{
	rtapp_unknown = 0,
	rtapp_mutex,
	rtapp_wait,
	rtapp_signal,
	rtapp_broadcast,
	rtapp_sleep,
	rtapp_run,
	rtapp_sig_and_wait,
	rtapp_lock,
	rtapp_unlock,
	rtapp_timer,
	rtapp_suspend,
	rtapp_resume,
	rtapp_mem,
	rtapp_iorun,
	rtapp_runtime,
	rtapp_yield
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

struct _rtapp_timer {
	struct timespec t_next;
	int init;
	int relative;
};

struct _rtapp_iomem_buf {
	char *ptr;
	int size;
};

struct _rtapp_iodev {
	int fd;
};

/* Shared resources */
typedef struct _rtapp_resource_t {
	union {
		struct _rtapp_mutex mtx;
		struct _rtapp_cond cond;
		struct _rtapp_signal signal;
		struct _rtapp_timer timer;
		struct _rtapp_iomem_buf buf;
		struct _rtapp_iodev dev;
	} res;
	int index;
	resource_t type;
	char *name;
} rtapp_resource_t;

typedef struct _event_data_t {
	resource_t type;
	int res;
	int dep;
	int duration;
	int count;
} event_data_t;

typedef struct _phase_data_t {
	int loop;
	event_data_t *events;
	int nbevents;
} phase_data_t;

typedef struct _thread_data_t {
	int ind;
	char *name;
	int lock_pages;
	int duration;
	rtapp_resource_t **resources;
	cpu_set_t *cpuset;
	char *cpuset_str;

	unsigned long runtime, deadline, period;

	int loop;
	int nphases;
	phase_data_t *phases;

	struct timespec main_app_start;

	FILE *log_handler;
	policy_t sched_policy;
	char sched_policy_descr[RTAPP_POLICY_DESCR_LENGTH];
	int sched_prio;

	unsigned long delay;

#ifdef DLSCHED
	struct sched_attr dl_params;
#endif
} thread_data_t;

typedef struct _ftrace_data_t {
	char *debugfs;
	int trace_fd;
	int marker_fd;
} ftrace_data_t;

typedef struct _log_data_t {
	unsigned long perf;
	unsigned long duration;
	unsigned long wu_latency;
	unsigned long c_duration;
	unsigned long c_period;
	long slack;
} log_data_t;

typedef struct _rtapp_options_t {
	int lock_pages;

	thread_data_t *threads_data;
	int nthreads;

	policy_t policy;
	int duration;

	char *logdir;
	char *logbasename;
	int logsize;
	int gnuplot;
	int calib_cpu;
	int calib_ns_per_loop;

	rtapp_resource_t *resources;
	int nresources;
	int pi_enabled;

	int ftrace;
	int die_on_dmiss;
	int mem_buffer_size;
	char *io_device;

	int cumulative_slack;
} rtapp_options_t;

typedef struct _timing_point_t {
	int ind;
	unsigned long perf;
	unsigned long duration;
	unsigned long period;
	unsigned long c_duration;
	unsigned long c_period;
	unsigned long wu_latency;
	long slack;
	__u64 start_time;
	__u64 end_time;
	__u64 rel_start_time;
} timing_point_t;

#endif // _RTAPP_TYPES_H_
