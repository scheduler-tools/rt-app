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

/* for CPU_SET macro */
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/mman.h>  /* for memlock */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "config.h"
#include "rt-app_utils.h"
#include "rt-app_args.h"
#include "rt-app_taskgroups.h"

/*
 * To prevent infinite loops in fork bombs, we will limit the number of
 * permissible forks before we exit on error.
 *
 * This limit is per forking task/thread NOT the aggregated number of forks.
 */
#define FORKS_LIMIT		1024

static volatile sig_atomic_t continue_running;
static pthread_data_t *threads;
static int nthreads;
static volatile sig_atomic_t running_threads;
static int p_load;
rtapp_options_t opts;
static struct timespec t_zero;
static struct timespec t_start;
static pthread_barrier_t threads_barrier;
static pthread_mutex_t joining_mutex;
static pthread_mutex_t fork_mutex;

static ftrace_data_t ft_data = {
	.debugfs = "/sys/kernel/debug",
	.marker_fd = -1,
};

void *thread_body(void *arg);
void setup_thread_logging(thread_data_t *tdata);

static thread_data_t *find_thread_data(const char *name, rtapp_options_t *opts)
{
	int i;

	for (i = 0; i < opts->num_tasks; i++) {
		thread_data_t *tdata = &opts->threads_data[i];
		if (!strcmp(tdata->name, name))
			return tdata;
	}

	log_error("Can't fork unknown task %s", name);
	exit(EXIT_FAILURE);
}

/*
 * Give each created thread a unique name based on tdata->ind. If the thread is
 * forked we track each fork by a unique number that is incremented
 * independently for each fork-event.
 */
static void thread_data_set_unique_name(thread_data_t *tdata, int nforks)
{
	int string_size = strlen(tdata->name) + 1 /* NULL */ + 10 /* postfix */;
	char *unique_name = malloc(string_size);
	if (unique_name) {
		if (tdata->forked) {
			snprintf(unique_name, string_size, "%s-%d-%04d", tdata->name, tdata->ind, nforks);
		} else {
			snprintf(unique_name, string_size, "%s-%d", tdata->name, tdata->ind);
		}
		/*
		 * no need to free tdata->name since tdata is a copy itself
		 */
		tdata->name = unique_name;
	} else {
		log_error("Failed to create a unique name for forked thread %s", tdata->name);
		/* if strdup() fails (again) at least we'll get a null asigned
		 * and we'll know */
		tdata->name = strdup(tdata->name);
	}

	log_notice("thread_data_set_unique_name %d %s", tdata->ind, tdata->name);
}

/*
 * Give each created thread a list of unique resources.
 */
static int thread_data_create_unique_resources(thread_data_t *tdata, const thread_data_t *td)
{
	rtapp_resources_t *table = td->local_resources;
	int local_size = sizeof(rtapp_resources_t) + sizeof(rtapp_resource_t) * table->nresources;

	tdata->local_resources = malloc(local_size);
	if (!tdata->local_resources) {
		log_error("Failed to duplicate thread local resources: %s", td->name);
		return -1;
	}

	memcpy(tdata->local_resources, td->local_resources, local_size);

	return 0;
}

/*
 * Create a new pthread using the info provided in the passed @td thread_data_t.
 *
 * @td:		The thread data of the task to create - we copy it and leave
 *		it unmodified for other fork events to use to create additional
 *		threads based on the same tdata.
 *
 * @index:	Index of the task to create.
 *
 * @forked:	Whether this task is a craeted from fork event or at
 *		application startup.
 *
 * @nforks:	If this is a forked task, we use nforks to give it a unique name.
 *
 * Returns 0 on success or -1 on failure.
 */
static int create_thread(const thread_data_t *td, int index, int forked, int nforks)
{
	thread_data_t *tdata;
	sched_data_t *tsched_data;

	if (!td) {
		log_error("Failed to create new thread, passed NULL thread_data_t: %s", td->name);
		return -1;
	}

	tdata = malloc(sizeof(thread_data_t));
	if (!tdata) {
		log_error("Failed to duplicate thread data: %s", td->name);
		return -1;
	}

	tsched_data = malloc(sizeof(sched_data_t));
	if (!tsched_data) {
		log_error("Failed to duplicate thread sched data: %s", td->name);
		return -1;
	}

	/*
	 * We have one tdata created at config parse, but we
	 * might spawn multiple threads if we were running in
	 * a loop, so ensure we duplicate the tdata before
	 * creating each thread, and we should free it at the
	 * end of the thread_body().
	 *
	 * Also duplicate the sched_data since it is modified by each thread to
	 * keep track of current sched state.
	 */
	memcpy(tdata, td, sizeof(*tdata));
	tdata->sched_data = tsched_data;
	memcpy(tdata->sched_data, td->sched_data, sizeof(*tdata->sched_data));

	/* Mark this thread as forked */
	tdata->forked = forked;
	/* update the index value */
	tdata->ind = index;

	/* Make sure each (forked) thread has a unique name */
	thread_data_set_unique_name(tdata, nforks);

	/* Make sure each (forked) thread has its own unique resources */
	if(thread_data_create_unique_resources(tdata, td))
		return -1;

	setup_thread_logging(tdata);

	/* save a pointer to thread's data */
	threads[index].data = tdata;

	if (pthread_create(&threads[index].thread, NULL, thread_body, (void*) tdata)) {
		perror("Failed to create a thread");
		return -1;
	}

	return 0;
}

/*
 * Function: to do some useless operation.
 * TODO: improve the waste loop with more heavy functions
 */
void waste_cpu_cycles(unsigned long long load_loops)
{
	double param, result;
	double n;
	unsigned long long i;

	param = 0.95;
	n = 4;
	for (i = 0 ; i < load_loops ; i++) {
		result = ldexp(param , (ldexp(param , ldexp(param , n))));
		result = ldexp(param , (ldexp(param , ldexp(param , n))));
		result = ldexp(param , (ldexp(param , ldexp(param , n))));
		result = ldexp(param , (ldexp(param , ldexp(param , n))));
	}
	return;
}

/*
* calibrate_cpu_cycles_1()
* 1st method to calibrate the ns per loop value
* We alternate idle period and run period in order to not trig some hw
* protection mechanism like thermal mitgation
*/
int calibrate_cpu_cycles_1(int clock)
{
	struct timespec start, stop, sleep;
	int max_load_loop = 10000;
	unsigned int diff;
	int nsec_per_loop, avg_per_loop = 0;
	int cal_trial = 1000;

	while (cal_trial) {
		cal_trial--;
		sleep.tv_sec = 1;
		sleep.tv_nsec = 0;

		clock_nanosleep(CLOCK_MONOTONIC, 0, &sleep, NULL);

		clock_gettime(clock, &start);
		waste_cpu_cycles(max_load_loop);
		clock_gettime(clock, &stop);

		diff = (int)timespec_sub_to_ns(&stop, &start);
		nsec_per_loop = diff / max_load_loop;
		avg_per_loop = (avg_per_loop + nsec_per_loop) >> 1;

		/* collect a critical mass of samples.*/
		if ((abs(nsec_per_loop - avg_per_loop) * 50)  < avg_per_loop)
			return avg_per_loop;

		/*
		* use several loop duration in order to be sure to not
		* fall into a specific platform loop duration
		*(like the cpufreq period)
		*/
		/*randomize the number of loops and recheck 1000 times*/
		max_load_loop += 33333;
		max_load_loop %= 1000000;
	}
	return 0;
}

/*
* calibrate_cpu_cycles_2()
* 2nd method to calibrate the ns per loop value
* We continously runs something to ensure that CPU is set to max freq by the
* governor
*/
int calibrate_cpu_cycles_2(int clock)
{
	struct timespec start, stop;
	int max_load_loop = 10000;
	unsigned int diff;
	int nsec_per_loop, avg_per_loop = 0;
	int cal_trial = 1000;

	while (cal_trial) {
		cal_trial--;

		clock_gettime(clock, &start);
		waste_cpu_cycles(max_load_loop);
		clock_gettime(clock, &stop);

		diff = (int)timespec_sub_to_ns(&stop, &start);
		nsec_per_loop = diff / max_load_loop;
		avg_per_loop = (avg_per_loop + nsec_per_loop) >> 1;

		/* collect a critical mass of samples.*/
		if ((abs(nsec_per_loop - avg_per_loop) * 50)  < avg_per_loop)
			return avg_per_loop;

		/*
		* use several loop duration in order to be sure to not
		* fall into a specific platform loop duration
		*(like the cpufreq period)
		*/
		/*randomize the number of loops and recheck 1000 times*/
		max_load_loop += 33333;
		max_load_loop %= 1000000;
	}
	return 0;
}

/*
* calibrate_cpu_cycles()
* Use several methods to calibrate the ns per loop and get the min value which
* correspond to the highest achievable compute capacity.
*/
int calibrate_cpu_cycles(int clock)
{
	int calib1, calib2;

	/* Run 1st method */
	calib1 = calibrate_cpu_cycles_1(clock);

	/* Run 2nd method */
	calib2 = calibrate_cpu_cycles_2(clock);

	if (calib1 < calib2)
		return calib1;
	else
		return calib2;

}

static inline unsigned long loadwait(unsigned long exec)
{
	unsigned long load_count, secs, perf;
	int i;

	/*
	 * Performace is the fixed amount of work that is performed by this run
	 * phase. We need to compute it here because both load_count and exec
	 * might be modified below.
	 */
	perf = exec / p_load;

	/*
	 * If exec is still too big, let's run it in bursts
	 * so that we don't overflow load_count.
	 */
	secs = exec / 1000000;

	for (i = 0; i < secs; i++) {
		load_count = 1000000000/p_load;
		waste_cpu_cycles(load_count);
		exec -= 1000000;
	}

	/*
	 * Run for the remainig exec (if any).
	 */
	load_count = (exec * 1000)/p_load;
	waste_cpu_cycles(load_count);

	return perf;
}

static void ioload(unsigned long count, struct _rtapp_iomem_buf *iomem, int io_fd)
{
	ssize_t ret;

	while (count != 0) {
		unsigned long size;

		if (count > iomem->size)
			size = iomem->size;
		else
			size = count;

		ret = write(io_fd, iomem->ptr, size);
		if (ret == -1) {
			perror("write");
			return;
		}
		count -= ret;
	}
}

static void memload(unsigned long count, struct _rtapp_iomem_buf *iomem)
{
	while (count > 0) {
		unsigned long size;

		if (count > iomem->size)
			size = iomem->size;
		else
			size = count;

		memset(iomem->ptr, 0, size);
		count -= size;
	}
}

static int run_event(event_data_t *event, int dry_run,
		unsigned long *perf, thread_data_t *tdata,
		struct timespec *t_first, log_data_t *ldata)
{
	/* By default we use global resources */
	rtapp_resources_t *table_resources = *(tdata->global_resources);
	rtapp_resource_t *rdata = &(table_resources->resources[event->res]);
	rtapp_resource_t *ddata = &(table_resources->resources[event->dep]);
	unsigned long lock = 0;

	switch(event->type) {
	case rtapp_lock:
		log_debug("lock %s ", rdata->name);
		pthread_mutex_lock(&(rdata->res.mtx.obj));
		lock = 1;
		break;
	case rtapp_unlock:
		log_debug("unlock %s ", rdata->name);
		pthread_mutex_unlock(&(rdata->res.mtx.obj));
		lock = -1;
		break;
	default:
		break;
	}

	if (dry_run)
		return lock;

	switch(event->type) {
	case rtapp_wait:
		log_debug("wait %s ", rdata->name);
		pthread_cond_wait(&(rdata->res.cond.obj), &(ddata->res.mtx.obj));
		break;
	case rtapp_signal:
		log_debug("signal %s ", rdata->name);
		pthread_cond_signal(&(rdata->res.cond.obj));
		break;
	case rtapp_barrier:
		log_debug("barrier %s ", rdata->name);
		pthread_mutex_lock(&(rdata->res.barrier.m_obj));
		if (rdata->res.barrier.waiting == 0) {
			/* everyone is already waiting, signal */
			pthread_cond_broadcast(&(rdata->res.barrier.c_obj));
		} else {
			/* not everyone is waiting, mark then wait */
			rdata->res.barrier.waiting -= 1;
			pthread_cond_wait(&(rdata->res.barrier.c_obj), &(rdata->res.barrier.m_obj));
			rdata->res.barrier.waiting += 1;
		}
		pthread_mutex_unlock(&(rdata->res.barrier.m_obj));
		break;
	case rtapp_sig_and_wait:
		log_debug("signal and wait %s", rdata->name);
		pthread_cond_signal(&(rdata->res.cond.obj));
		pthread_cond_wait(&(rdata->res.cond.obj), &(ddata->res.mtx.obj));
		break;
	case rtapp_broadcast:
		pthread_cond_broadcast(&(rdata->res.cond.obj));
		break;
	case rtapp_sleep:
		{
		struct timespec sleep = usec_to_timespec(event->duration);
		log_debug("sleep %d ", event->duration);
		nanosleep(&sleep, NULL);
		}
		break;
	case rtapp_run:
		{
			struct timespec t_start, t_end;
			log_debug("run %d ", event->duration);
			ldata->c_duration += event->duration;
			clock_gettime(CLOCK_MONOTONIC, &t_start);
			*perf += loadwait(event->duration);
			clock_gettime(CLOCK_MONOTONIC, &t_end);
			t_end = timespec_sub(&t_end, &t_start);
			ldata->duration += timespec_to_usec(&t_end);
		}
		break;
	case rtapp_runtime:
		{
			struct timespec t_start, t_end;
			int64_t diff_ns;

			log_debug("runtime %d ", event->duration);
			ldata->c_duration += event->duration;
			clock_gettime(CLOCK_MONOTONIC, &t_start);

			do {
				/* Do work for 32usec  */
				*perf += loadwait(32);

				clock_gettime(CLOCK_MONOTONIC, &t_end);
				diff_ns = timespec_sub_to_ns(&t_end, &t_start);
			} while ((diff_ns / 1000) < event->duration);

			t_end = timespec_sub(&t_end, &t_start);
			ldata->duration += timespec_to_usec(&t_end);
		}
		break;
	case rtapp_timer_unique:
		{
			/* Unique timer uses local resources */
			rdata = &(tdata->local_resources->resources[event->res]);
		}
	case rtapp_timer:
		{
			struct timespec t_period, t_now, t_wu, t_slack;
			log_debug("timer %d ", event->duration);

			t_period = usec_to_timespec(event->duration);
			ldata->c_period += event->duration;

			if (rdata->res.timer.init == 0) {
				rdata->res.timer.init = 1;
				rdata->res.timer.t_next = *t_first;
			}

			rdata->res.timer.t_next = timespec_add(&rdata->res.timer.t_next, &t_period);
			clock_gettime(CLOCK_MONOTONIC, &t_now);
			t_slack = timespec_sub(&rdata->res.timer.t_next, &t_now);
			if (opts.cumulative_slack)
				ldata->slack += timespec_to_usec_long(&t_slack);
			else
				ldata->slack = timespec_to_usec_long(&t_slack);
			if (timespec_lower(&t_now, &rdata->res.timer.t_next)) {
				clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &rdata->res.timer.t_next, NULL);
				clock_gettime(CLOCK_MONOTONIC, &t_now);
				t_wu = timespec_sub(&t_now, &rdata->res.timer.t_next);
				ldata->wu_latency += timespec_to_usec(&t_wu);
			} else {
				if (rdata->res.timer.relative)
					clock_gettime(CLOCK_MONOTONIC, &rdata->res.timer.t_next);
				ldata->wu_latency = 0UL;
			}
		}
		break;
	case rtapp_suspend:
		{
			log_debug("suspend %s ", rdata->name);
			pthread_mutex_lock(&(ddata->res.mtx.obj));
			pthread_cond_wait(&(rdata->res.cond.obj), &(ddata->res.mtx.obj));
			pthread_mutex_unlock(&(ddata->res.mtx.obj));
		break;
		}
	case rtapp_resume:
		{
			log_debug("resume %s ", rdata->name);
			pthread_mutex_lock(&(ddata->res.mtx.obj));
			pthread_cond_broadcast(&(rdata->res.cond.obj));
			pthread_mutex_unlock(&(ddata->res.mtx.obj));
		break;
		}
	case rtapp_mem:
		{
			log_debug("mem %d", event->count);
			memload(event->count, &rdata->res.buf);
		}
		break;
	case rtapp_iorun:
		{
			log_debug("iorun %d", event->count);
			ioload(event->count, &rdata->res.buf, ddata->res.dev.fd);
		}
		break;
	case rtapp_yield:
		{
			log_debug("yield %d", event->count);
			sched_yield();
		}
		break;
	case rtapp_fork:
		{
			log_debug("fork %s", rdata->res.fork.ref);

			/*
			 * Check if the current thread reached its limit of
			 * number of allowable forks.
			 * We enforce a limit to prevent infinite loops.
			 */
			if (rdata->res.fork.nforks >= FORKS_LIMIT) {
				log_error("%s reached its fork limit (%d)", rdata->res.fork.tdata->name, FORKS_LIMIT);
				exit(EXIT_FAILURE);
			}

			/*
			 * If multiple threads race to fork, we must ensure
			 * each one sees a unique index. Hence the lock.
			 */
			pthread_mutex_lock(&fork_mutex);
			int new_thread_index = nthreads++;
			threads = realloc(threads, nthreads * sizeof(*threads));

			if (!threads) {
				log_error("Failed to allocate memory for a new fork: %s", rdata->res.fork.tdata->name);
				pthread_mutex_unlock(&fork_mutex);
				exit(EXIT_FAILURE);
			}

			/*
			 * Find the thread data associated with this fork and
			 * store it in tdata; this needs to be done once if the
			 * fork is done within a loop.
			 *
			 * Note that we can't search for the reference at parse
			 * time because we are not guaranteed that the task
			 * that is referenced was already parsed; it'll depend
			 * greatly on the ordering within the defined json
			 * file.
			 */
			if (!rdata->res.fork.tdata) {
				rdata->res.fork.tdata = find_thread_data(rdata->res.fork.ref, &opts);
			}

			int ret = create_thread(rdata->res.fork.tdata, new_thread_index, 1, rdata->res.fork.nforks++);
			if (ret) {
				pthread_mutex_unlock(&fork_mutex);
				exit(EXIT_FAILURE);
			}

			running_threads = nthreads;

			pthread_mutex_unlock(&fork_mutex);
		}
		break;
	default:
		break;
	}

	return lock;
}

int run(thread_data_t *tdata,
	phase_data_t *pdata,
	struct timespec *t_first,
	log_data_t *ldata)
{
	event_data_t *events = pdata->events;
	int ind = tdata->ind;
	int nbevents = pdata->nbevents;
	int i, lock = 0;
	unsigned long perf = 0;

	for (i = 0; i < nbevents; i++)
	{
		if (!continue_running && !lock)
			return perf;

		log_debug("[%d] runs events %d type %d ", ind, i, events[i].type);
		log_ftrace(ft_data.marker_fd, FTRACE_EVENT,
			   "rtapp_event: id=%d type=%d desc=%s",
			   i, events[i].type, events[i].name);
		lock += run_event(&events[i], !continue_running, &perf,
				  tdata, t_first, ldata);
	}

	return perf;
}

static void wakeup_all_threads(void)
{
	int i;
	int nresources = opts.resources->nresources;
	rtapp_resource_t *resources = opts.resources->resources;

	/*
	 * Force wake up of all waiting threads.
	 * At now we don't need to look into local resources because rtapp_wait
	 * and rtapp_barrier are always global
	 */
	for (i = 0; i <  nresources; i++) {
		if (resources[i].type == rtapp_wait) {
			pthread_cond_broadcast(&resources[i].res.cond.obj);
		}
		if (resources[i].type == rtapp_barrier) {
			pthread_cond_broadcast(&resources[i].res.barrier.c_obj);
		}
	}
}

static void setup_main_gnuplot(void);

static void __shutdown(bool force_terminate)
{
	int i;

	if(!continue_running)
		return;

	if (force_terminate) {
		continue_running = 0;
		wakeup_all_threads();
	}

	/*
	 * Make sure pthread_join() is done once or we hit undefined behaviour
	 * of multiple simulataneous calls to pthread_join().
	 *
	 * This is a problem because __shutdown() could be called
	 * asynchronously from some signals and from main().
	 */
	if (pthread_mutex_trylock(&joining_mutex))
		return;


	/*
	 * Wait for all threads to terminate.
	 *
	 * pthread_join() will block until the process has terminated. If any
	 * thread forks any processes then it'll update running_threads before
	 * it returns and since running_threads is volatile the for() loop is
	 * guaranteed to check against the updated value in each iteration.
	 * Hence we are guaranteed to wait for all forked threads beside the
	 * originally created ones at startup time.
	 */
	for (i = 0; i < running_threads; i++)
	{
		int ret = pthread_join(threads[i].thread, NULL);
		if (ret)
			perror("pthread_join() failed");
	}

	/*
	 * Set main gnuplot files
	 *
	 * We have to access thread's data structure to fill these files so we must
	 * not free them before.
	 */
	setup_main_gnuplot();

	/*
	 * Now that we don't need the allocated structure anymore, we can safely
	 * free them
	 */
	for (i = 0; i < running_threads; i++)
	{
		/* clean up tdata if this was a forked thread */
		free(threads[i].data->name);
		free(threads[i].data->sched_data);
		free(threads[i].data);
	}


	log_ftrace(ft_data.marker_fd, FTRACE_MAIN,
		   "rtapp_main: event=end");
	if (ftrace_level) {
		log_notice("deconfiguring ftrace");
		close(ft_data.marker_fd);
	}

	remove_cgroups();

	/*
	 * If we unlock the joining_mutex here we could risk a late SIGINT
	 * causing us to re-enter this loop. Since we are calling exit() to
	 * terminate the application and release all resources - we don't
	 * really need to unlock the mutex anyway.
	 */
	exit(EXIT_SUCCESS);
}

static void
shutdown(int sig)
{
	__shutdown(true);
}

static int create_cpuset_str(cpuset_data_t *cpu_data)
{
	unsigned int cpu_count = CPU_COUNT_S(cpu_data->cpusetsize,
							cpu_data->cpuset);
	unsigned int i;
	unsigned int idx = 0;

	/*
	 * Assume we can go up to 9999 cpus. Each cpu would take up to 4 + 2
	 * bytes (4 for the number and 2 for the comma and space). 2 bytes
	 * for beginning bracket + space and 2 bytes for end bracket and space
	 * and finally null-terminator.
	 */
	unsigned int size_needed = cpu_count * 6 + 2 + 2 + 1;

	if (cpu_count > 9999) {
		log_error("Too many cpus specified. Up to 9999 cpus supported");
		exit(EXIT_FAILURE);
	}

	cpu_data->cpuset_str = malloc(size_needed);
	if (!cpu_data->cpuset_str) {
		log_error("Failed to set cpuset string");
		return -1;
	}

	strcpy(cpu_data->cpuset_str, "[ ");
	idx += 2;

	for (i = 0; i < 10000 && cpu_count; ++i) {
		unsigned int n;

		if (CPU_ISSET(i, cpu_data->cpuset)) {
			--cpu_count;
			if (size_needed <= (idx + 1)) {
				log_error("Not enough memory for array");
				exit(EXIT_FAILURE);
			}
			n = snprintf(&cpu_data->cpuset_str[idx],
						size_needed - idx - 1, "%u", i);
			if (n > 0) {
				idx += n;
			} else {
				log_error("Error creating array");
				exit(EXIT_FAILURE);
			}
			if (size_needed <= (idx + 1)) {
				log_error("Not enough memory for array");
				exit(EXIT_FAILURE);
			}
			if (cpu_count) {
				strncat(cpu_data->cpuset_str, ", ",
							size_needed - idx - 1);
				idx += 2;
			}
		}
	}
	strncat(cpu_data->cpuset_str, " ]", size_needed - idx - 1);

	return 0;
}

static void set_thread_affinity(thread_data_t *data, cpuset_data_t *cpu_data)
{
	int ret;
	cpuset_data_t *actual_cpu_data = &data->cpu_data;

	if (data->def_cpu_data.cpuset == NULL) {
		/* Get default affinity */
		cpu_set_t cpuset;
		unsigned int cpu_count;

		ret = pthread_getaffinity_np(pthread_self(),
						    sizeof(cpu_set_t), &cpuset);
		if (ret != 0) {
			errno = ret;
			perror("pthread_get_affinity");
			exit(EXIT_FAILURE);
		}
		cpu_count = CPU_COUNT(&cpuset);
		data->def_cpu_data.cpusetsize = CPU_ALLOC_SIZE(cpu_count);
		data->def_cpu_data.cpuset = CPU_ALLOC(cpu_count);
		memcpy(data->def_cpu_data.cpuset, &cpuset,
						data->def_cpu_data.cpusetsize);
		create_cpuset_str(&data->def_cpu_data);
		data->curr_cpu_data = &data->def_cpu_data;
	}

	/*
	 * Order of preference:
	 * 1. Phase cpuset
	 * 2. Task level cpuset
	 * 3. Default cpuset
	 */
	if (cpu_data->cpuset != NULL)
		actual_cpu_data = cpu_data;

	if (actual_cpu_data->cpuset == NULL)
		actual_cpu_data = &data->def_cpu_data;

	if (!CPU_EQUAL(actual_cpu_data->cpuset, data->curr_cpu_data->cpuset))
	{
		log_debug("[%d] setting cpu affinity to CPU(s) %s", data->ind,
			actual_cpu_data->cpuset_str);
		ret = pthread_setaffinity_np(pthread_self(),
						actual_cpu_data->cpusetsize,
						actual_cpu_data->cpuset);
		if (ret != 0) {
			errno = ret;
			perror("pthread_setaffinity_np");
			exit(EXIT_FAILURE);
		}
		data->curr_cpu_data = actual_cpu_data;
	}
}

static void set_thread_membind(thread_data_t *data, numaset_data_t * numa_data)
{
#if HAVE_LIBNUMA
	if (numa_data->numaset == NULL)
		return;

	if(data->curr_numa_data == numa_data)
		return;

	if (data->curr_numa_data == NULL ||
			!numa_bitmask_equal(numa_data->numaset, data->curr_numa_data->numaset))
	{
		log_debug("[%d] setting numa_membind to Node (s) %s", data->ind,
				numa_data->numaset_str);
		numa_set_membind(numa_data->numaset);
	}
	data->curr_numa_data = numa_data;
#endif
}

/*
 * sched_priority is only meaningful for RT tasks. Otherwise, it must be
 * set to 0 for the setattr syscall to succeed.
 */
static int __sched_priority(thread_data_t *data, sched_data_t *sched_data)
{
	switch (sched_data->policy) {
		case rr:
		case fifo:
			return sched_data->prio;
	}

	 return 0;
}


static void __log_policy_priority_change(thread_data_t *data,
					 sched_data_t *sched_data)
{
	log_debug("[%d] setting scheduler %s priority %d", data->ind,
		  policy_to_string(sched_data->policy),
		  sched_data->prio);

	log_ftrace(ft_data.marker_fd, FTRACE_ATTRS,
		   "rtapp_attrs: event=policy policy=%s prio=%d",
		   policy_to_string(sched_data->policy),
		   sched_data->prio);
}

static bool __set_thread_policy_priority(thread_data_t *data,
					 sched_data_t *sched_data)
{
	struct sched_param param;
	int ret;

	param.sched_priority = __sched_priority(data, sched_data);

	ret = pthread_setschedparam(pthread_self(),
				    sched_data->policy,
				    &param);
	if (ret) {
		log_critical("[%d] pthread_setschedparam returned %d",
			     data->ind, ret);
		errno = ret;
		perror("pthread_setschedparam");
		exit(EXIT_FAILURE);
	}
}

static void __set_thread_nice(thread_data_t *data, sched_data_t *sched_data)
{
	int ret;

	if (sched_data->prio > 19 || sched_data->prio < -20) {
		log_critical("[%d] setpriority %d nice invalid. "
			     "Valid between -20 and 19",
			     data->ind, sched_data->prio);
		exit(EXIT_FAILURE);
	}

	ret = setpriority(PRIO_PROCESS, 0, sched_data->prio);
	if (ret) {
		log_critical("[%d] setpriority returned %d", data->ind, ret);
		errno = ret;
		perror("setpriority");
		exit(EXIT_FAILURE);
	}
}

static void _set_thread_cfs(thread_data_t *data, sched_data_t *sched_data)
{
	/* Priority unchanged => Policy unchanged */
	if (sched_data->prio == THREAD_PRIORITY_UNCHANGED)
		return;
	/*
	 * In the CFS case, sched_data->prio is the NICE value. As long as the
	 * policy hasn't changed, there's no need to call
	 * __set_thread_policy_priority()
	 *
	 * We can't rely on policy == same as it is overwritten in
	 * set_thread_param()
	 */
	if (!data->curr_sched_data ||
	    (sched_data->policy != data->curr_sched_data->policy))
		__set_thread_policy_priority(data, sched_data);

	if (sched_data->policy == other)
		__set_thread_nice(data, sched_data);

	__log_policy_priority_change(data, sched_data);
}

static void _set_thread_rt(thread_data_t *data, sched_data_t *sched_data)
{
	/* Priority unchanged => Policy unchanged */
	if (sched_data->prio == THREAD_PRIORITY_UNCHANGED)
		return;

	__set_thread_policy_priority(data, sched_data);
	__log_policy_priority_change(data, sched_data);
}

/* deadline can't rely on the default __set_thread_policy_priority */
static void _set_thread_deadline(thread_data_t *data, sched_data_t *sched_data)
{
	struct sched_attr sa_params = {0};
	unsigned int flags = 0;
	pid_t tid;
	int ret;

	log_debug("[%d] setting scheduler %s exec %lu, deadline %lu"
		  " period %lu",
		  data->ind,
		  policy_to_string(sched_data->policy),
		  sched_data->runtime, sched_data->deadline,
		  sched_data->period);
	log_ftrace(ft_data.marker_fd, FTRACE_ATTRS,
		   "rtapp_attrs: event=policy policy=dl runtime=%lu "
		   "deadline=%lu period=%lu",
		   sched_data->runtime, sched_data->deadline,
		   sched_data->period);

	tid = gettid();
	sa_params.size = sizeof(struct sched_attr);
	sa_params.sched_flags = 0;
	sa_params.sched_policy = SCHED_DEADLINE;
	sa_params.sched_priority = __sched_priority(data, sched_data);
	sa_params.sched_runtime = sched_data->runtime;
	sa_params.sched_deadline = sched_data->deadline;
	sa_params.sched_period = sched_data->period;

	ret = sched_setattr(tid, &sa_params, flags);
	if (ret) {
		log_critical("[%d] sched_setattr returned %d",
			     data->ind, ret);
		errno = ret;
		perror("sched_setattr: failed to set deadline attributes");
		exit(EXIT_FAILURE);
	}
}

static void _set_thread_uclamp(thread_data_t *data, sched_data_t *sched_data)
{
	struct sched_attr sa_params = {0};
	unsigned int flags = 0;
	pid_t tid;
	int ret;

	if ((sched_data->util_min == -2 &&
	     sched_data->util_max == -2))
		    return;

	sa_params.sched_policy = sched_data->policy;
	sa_params.sched_priority = __sched_priority(data, sched_data);
	sa_params.size = sizeof(struct sched_attr);
	sa_params.sched_flags = SCHED_FLAG_KEEP_ALL;
	tid = gettid();

	if (sched_data->util_min != -2) {
		sa_params.sched_util_min = sched_data->util_min;
		sa_params.sched_flags |= SCHED_FLAG_UTIL_CLAMP_MIN;
		log_debug("[%d] setting util_min=%d",
			   data->ind, sched_data->util_min);
		log_ftrace(ft_data.marker_fd, FTRACE_ATTRS,
			   "rtapp_attrs: event=uclamp util_min=%d",
			   sched_data->util_min);
	}
	if (sched_data->util_max != -2) {
		sa_params.sched_util_max = sched_data->util_max;
		sa_params.sched_flags |= SCHED_FLAG_UTIL_CLAMP_MAX;
		log_debug("[%d] setting util_max=%d",
			   data->ind, sched_data->util_max);
		log_ftrace(ft_data.marker_fd, FTRACE_ATTRS,
			   "rtapp_attrs: event=uclamp util_max=%d",
			   sched_data->util_max);
	}

	ret = sched_setattr(tid, &sa_params, flags);
	if (ret) {
		log_critical("[%d] sched_setattr returned %d",
			     data->ind, ret);
		errno = ret;
		perror("sched_setattr: failed to set uclamp value(s)");
		exit(EXIT_FAILURE);
	}
}

static void set_thread_param(thread_data_t *data, sched_data_t *sched_data)
{
	if (!sched_data)
		return;

	if (data->curr_sched_data == sched_data)
		return;

	/* if no policy is specified, reuse the previous one */
	if ((sched_data->policy == same) && data->curr_sched_data)
		sched_data->policy = data->curr_sched_data->policy;

	switch (sched_data->policy) {
		case rr:
		case fifo:
			_set_thread_rt(data, sched_data);
			_set_thread_uclamp(data, sched_data);
			break;
		case other:
		case idle:
			_set_thread_cfs(data, sched_data);
			_set_thread_uclamp(data, sched_data);
			data->lock_pages = 0; /* forced off */
			break;
		case deadline:
			_set_thread_deadline(data, sched_data);
			break;
		default:
			log_error("Unknown scheduling policy %d",
				  sched_data->policy);
			exit(EXIT_FAILURE);
	}

	data->curr_sched_data = sched_data;
}

void setup_thread_gnuplot(thread_data_t *tdata);

void *thread_body(void *arg)
{
	thread_data_t *data = (thread_data_t*) arg;
	phase_data_t *pdata;
	log_data_t ldata;
	struct sched_param param;
	struct timespec t_start, t_end, t_first;
	unsigned long t_start_usec;
	long slack;
	timing_point_t *curr_timing;
	timing_point_t *timings;
	timing_point_t tmp_timing;
	unsigned int timings_size, timing_loop;
	struct sched_attr attr;
	int ret, phase, phase_loop, thread_loop, log_idx;

	/* Set thread name */
	ret = pthread_setname_np(pthread_self(), data->name);
	if (ret !=  0) {
		perror("pthread_setname_np thread name over 16 characters");
	}

	/* Get the 1st phase's data */
	pdata = &data->phases[0];

	/* Init timing buffer */
	if (opts.logsize > 0) {
		timings = malloc(opts.logsize);
		/*
		 * If malloc return null ptr because it fails to alloc mem, we are
		 * safe. timing buffer will not be used
		 */
		timings_size = opts.logsize / sizeof(timing_point_t);
	} else {
		timings = NULL;
		timings_size = 0;
	}
	timing_loop = 0;

	/* Lock pages */
	if (data->lock_pages == 1)
	{
		log_notice("[%d] Locking pages in memory", data->ind);
		ret = mlockall(MCL_CURRENT | MCL_FUTURE);
		if (ret != 0) {
			perror("mlockall");
			exit(EXIT_FAILURE);
		}
	}

	if (data->ind == 0) {
		/*
		 * Only first thread sets t_zero. Other threads sync with this
		 * timestamp.
		 */
		clock_gettime(CLOCK_MONOTONIC, &t_zero);
		log_ftrace(ft_data.marker_fd, FTRACE_TASK,
			   "rtapp_main: event=clock_ref data=%llu",
			   timespec_to_usec_ull(&t_zero));
	}

	if (!data->forked)
		pthread_barrier_wait(&threads_barrier);

	t_first = t_zero;

	log_notice("[%d] starting thread ...\n", data->ind);

	if (opts.logsize)
		fprintf(data->log_handler, "%s %8s %8s %8s %15s %15s %15s %10s %10s %10s %10s\n",
				   "#idx", "perf", "run", "period",
				   "start", "end", "rel_st", "slack",
				   "c_duration", "c_period", "wu_lat");

	log_ftrace(ft_data.marker_fd, FTRACE_TASK,
		   "rtapp_task: event=start");

	if (data->delay > 0) {
		struct timespec delay = usec_to_timespec(data->delay);

		log_debug("initial delay %lu ", data->delay);
		t_first = timespec_add(&t_first, &delay);
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t_first,
				NULL);
	}

	/* TODO find a better way to handle that constraint:
	 * Set the task to SCHED_DEADLINE as far as possible touching its
	 * budget as little as possible for the first iteration.
	 */

	/* Set scheduling policy and print pretty info on stdout */
	log_notice("[%d] Starting with %s policy with priority %d",
			data->ind, policy_to_string(data->sched_data->policy),
			data->sched_data->prio);
	set_thread_param(data, data->sched_data);
	set_thread_membind(data, &data->numa_data);
	set_thread_taskgroup(data, data->taskgroup_data);

	/*
	 * phase        - index of current phase in data->phases array
	 * phase_loop   - current iteration of current phase
	 *                (corresponds to "loop" at phase level)
	 * thread_loop  - current iteration of thread/phases
	 *                (corresponds to "loop" at task level
	 * log_idx      - index of current row in the log buffer
	 */
	phase = phase_loop = thread_loop = log_idx = 0;

	/* The following is executed for each phase. */
	while (continue_running && thread_loop != data->loop) {
		struct timespec t_diff, t_rel_start;

		set_thread_affinity(data, &pdata->cpu_data);
		set_thread_param(data, pdata->sched_data);
		set_thread_membind(data, &pdata->numa_data);
		set_thread_taskgroup(data, pdata->taskgroup_data);

		log_ftrace(ft_data.marker_fd, FTRACE_LOOP,
			   "rtapp_loop: event=start thread_loop=%d phase=%d phase_loop=%d",
			   thread_loop, phase, phase_loop);

		log_debug("[%d] begins thread_loop %d phase %d phase_loop %d",
			  data->ind, thread_loop, phase, phase_loop);

		memset(&ldata, 0, sizeof(ldata));
		clock_gettime(CLOCK_MONOTONIC, &t_start);
		ldata.perf = run(data, pdata, &t_first, &ldata);
		clock_gettime(CLOCK_MONOTONIC, &t_end);

		if (timings)
			curr_timing = &timings[log_idx];
		else
			curr_timing = &tmp_timing;

		t_diff = timespec_sub(&t_end, &t_start);
		t_rel_start = timespec_sub(&t_start, &data->main_app_start);

		curr_timing->ind = data->ind;
		curr_timing->rel_start_time = timespec_to_usec_ull(&t_rel_start);
		curr_timing->start_time = timespec_to_usec_ull(&t_start);
		curr_timing->end_time = timespec_to_usec_ull(&t_end);
		curr_timing->period = timespec_to_usec(&t_diff);
		curr_timing->duration = ldata.duration;
		curr_timing->perf = ldata.perf;
		curr_timing->wu_latency = ldata.wu_latency;
		curr_timing->slack = ldata.slack;
		curr_timing->c_period = ldata.c_period;
		curr_timing->c_duration = ldata.c_duration;

		if (opts.logsize && !timings && continue_running)
			log_timing(data->log_handler, curr_timing);

		log_ftrace(ft_data.marker_fd, FTRACE_LOOP,
			   "rtapp_loop: event=end thread_loop=%d phase=%d phase_loop=%d",
			   thread_loop, phase, phase_loop);
		log_ftrace(ft_data.marker_fd, FTRACE_STATS,
			   "rtapp_stats: period=%d run=%d wu_lat=%d slack=%d c_period=%d c_run=%d",
			   curr_timing->period,
			   curr_timing->duration,
			   curr_timing->wu_latency,
			   curr_timing->slack,
			   curr_timing->c_period,
			   curr_timing->c_duration);

		phase_loop++;
		/* Reached the specified number of loops for this phase. */
		if (phase_loop == pdata->loop) {
			phase_loop = 0;

			phase++;
			if (phase == data->nphases) {
				/*
				 * Phases are finished, but we might potentially have
				 * to start over (depending on data->loop).
				 */
				phase = 0;
				thread_loop++;
				/*
				 * Overflow due to data->loop being -1 (looping forever)
				 * Reset thread_loop, so that we can continue looping.
				 */
				if (thread_loop < 0)
					thread_loop = 0;
			}
			pdata = &data->phases[phase];
		}

		log_idx++;
		if (log_idx >= timings_size) {
			timing_loop = 1;
			log_idx = 0;
		}
	}

	param.sched_priority = 0;
	ret = pthread_setschedparam(pthread_self(),
				    SCHED_OTHER,
				    &param);
	if (ret != 0) {
		errno = ret;
		perror("pthread_setschedparam");
		exit(EXIT_FAILURE);
	}

	/* Force thread into root taskgroup. */
	reset_thread_taskgroup();

	if (timings) {
		int j;

		for (j = log_idx; timing_loop && (j < timings_size); j++)
			log_timing(data->log_handler, &timings[j]);
		for (j = 0; j < log_idx; j++)
			log_timing(data->log_handler, &timings[j]);
	}

	log_ftrace(ft_data.marker_fd, FTRACE_TASK,
		   "rtapp_task: event=end");

	/* set gnuplot file if enable */
	setup_thread_gnuplot(data);

	log_notice("[%d] Exiting.", data->ind);
	if (opts.logsize)
		fclose(data->log_handler);

	pthread_exit(NULL);
}

void setup_thread_logging(thread_data_t *tdata)
{
	char tmp[PATH_LENGTH];

	tdata->duration = opts.duration;
	tdata->main_app_start = t_start;
	tdata->lock_pages = opts.lock_pages;

	if (!opts.logsize)
		return;

	if (opts.logdir) {
		snprintf(tmp, PATH_LENGTH, "%s/%s-%s.log",
			 opts.logdir,
			 opts.logbasename,
			 tdata->name);
		tdata->log_handler = fopen(tmp, "w");
		if (!tdata->log_handler) {
			log_error("Cannot open logfile %s", tmp);
			exit(EXIT_FAILURE);
		}
	} else {
		tdata->log_handler = stdout;
	}
}

void setup_thread_gnuplot(thread_data_t *tdata)
{
	FILE *gnuplot_script = NULL;
	char tmp[PATH_LENGTH];

	if (!opts.gnuplot || !opts.logdir)
		return;

	snprintf(tmp, PATH_LENGTH, "%s/%s-%s.plot",
		opts.logdir, opts.logbasename, tdata->name);
	gnuplot_script = fopen(tmp, "w+");
	snprintf(tmp, PATH_LENGTH, "%s-%s.eps",
		opts.logbasename, tdata->name);
	fprintf(gnuplot_script,
		"set terminal postscript enhanced color\n"
		"set output '%s'\n"
		"set grid\n"
		"set key outside right\n"
		"set title \"Measured %s Loop stats\"\n"
		"set xlabel \"Loop start time [msec]\"\n"
		"set ylabel \"Period/Run Time [usec]\"\n"
		"set y2label \"Load [number of 1000 loops executed]\"\n"
		"set y2tics  \n"
		"set xtics rotate by -45\n"
		"plot ", tmp, tdata->name);

	fprintf(gnuplot_script,
		"\"%s-%s.log\" u ($5/1000000):2 w l"
		" title \"load \" axes x1y2, ",
		opts.logbasename, tdata->name);

	fprintf(gnuplot_script,
		"\"%s-%s.log\" u ($5/1000000):3 w l"
		" title \"run \", ",
		opts.logbasename, tdata->name);

	fprintf(gnuplot_script,
		"\"%s-%s.log\" u ($5/1000000):4 w l"
		" title \"period \" ",
		opts.logbasename, tdata->name);

	fprintf(gnuplot_script, "\n");

	fprintf(gnuplot_script, "set terminal wxt\nreplot\n");
	fclose(gnuplot_script);
}

static void setup_main_gnuplot(void)
{
	int i;
	FILE *gnuplot_script = NULL;
	char tmp[PATH_LENGTH];

	/* Prepare gnuplot files before starting the use case */
	if (opts.logdir && opts.gnuplot) {
		/* gnuplot plot of the period */
		snprintf(tmp, PATH_LENGTH, "%s/%s-period.plot",
			 opts.logdir, opts.logbasename);
		gnuplot_script = fopen(tmp, "w+");
		snprintf(tmp, PATH_LENGTH, "%s-period.eps",
			 opts.logbasename);
		fprintf(gnuplot_script,
			"set terminal postscript enhanced color\n"
			"set output '%s'\n"
			"set grid\n"
			"set key outside right\n"
			"set title \"Measured time per loop\"\n"
			"set xlabel \"Loop start time [usec]\"\n"
			"set ylabel \"Period Time [usec]\"\n"
			"set xtics rotate by -45\n"
			"set key noenhanced\n"
			"plot ", tmp);

		for (i=0; i<running_threads; i++) {
			fprintf(gnuplot_script,
				"\"%s-%s.log\" u ($5/1000):4 w l"
				" title \"thread [%s] (%s)\"",
				opts.logbasename, threads[i].data->name,
				threads[i].data->name,
				policy_to_string(threads[i].data->sched_data->policy));

			if ( i == nthreads-1)
				fprintf(gnuplot_script, "\n");
			else
				fprintf(gnuplot_script, ", ");
		}

		fprintf(gnuplot_script, "set terminal wxt\nreplot\n");
		fclose(gnuplot_script);

		/* gnuplot of the run time */
		snprintf(tmp, PATH_LENGTH, "%s/%s-run.plot",
			 opts.logdir, opts.logbasename);
		gnuplot_script = fopen(tmp, "w+");
		snprintf(tmp, PATH_LENGTH, "%s-run.eps",
			 opts.logbasename);
		fprintf(gnuplot_script,
			"set terminal postscript enhanced color\n"
			"set output '%s'\n"
			"set grid\n"
			"set key outside right\n"
			"set title \"Measured run time per loop\"\n"
			"set xlabel \"Loop start time [usec]\"\n"
			"set ylabel \"Run Time [usec]\"\n"
			"set xtics rotate by -45\n"
			"set key noenhanced\n"
			"plot ", tmp);

		for (i=0; i<running_threads; i++) {
			fprintf(gnuplot_script,
				"\"%s-%s.log\" u ($5/1000):3 w l"
				" title \"thread [%s] (%s)\"",
				opts.logbasename, threads[i].data->name,
				threads[i].data->name,
				policy_to_string(threads[i].data->sched_data->policy));

			if ( i == nthreads-1)
				fprintf(gnuplot_script, "\n");
			else
				fprintf(gnuplot_script, ", ");
		}

		fprintf(gnuplot_script, "set terminal wxt\nreplot\n");
		fclose(gnuplot_script);
	}
}

int main(int argc, char* argv[])
{
	int i, res, nresources;
	rtapp_resource_t *rdata;
	static cpu_set_t orig_set;
	struct stat sb;
	char tmp[PATH_LENGTH];

	parse_command_line(argc, argv, &opts);

	/* If logdir provided, check if existing */
	if (opts.logdir && (stat(opts.logdir, &sb) || !S_ISDIR(sb.st_mode))){
		log_error("Log directory %s not existing!\n", opts.logdir);
		exit(EXIT_FAILURE);
	}

	/* allocated threads */
	nthreads = opts.nthreads;
	threads = malloc(nthreads * sizeof(*threads));
	if (!threads) {
		log_error("Cannot allocate threads");
		exit(EXIT_FAILURE);
	}
	pthread_barrier_init(&threads_barrier, NULL, nthreads);
	pthread_mutex_init(&joining_mutex, NULL);
	pthread_mutex_init(&fork_mutex, NULL);

	/* install a signal handler for proper shutdown */
	signal(SIGQUIT, shutdown);
	signal(SIGTERM, shutdown);
	signal(SIGHUP, shutdown);
	signal(SIGINT, shutdown);

	/* If using ftrace, open trace and marker fds */
	if (ftrace_level != FTRACE_NONE) {
		log_notice("configuring ftrace");
		// check if tracing is enabled
		strcpy(tmp, ft_data.debugfs);
		strcat(tmp, "/tracing/tracing_on");
		int ftrace_f = open(tmp, O_RDONLY);
		if (ftrace_f < 0){
			log_error("Cannot open tracing_on file %s", tmp);
			exit(EXIT_FAILURE);
		}
		char trace_val[10];
		int ret = read(ftrace_f, trace_val, 10);
		if ( ret < 0 || trace_val[0] != '1'){
			log_error("tracing is not enabled in file %s", tmp);
			exit(EXIT_FAILURE);
		}
		close(ftrace_f);
		// set the marker
		strcpy(tmp, ft_data.debugfs);
		strcat(tmp, "/tracing/trace_marker");
		ft_data.marker_fd = open(tmp, O_WRONLY);
		if (ft_data.marker_fd < 0) {
			log_error("Cannot open trace_marker file %s", tmp);
			exit(EXIT_FAILURE);
		}
	}
	log_ftrace(ft_data.marker_fd, FTRACE_MAIN,
		   "rtapp_main: event=start");

	/* Init global running_variable */
	continue_running = 1;

	/* Needs to calibrate 'calib_cpu' core */
	if (opts.calib_ns_per_loop == 0) {
		log_notice("Calibrate ns per loop");
		cpu_set_t calib_set;

		CPU_ZERO(&calib_set);
		CPU_SET(opts.calib_cpu, &calib_set);
		sched_getaffinity(0, sizeof(cpu_set_t), &orig_set);
		sched_setaffinity(0, sizeof(cpu_set_t), &calib_set);
		p_load = calibrate_cpu_cycles(CLOCK_MONOTONIC);
		sched_setaffinity(0, sizeof(cpu_set_t), &orig_set);
		log_notice("pLoad = %dns : calib_cpu %d", p_load, opts.calib_cpu);
	} else {
		p_load = opts.calib_ns_per_loop;
		log_notice("pLoad = %dns", p_load);
	}

	initialize_cgroups();
	add_cgroups();

	/* Take the beginning time for everything */
	clock_gettime(CLOCK_MONOTONIC, &t_start);

	/* Sync timer resources with start time */
	clock_gettime(CLOCK_MONOTONIC, &t_start);

	/* Start the use case */
	int ind = 0;
	for (i = 0; i < opts.num_tasks; i++) {
		/*
		 * Duplicate thread data so that we can safely copy the
		 * original thread data when forking.
		 *
		 * If we don't do that and try to fork a running thread, we
		 * might have partially modified content of thread data since
		 * thread_body() calls functions like set_thread_affinity()
		 * that modifies thread data at runtime. Rather than introduce
		 * complex locking ensure that the original parsed thread_data
		 * are intact and duplicate them before forking or when we run
		 * for the first time as in here.
		 */
		thread_data_t *tdata_orig = &opts.threads_data[i];
		int j;

		if (tdata_orig->num_instances < 0) {
			log_error("Invalid num_instances value: %d", tdata_orig->num_instances);
			goto exit_err;
		}

		for (j = 0; j < tdata_orig->num_instances; j++) {
			int ret = create_thread(tdata_orig, ind++, 0, 0);
			if (ret) {
				goto exit_err;
			}
		}
	}
	running_threads = nthreads;

	if (opts.duration > 0) {
		sleep(opts.duration);
		log_ftrace(ft_data.marker_fd, FTRACE_MAIN,
			   "rtapp_main: event=shutdown");
		__shutdown(true);
	}

	__shutdown(false);

exit_err:
	remove_cgroups();
	exit(EXIT_FAILURE);
}
