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

/* For cpu_set_t type */
#define _GNU_SOURCE
#include <sched.h>

#include <pthread.h>
#include <limits.h>
#include "config.h"
#include "dl_syscalls.h"

#if HAVE_LIBNUMA
#include <numa.h>
#endif

#define RTAPP_POLICY_DESCR_LENGTH 16
#define RTAPP_RESOURCE_DESCR_LENGTH 16
#define RTAPP_FTRACE_PATH_LENGTH 256

#define DEFAULT_THREAD_PRIORITY 10
#define DEFAULT_THREAD_NICE 0
#define THREAD_PRIORITY_UNCHANGED INT_MAX

#define PATH_LENGTH 256

/* exit codes */
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define EXIT_INV_CONFIG 2
#define EXIT_INV_COMMANDLINE 3

/* SCHED_IDLE is not available if __USE_GNU is not defined */
#ifndef __USE_GNU
#define SCHED_IDLE 5
#endif

struct _thread_data_t;

typedef enum policy_t
{
	other = SCHED_OTHER,
	idle = SCHED_IDLE,
	rr = SCHED_RR,
	fifo = SCHED_FIFO,
	deadline = SCHED_DEADLINE,
	same = -1
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
	rtapp_timer_unique,
	rtapp_suspend,
	rtapp_resume,
	rtapp_mem,
	rtapp_iorun,
	rtapp_runtime,
	rtapp_yield,
	rtapp_barrier,
	rtapp_fork
} resource_t;

struct _rtapp_mutex {
		pthread_mutex_t obj;
		pthread_mutexattr_t attr;
} ;

struct _rtapp_cond {
	pthread_cond_t obj;
	pthread_condattr_t attr;
};

struct _rtapp_barrier_like {
	/* sync operation which works without ordering - everyone waits
	 * until the last task arrives at the sync point. Conceptually
	 * just like pthread_barrier except we don't have any cleanup
	 * issues which barrier would impose */
	/* mutex to guard read/write of the flag */
	pthread_mutex_t m_obj;
	pthread_mutexattr_t m_attr;
	/* flag to indicate how many are waiting */
	int waiting;
	/* condvar to wait/signal on */
	pthread_cond_t c_obj;
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

struct _rtapp_fork {
	struct _thread_data_t *tdata;
	char *ref;
	int nforks;
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
		struct _rtapp_barrier_like barrier;
		struct _rtapp_fork fork;
	} res;
	int index;
	resource_t type;
	char *name;
} rtapp_resource_t;

typedef struct _rtapp_resources_t {
	int nresources;
	rtapp_resource_t resources[0];
} rtapp_resources_t;

typedef struct _event_data_t {
	char name[48];
	resource_t type;
	int res;
	int dep;
	int duration;
	int count;
} event_data_t;

typedef struct _cpuset_data_t {
	cpu_set_t *cpuset;
	char *cpuset_str;
	size_t cpusetsize;
} cpuset_data_t;

typedef struct _numaset_data_t {
	struct bitmask * numaset;
	char *numaset_str;
} numaset_data_t;

typedef struct _sched_data_t {
	policy_t policy;
	int prio;
	unsigned long runtime;
	unsigned long deadline;
	unsigned long period;
	int util_min;
	int util_max;
} sched_data_t;

typedef struct _taskgroup_data_t {
	char *name;
	int offset;
} taskgroup_data_t;

typedef struct _phase_data_t {
	int loop;
	event_data_t *events;
	int nbevents;
	cpuset_data_t cpu_data;
	numaset_data_t numa_data;
	sched_data_t *sched_data;
	taskgroup_data_t *taskgroup_data;
} phase_data_t;

typedef struct _thread_data_t {
	int ind;
	char *name;
	int lock_pages;
	int duration;

	cpuset_data_t cpu_data; /* cpu set information */
	cpuset_data_t *curr_cpu_data; /* Current cpu set being used */
	cpuset_data_t def_cpu_data; /* Default cpu set for task */

	numaset_data_t numa_data; /* numa bind set mask */
	numaset_data_t *curr_numa_data; /* Current numa bind set being used */

	sched_data_t *sched_data; /* scheduler policy information */
	sched_data_t *curr_sched_data; /* current scheduler policy */

	taskgroup_data_t *taskgroup_data; /* taskgroup information */
	taskgroup_data_t *curr_taskgroup_data; /* current taskgroup */

	int loop;
	int nphases;
	phase_data_t *phases;

	struct timespec main_app_start;

	FILE *log_handler;

	unsigned long delay;

	int forked;
	int num_instances;

	rtapp_resources_t *local_resources;
	rtapp_resources_t **global_resources;
} thread_data_t;

typedef struct _pthread_data_t {
	thread_data_t *data;
	pthread_t thread;
} pthread_data_t;

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
	int num_tasks;

	policy_t policy;
	int duration;

	char *logdir;
	char *logbasename;
	int logsize;
	int gnuplot;
	int calib_cpu;
	int calib_ns_per_loop;

	rtapp_resources_t *resources;
	int pi_enabled;

	int die_on_dmiss;
	int mem_buffer_size;
	char *io_device;

	int cumulative_slack;

	char *ftracedir; /* should end in 'tracing' */
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
