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
#include <string.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/mman.h>  /* for memlock */

#include "config.h"
#include "rt-app_utils.h"
#include "rt-app_args.h"

static int errno;
static volatile sig_atomic_t continue_running;
static pthread_t *threads;
static int nthreads;
static volatile sig_atomic_t running_threads;
static int p_load;
rtapp_options_t opts;
static struct timespec t_zero;
static pthread_barrier_t threads_barrier;

static ftrace_data_t ft_data = {
	.debugfs = "/sys/kernel/debug",
	.marker_fd = -1,
};

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
		unsigned long *perf, rtapp_resource_t *resources,
		struct timespec *t_first, log_data_t *ldata)
{
	rtapp_resource_t *rdata = &(resources[event->res]);
	rtapp_resource_t *ddata = &(resources[event->dep]);
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
		{
			struct timespec t_delta;
			log_debug("barrier %s ", rdata->name);
			pthread_mutex_lock(&(rdata->res.barrier.m_obj));
			if (rdata->res.barrier.waiting == 0) {
				/* everyone is already waiting, signal */
				pthread_cond_broadcast(&(rdata->res.barrier.c_obj));
				clock_gettime(CLOCK_MONOTONIC, &t_delta);
				t_delta = timespec_sub(&t_delta, &rdata->res.barrier.t_ref);
				ldata->pipe_latency = timespec_to_usec(&t_delta);
				log_debug("pipeline duration %lu ", ldata->pipe_latency);
			} else {
				/* not everyone is waiting, mark then wait */
				rdata->res.barrier.waiting -= 1;
				pthread_cond_wait(&(rdata->res.barrier.c_obj), &(rdata->res.barrier.m_obj));
				rdata->res.barrier.waiting += 1;
			}
			pthread_mutex_unlock(&(rdata->res.barrier.m_obj));
		}
		break;
	case rtapp_bref:
		log_debug("bref %s ", rdata->name);
		clock_gettime(CLOCK_MONOTONIC, &rdata->res.barrier.t_ref);
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
			pthread_yield();
		}
		break;
	default:
		break;
	}

	return lock;
}

int run(int ind,
	phase_data_t *pdata,
	rtapp_resource_t *resources,
	struct timespec *t_first,
	log_data_t *ldata)
{
	event_data_t *events = pdata->events;
	int nbevents = pdata->nbevents;
	int i, lock = 0;
	unsigned long perf = 0;

	for (i = 0; i < nbevents; i++)
	{
		if (!continue_running && !lock)
			return perf;

		log_debug("[%d] runs events %d type %d ", ind, i, events[i].type);
		if (opts.ftrace)
				log_ftrace(ft_data.marker_fd,
						"[%d] executing %d",
						ind, i);
		lock += run_event(&events[i], !continue_running, &perf,
				  resources, t_first, ldata);
	}

	return perf;
}

static void
shutdown(int sig)
{
	int i;

	if(!continue_running)
		return;

	/* notify threads, join them, then exit */
	continue_running = 0;

	/* Force wake up of all waiting threads */
	for (i = 0; i <  opts.nresources; i++) {
		if (opts.resources[i].type == rtapp_wait) {
			pthread_cond_broadcast(&opts.resources[i].res.cond.obj);
		}
		if (opts.resources[i].type == rtapp_barrier) {
			pthread_cond_broadcast(&opts.resources[i].res.barrier.c_obj);
		}
	}

	/* wait up all waiting threads */
	for (i = 0; i < running_threads; i++)
	{
		pthread_join(threads[i], NULL);
	}

	if (opts.ftrace) {
		log_ftrace(ft_data.marker_fd, "main ends\n");
		log_notice("deconfiguring ftrace");
		close(ft_data.marker_fd);
	}

	exit(EXIT_SUCCESS);
}

static void create_cpuset_str(cpuset_data_t *cpu_data)
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

static void set_thread_priority(thread_data_t *data, sched_data_t *sched_data)
{
	struct sched_param param;
	policy_t policy;
#ifdef DLSCHED
	struct sched_attr dl_params;
	pid_t tid;
	unsigned int flags = 0;
#endif
	int ret;

	if (sched_data == NULL)
		return;

	if (data->curr_sched_data == sched_data)
		return;

	if ((sched_data->policy == same) &&  (data->curr_sched_data)){
		/* if policy not specificed, reuse previous policy */
		sched_data->policy = data->curr_sched_data->policy;
	}


	switch (sched_data->policy)
	{
		case rr:
		case fifo:
			log_debug("[%d] setting scheduler %s priority %d", data->ind,
					policy_to_string(sched_data->policy), sched_data->prio);
			param.sched_priority = sched_data->prio;
			ret = pthread_setschedparam(pthread_self(),
					sched_data->policy,
					&param);
			if (ret != 0) {
				log_critical("[%d] pthread_setschedparam"
				     "returned %d", data->ind, ret);
				errno = ret;
				perror("pthread_setschedparam");
				exit(EXIT_FAILURE);
			}
			break;

		case other:
			log_debug("[%d] setting scheduler %s priority %d", data->ind,
					policy_to_string(sched_data->policy), sched_data->prio);


			if (sched_data->prio > 19 || sched_data->prio < -20) {
				log_critical("[%d] setpriority "
					"%d nice invalid. "
					"Valid between -20 and 19",
					data->ind, sched_data->prio);
				exit(EXIT_FAILURE);
			}

			ret = setpriority(PRIO_PROCESS, 0,
					sched_data->prio);
			if (ret != 0) {
				log_critical("[%d] setpriority"
				     "returned %d", data->ind, ret);
				errno = ret;
				perror("setpriority");
				exit(EXIT_FAILURE);
			}

			data->lock_pages = 0; /* forced off for SCHED_OTHER */
			break;

#ifdef DLSCHED
		case deadline:
			log_debug("[%d] setting scheduler %s exec %lu, deadline %lu"
					" period %lu", data->ind,
					policy_to_string(sched_data->policy), sched_data->period,
					sched_data->runtime, sched_data->deadline);

			tid = gettid();
			dl_params.size = sizeof(struct sched_attr);
			dl_params.sched_flags = 0;
			dl_params.sched_policy = SCHED_DEADLINE;
			dl_params.sched_priority = 0;
			dl_params.sched_runtime = sched_data->runtime;
			dl_params.sched_deadline = sched_data->deadline;
			dl_params.sched_period = sched_data->period;

			ret = sched_setattr(tid, &dl_params, flags);
			if (ret != 0) {
				log_critical("[%d] sched_setattr "
						"returned %d", data->ind, ret);
				errno = ret;
				perror("sched_setattr");
				exit(EXIT_FAILURE);
			}
		break;
#endif

		default:
			log_error("Unknown scheduling policy %d",
					sched_data->policy);

			exit(EXIT_FAILURE);
	}

	data->curr_sched_data = sched_data;
}

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
		if (ret < 0) {
			errno = ret;
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
		if (opts.ftrace)
			log_ftrace(ft_data.marker_fd,
				"[%d] sets zero time",
				data->ind);
	}

	pthread_barrier_wait(&threads_barrier);
	t_first = t_zero;

	log_notice("[%d] starting thread ...\n", data->ind);

	fprintf(data->log_handler, "%s %8s %8s %8s %15s %15s %15s %10s %10s %10s %10s %10s\n",
				   "#idx", "perf", "run", "period",
				   "start", "end", "rel_st", "slack",
				   "c_duration", "c_period", "wu_lat",
				   "pipe_lat");

	if (opts.ftrace)
		log_ftrace(ft_data.marker_fd, "[%d] starts", data->ind);

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
	set_thread_priority(data, data->sched_data);

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
		set_thread_priority(data, pdata->sched_data);

		if (opts.ftrace)
			log_ftrace(ft_data.marker_fd,
				   "[%d] begins thread_loop %d phase %d phase_loop %d",
				   data->ind, thread_loop, phase, phase_loop);
		log_debug("[%d] begins thread_loop %d phase %d phase_loop %d",
			  data->ind, thread_loop, phase, phase_loop);

		memset(&ldata, 0, sizeof(ldata));
		clock_gettime(CLOCK_MONOTONIC, &t_start);
		ldata.perf = run(data->ind, pdata, *(data->resources),
				&t_first, &ldata);
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
		curr_timing->pipe_latency = ldata.pipe_latency;
		curr_timing->slack = ldata.slack;
		curr_timing->c_period = ldata.c_period;
		curr_timing->c_duration = ldata.c_duration;

		if (opts.logsize && !timings && continue_running)
			log_timing(data->log_handler, curr_timing);

		if (opts.ftrace)
			log_ftrace(ft_data.marker_fd,
				   "[%d] end thread_loop %d phase %d phase_loop %d",
				   data->ind, thread_loop, phase, phase_loop);

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

	if (timings) {
		int j;

		for (j = log_idx; timing_loop && (j < timings_size); j++)
			log_timing(data->log_handler, &timings[j]);
		for (j = 0; j < log_idx; j++)
			log_timing(data->log_handler, &timings[j]);
	}


	if (opts.ftrace)
		log_ftrace(ft_data.marker_fd, "[%d] exiting", data->ind);

	log_notice("[%d] Exiting.", data->ind);
	fclose(data->log_handler);

	pthread_exit(NULL);
}


int main(int argc, char* argv[])
{
	struct timespec t_start;
	FILE *gnuplot_script = NULL;
	int i, res, nresources;
	thread_data_t *tdata;
	rtapp_resource_t *rdata;
	char tmp[PATH_LENGTH];
	static cpu_set_t orig_set;

	parse_command_line(argc, argv, &opts);

	/* allocated threads */
	nthreads = opts.nthreads;
	threads = malloc(nthreads * sizeof(pthread_t));
	pthread_barrier_init(&threads_barrier, NULL, nthreads);

	/* install a signal handler for proper shutdown */
	signal(SIGQUIT, shutdown);
	signal(SIGTERM, shutdown);
	signal(SIGHUP, shutdown);
	signal(SIGINT, shutdown);

	/* If using ftrace, open trace and marker fds */
	if (opts.ftrace) {
		log_notice("configuring ftrace");
		strcpy(tmp, ft_data.debugfs);
		strcat(tmp, "/tracing/tracing_on");
		strcpy(tmp, ft_data.debugfs);
		strcat(tmp, "/tracing/trace_marker");
		ft_data.marker_fd = open(tmp, O_WRONLY);
		if (ft_data.marker_fd < 0) {
			log_error("Cannot open trace_marker file %s", tmp);
			exit(EXIT_FAILURE);
		}

		log_ftrace(ft_data.marker_fd, "main creates threads\n");
	}

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

	/* Take the beginning time for everything */
	clock_gettime(CLOCK_MONOTONIC, &t_start);

	/* Prepare log file of each thread before starting the use case */
	for (i = 0; i < nthreads; i++) {
		tdata = &opts.threads_data[i];

		tdata->duration = opts.duration;
		tdata->main_app_start = t_start;
		tdata->lock_pages = opts.lock_pages;

		if (opts.logdir) {
			snprintf(tmp, PATH_LENGTH, "%s/%s-%s-%d.log",
				 opts.logdir,
				 opts.logbasename,
				 tdata->name,
				 tdata->ind);
			tdata->log_handler = fopen(tmp, "w");
			if (!tdata->log_handler) {
				log_error("Cannot open logfile %s", tmp);
				exit(EXIT_FAILURE);
			}
		} else {
			tdata->log_handler = stdout;
		}
	}

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
			"plot ", tmp);

		for (i=0; i<nthreads; i++) {
			fprintf(gnuplot_script,
				"\"%s-%s-%d.log\" u ($5/1000):4 w l"
				" title \"thread [%s] (%s)\"",
				opts.logbasename, opts.threads_data[i].name,
				opts.threads_data[i].ind,
				opts.threads_data[i].name,
				policy_to_string(opts.threads_data[i].sched_data->policy));

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
			"plot ", tmp);

		for (i=0; i<nthreads; i++) {
			fprintf(gnuplot_script,
				"\"%s-%s-%d.log\" u ($5/1000):3 w l"
				" title \"thread [%s] (%s)\"",
				opts.logbasename, opts.threads_data[i].name,
				opts.threads_data[i].ind,
				opts.threads_data[i].name,
				policy_to_string(opts.threads_data[i].sched_data->policy));

			if ( i == nthreads-1)
				fprintf(gnuplot_script, "\n");
			else
				fprintf(gnuplot_script, ", ");
		}

		fprintf(gnuplot_script, "set terminal wxt\nreplot\n");
		fclose(gnuplot_script);

		/* gnuplot of each task */
		for (i=0; i<nthreads; i++) {
			snprintf(tmp, PATH_LENGTH, "%s/%s-%s-%d.plot",
				 opts.logdir, opts.logbasename, opts.threads_data[i].name, opts.threads_data[i].ind );
			gnuplot_script = fopen(tmp, "w+");
			snprintf(tmp, PATH_LENGTH, "%s-%s.eps",
				opts.logbasename, opts.threads_data[i].name);
			fprintf(gnuplot_script,
				"set terminal postscript enhanced color\n"
				"set output '%s'\n"
				"set grid\n"
				"set key outside right\n"
				"set title \"Measured %s Loop stats\"\n"
				"set xlabel \"Loop start time [msec]\"\n"
				"set ylabel \"Run Time [msec]\"\n"
				"set y2label \"Load [nb loop]\"\n"
				"set y2tics  \n"
				"plot ", tmp, opts.threads_data[i].name);

			fprintf(gnuplot_script,
				"\"%s-%s-%d.log\" u ($5/1000000):2 w l"
				" title \"load \" axes x1y2, ",
				opts.logbasename, opts.threads_data[i].name, opts.threads_data[i].ind);

			fprintf(gnuplot_script,
				"\"%s-%s-%d.log\" u ($5/1000000):3 w l"
				" title \"run \", ",
				opts.logbasename, opts.threads_data[i].name, opts.threads_data[i].ind);

			fprintf(gnuplot_script,
				"\"%s-%s-%d.log\" u ($5/1000000):4 w l"
				" title \"period \" ",
				opts.logbasename, opts.threads_data[i].name, opts.threads_data[i].ind);

			fprintf(gnuplot_script, "\n");

		fprintf(gnuplot_script, "set terminal wxt\nreplot\n");
		fclose(gnuplot_script);
		}

	}

	/* Sync timer resources with start time */
	clock_gettime(CLOCK_MONOTONIC, &t_start);

	/* Start the use case */
	for (i = 0; i < nthreads; i++) {
		tdata = &opts.threads_data[i];

		if (pthread_create(&threads[i],
				  NULL,
				  thread_body,
				  (void*) tdata))
			goto exit_err;
	}
	running_threads = nthreads;

	if (opts.duration > 0) {
		sleep(opts.duration);
		if (opts.ftrace)
			log_ftrace(ft_data.marker_fd, "main shutdown\n");
		shutdown(SIGTERM);
	}

	for (i = 0; i < nthreads; i++) {
		pthread_join(threads[i], NULL);
	}

	if (opts.ftrace) {
		log_ftrace(ft_data.marker_fd, "main ends\n");
		close(ft_data.marker_fd);
	}
	exit(EXIT_SUCCESS);


exit_err:
	exit(EXIT_FAILURE);
}
