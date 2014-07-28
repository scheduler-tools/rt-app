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

#define _GNU_SOURCE
#include <fcntl.h>
#include "rt-app.h"
#include "rt-app_utils.h"
#include <sched.h>
#include "pthread.h"
#include <sys/time.h>
#include <sys/resource.h>

static int errno;
static volatile int continue_running;
static pthread_t *threads;
static int nthreads;
static int p_load;
rtapp_options_t opts;

static ftrace_data_t ft_data = {
	.debugfs = "/sys/kernel/debug",
	.trace_fd = -1,
	.marker_fd = -1,
};

/*
 * Function: to do some useless operation.
 *                      TODO: improve the waste loop with more heavy functions
 */
void waste_cpu_cycles(int load_loops)
{
	double param, result;
	double n, i;

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
* calibrate_cpu_cycles()
* collects the time that waste_cycles runs.
*/
int calibrate_cpu_cycles(int clock)
{
	struct timespec start, stop;
	int max_load_loop = 10000;
	unsigned int diff;
	int nsec_per_loop, avg_per_loop = 0;
	int ret, cal_trial = 1000;

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

static inline loadwait(struct timespec *exec_time)
{
	unsigned long exec, load_count;

	exec = timespec_to_usec(exec_time);
	load_count = (exec * 1000)/p_load;
	waste_cpu_cycles(load_count);
}

static inline busywait(struct timespec *to)
{
	struct timespec t_step;
	while (1) {
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t_step);
		if (!timespec_lower(&t_step, to))
			break;
	}
}

int get_resource(rtapp_resource_access_list_t *lock, struct timespec *usage)
{
	int busywait = 1;
	rtapp_resource_access_list_t *prev;

	switch(lock->res->type) {
	case rtapp_mutex:
		pthread_mutex_lock(&(lock->res->res.mtx.obj));
		break;
	case rtapp_wait:
		prev = lock->prev;
		pthread_cond_wait(&(lock->res->res.cond.obj), &(prev->res->res.mtx.obj));
		break;
	case rtapp_signal:
		pthread_cond_signal(lock->res->res.signal.target);
		break;
	case rtapp_sig_and_wait:
		pthread_cond_signal(lock->res->res.signal.target);
		prev = lock->prev;
		pthread_cond_wait(lock->res->res.signal.target, &(prev->res->res.mtx.obj));
		break;
	case rtapp_broadcast:
		pthread_cond_broadcast(lock->res->res.signal.target);
		break;
	case rtapp_sleep:
		nanosleep(usage, NULL);
		busywait = 0;
		break;
	}

	return busywait;
}

void put_resource(rtapp_resource_access_list_t *lock)
{
	if (lock->res->type == rtapp_mutex)
		pthread_mutex_unlock(&(lock->res->res.mtx.obj));
}

void run(int ind, struct timespec *min, struct timespec *max,
	 rtapp_tasks_resource_list_t *blockages, int nblockages, struct timespec *t_start)
{
	int i, busywait = 1;
	struct timespec t_exec;
	rtapp_resource_access_list_t *lock, *last;

	t_exec = *min;

	for (i = 0; i < nblockages; i++)
	{
		/* Lock resources sequence including the busy wait */
		lock = blockages[i].acl;
		while (lock != NULL && continue_running) {
			log_debug("[%d] locking %d", ind, lock->res->index);
			if (opts.ftrace)
				log_ftrace(ft_data.marker_fd,
						"[%d] locking %d",
						ind, lock->res->index);
			busywait = get_resource(lock, &blockages[i].usage);
			last = lock;
			lock = lock->next;
		}

		if (!i && t_start)
			clock_gettime(CLOCK_MONOTONIC, t_start);

		if (busywait) {
			/* Busy wait */
			log_debug("[%d] busywait for %lu", ind, timespec_to_usec(&blockages[i].usage));
			if (opts.ftrace)
				log_ftrace(ft_data.marker_fd,
					   "[%d] busywait for %d",
					   ind, timespec_to_usec(&blockages[i].usage));
			loadwait(&blockages[i].usage);
			t_exec = timespec_sub(&t_exec, &blockages[i].usage);
		}

		/* Unlock resources */
		lock = last;
		while (lock != NULL) {
			log_debug("[%d] unlocking %d", ind, lock->res->index);
			if (opts.ftrace)
				log_ftrace(ft_data.marker_fd,
						"[%d] unlocking %d",
						ind, lock->res->index);
			put_resource(lock);
			lock = lock->prev;
		}
	}

	/* Compute finish time for CPUTIME_ID clock */
	log_debug("[%d] busywait for %lu", ind, timespec_to_usec(&t_exec));
	if (opts.ftrace)
		log_ftrace(ft_data.marker_fd,
				"[%d] busywait for %d",
				ind, timespec_to_usec(&t_exec));
	loadwait(&t_exec);
}

static void
shutdown(int sig)
{
	int i;
	// notify threads, join them, then exit
	continue_running = 0;

	for (i = 0; i <  opts.nresources; i++) {
		switch (opts.resources[i].type) {
		case rtapp_sig_and_wait:
		case rtapp_signal:
		case rtapp_broadcast:
			pthread_cond_broadcast(opts.resources[i].res.signal.target);
		}
	}

	// wait up all waiting threads
	for (i = 0; i < nthreads; i++)
	{
		pthread_join(threads[i], NULL);
	}

	if (opts.ftrace) {
		log_notice("stopping ftrace");
		log_ftrace(ft_data.marker_fd, "main ends\n");
		log_ftrace(ft_data.trace_fd, "0");
		close(ft_data.trace_fd);
		close(ft_data.marker_fd);
	}

	exit(EXIT_SUCCESS);
}

void *thread_body(void *arg)
{
	thread_data_t *data = (thread_data_t*) arg;
	struct sched_param param;
	struct timespec t_now, t_next;
	unsigned long t_start_usec;
	unsigned long my_duration_usec;
	int nperiods;
	timing_point_t *timings;
	timing_point_t tmp_timing;
	timing_point_t *curr_timing;
	pid_t tid;
	struct sched_attr attr;
	unsigned int flags = 0;
	int ret, i = 0;
	int j;

	/* set thread name */
	ret = pthread_setname_np(pthread_self(), data->name);
	if (ret !=  0) {
		perror("pthread_setname_np thread name over 16 characters");
	}

	/* set thread affinity */
	if (data->cpuset != NULL)
	{
		log_notice("[%d] setting cpu affinity to CPU(s) %s", data->ind,
			 data->cpuset_str);
		ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t),
						data->cpuset);
		if (ret < 0) {
			errno = ret;
			perror("pthread_setaffinity_np");
			exit(EXIT_FAILURE);
		}
	}

	/* set scheduling policy and print pretty info on stdout */
	log_notice("[%d] Using %s policy:", data->ind, data->sched_policy_descr);
	switch (data->sched_policy)
	{
		case rr:
		case fifo:
			fprintf(data->log_handler, "# Policy : %s\n",
					(data->sched_policy == rr ? "SCHED_RR" : "SCHED_FIFO"));
			param.sched_priority = data->sched_prio;
			ret = pthread_setschedparam(pthread_self(),
					data->sched_policy,
					&param);
			if (ret != 0) {
				errno = ret;
				perror("pthread_setschedparam");
				exit(EXIT_FAILURE);
			}

			log_notice("[%d] starting thread with period: %lu, exec: %lu,"
					"deadline: %lu, priority: %d",
					data->ind,
					timespec_to_usec(&data->period),
					timespec_to_usec(&data->min_et),
					timespec_to_usec(&data->deadline),
					data->sched_prio);
			break;

		case other:
			fprintf(data->log_handler, "# Policy : SCHED_OTHER\n");

			if (data->sched_prio > 19 || data->sched_prio < -20) {
				log_critical("[%d] setpriority "
					"%d nice invalid. "
					"Valid between -20 and 19",
					data->ind, data->sched_prio);
				exit(EXIT_FAILURE);
			}

			if (data->sched_prio) {
				ret = setpriority(PRIO_PROCESS, 0,
						data->sched_prio);
				if (ret != 0) {
					log_critical("[%d] setpriority"
					     "returned %d", data->ind, ret);
					errno = ret;
					perror("setpriority");
					exit(EXIT_FAILURE);
				}
			}

			log_notice("[%d] starting thread with period: %lu, exec: %lu,"
					"deadline: %lu, nice: %d",
					data->ind,
					timespec_to_usec(&data->period),
					timespec_to_usec(&data->min_et),
					timespec_to_usec(&data->deadline),
					data->sched_prio);

			data->lock_pages = 0; /* forced off for SCHED_OTHER */
			break;

#ifdef DLSCHED
		case deadline:
			fprintf(data->log_handler, "# Policy : SCHED_DEADLINE\n");
			tid = gettid();
			attr.size = sizeof(attr);
			attr.sched_flags = 0;
			attr.sched_policy = SCHED_DEADLINE;
			attr.sched_priority = 0;
			attr.sched_runtime = timespec_to_nsec(&data->max_et) +
					(timespec_to_nsec(&data->max_et) /100) * BUDGET_OVERP;
			attr.sched_deadline = timespec_to_nsec(&data->deadline);
			attr.sched_period = timespec_to_nsec(&data->period);
		break;
#endif

		default:
			log_error("Unknown scheduling policy %d",
					data->sched_policy);

			exit(EXIT_FAILURE);
	}

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

	/* if we know the duration we can calculate how many periods we will
	 * do at most, and log to memory, instead of logging to file.
	 */
	nperiods = 0;
	if ((data->duration > 0) && (data->duration > data->wait_before_start)) {
		my_duration_usec = (data->duration * 1000000) - data->wait_before_start;
		nperiods = (my_duration_usec + timespec_to_usec(&data->period) - 1)  / timespec_to_usec(&data->period) + 1;
	}

	if ((data->loop > 0)  && (data->loop < nperiods)) {
		nperiods = data->loop + 1 ;
	}

	if (nperiods)
		timings = malloc ( nperiods * sizeof(timing_point_t));
	else
		timings = NULL;

	fprintf(data->log_handler, "#idx\tperiod\tmin_et\tmax_et\trel_st\tstart"
				"\t\tend\t\tdeadline\tdur.\tslack"
				"\tBudget\tUsed Budget\n");


	if (data->wait_before_start > 0) {
		log_notice("[%d] Waiting %ld usecs... ", data->ind,
			 data->wait_before_start);
		clock_gettime(CLOCK_MONOTONIC, &t_now);
		t_next = usec_to_timespec(data->wait_before_start);
		t_next = timespec_add(&t_now, &t_next);
		clock_nanosleep(CLOCK_MONOTONIC,
				TIMER_ABSTIME,
				&t_next,
				NULL);
		log_notice("[%d] Starting...", data->ind);
	}

#ifdef DLSCHED
	/* TODO find a better way to handle that constraint */
	/*
	 * Set the task to SCHED_DEADLINE as far as possible touching its
	 * budget as little as possible for the first iteration.
	 */
	if (data->sched_policy == SCHED_DEADLINE) {
		log_notice("[%d] starting thread with period: %llu, exec: %llu,"
				"deadline: %llu, priority: %d",
				data->ind,
				attr.sched_period / 1000,
				attr.sched_runtime / 1000,
				attr.sched_deadline / 1000,
				attr.sched_priority);

		ret = sched_setattr(tid, &attr, flags);
		if (ret != 0) {
			log_critical("[%d] sched_setattr "
				     "returned %d", data->ind, ret);
			errno = ret;
			perror("sched_setattr");
			exit(EXIT_FAILURE);
		}
	}
#endif

	if (opts.ftrace)
		log_ftrace(ft_data.marker_fd, "[%d] starts", data->ind);

	clock_gettime(CLOCK_MONOTONIC, &t_now);
	t_next = t_now;
	data->deadline = timespec_add(&t_now, &data->deadline);

	while (continue_running &&  (i != data->loop)) {
		struct timespec t_start, t_end, t_diff, t_slack;

		if (opts.ftrace)
			log_ftrace(ft_data.marker_fd, "[%d] begins loop %d", data->ind, i);

		clock_gettime(CLOCK_MONOTONIC, &t_start);
		run(data->ind, &data->min_et, &data->max_et, data->blockages,
		    data->nblockages, data->sleep ? NULL: &t_start);
		clock_gettime(CLOCK_MONOTONIC, &t_end);

		t_diff = timespec_sub(&t_end, &t_start);
		t_slack = timespec_sub(&data->deadline, &t_end);

		t_start_usec = timespec_to_usec(&t_start);
		if (timings)
			curr_timing = &timings[i];
		else
			curr_timing = &tmp_timing;

		curr_timing->ind = data->ind;
		curr_timing->period = timespec_to_usec(&data->period);
		curr_timing->min_et = timespec_to_usec(&data->min_et);
		curr_timing->max_et = timespec_to_usec(&data->max_et);
		curr_timing->rel_start_time =
			t_start_usec - timespec_to_usec(&data->main_app_start);
		curr_timing->abs_start_time = t_start_usec;
		curr_timing->end_time = timespec_to_usec(&t_end);
		curr_timing->deadline = timespec_to_usec(&data->deadline);
		curr_timing->duration = timespec_to_usec(&t_diff);
		curr_timing->slack =  timespec_to_lusec(&t_slack);


		if (!timings)
			log_timing(data->log_handler, curr_timing);

		t_next = timespec_add(&t_next, &data->period);
		data->deadline = timespec_add(&data->deadline, &data->period);

		if (opts.ftrace)
			log_ftrace(ft_data.marker_fd, "[%d] end loop %d",
				   data->ind, i);

		if (curr_timing->slack < 0 && opts.die_on_dmiss) {
			log_critical("[%d] DEADLINE MISS !!!", data->ind);
			if (opts.ftrace)
				log_ftrace(ft_data.marker_fd,
					   "[%d] DEADLINE MISS!!", data->ind);
			shutdown(SIGTERM);
			goto exit_miss;
		}

		clock_gettime(CLOCK_MONOTONIC, &t_now);
		if (data->sleep && timespec_lower(&t_now, &t_next))
			clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t_next, NULL);

		i++;
	}

exit_miss:
	param.sched_priority = 0;
	ret = pthread_setschedparam(pthread_self(),
				    SCHED_OTHER,
				    &param);
	if (ret != 0) {
		errno = ret;
		perror("pthread_setschedparam");
		exit(EXIT_FAILURE);
	}

	if (timings)
		for (j=0; j < i; j++)
			log_timing(data->log_handler, &timings[j]);

	if (opts.ftrace)
		log_ftrace(ft_data.marker_fd, "[%d] exiting", data->ind);
	log_notice("[%d] Exiting.", data->ind);
	fclose(data->log_handler);

	pthread_exit(NULL);
}


int main(int argc, char* argv[])
{
	struct timespec t_curr, t_next, t_start;
	FILE *gnuplot_script = NULL;
	int i, res;
	thread_data_t *tdata;
	char tmp[PATH_LENGTH];
	static cpu_set_t orig_set;

	parse_command_line(argc, argv, &opts);

	/* allocated threads */
	nthreads = opts.nthreads;
	threads = malloc(nthreads * sizeof(pthread_t));

	/* install a signal handler for proper shutdown */
	signal(SIGQUIT, shutdown);
	signal(SIGTERM, shutdown);
	signal(SIGHUP, shutdown);
	signal(SIGINT, shutdown);

	/* if using ftrace, open trace and marker fds */
	if (opts.ftrace) {
		log_notice("configuring ftrace");
		strcpy(tmp, ft_data.debugfs);
		strcat(tmp, "/tracing/tracing_on");
		ft_data.trace_fd = open(tmp, O_WRONLY);
		if (ft_data.trace_fd < 0) {
			log_error("Cannot open trace_fd file %s", tmp);
			exit(EXIT_FAILURE);
		}

		strcpy(tmp, ft_data.debugfs);
		strcat(tmp, "/tracing/trace_marker");
		ft_data.marker_fd = open(tmp, O_WRONLY);
		if (ft_data.trace_fd < 0) {
			log_error("Cannot open trace_marker file %s", tmp);
			exit(EXIT_FAILURE);
		}

		log_ftrace(ft_data.trace_fd, "1");
		log_ftrace(ft_data.marker_fd, "main creates threads\n");
	}

	continue_running = 1;

	/*Needs to calibrate 'calib_cpu' core*/
	if (opts.calib_ns_per_loop == 0) {
		cpu_set_t calib_set;

		CPU_ZERO(&calib_set);
		CPU_SET(opts.calib_cpu, &calib_set);
		sched_getaffinity(0, sizeof(cpu_set_t), &orig_set);
		sched_setaffinity(0, sizeof(cpu_set_t), &calib_set);
		p_load = calibrate_cpu_cycles(CLOCK_THREAD_CPUTIME_ID);
		sched_setaffinity(0, sizeof(cpu_set_t), &orig_set);
		log_notice("pLoad = %dns : calib_cpu %d", p_load, opts.calib_cpu);
	} else {
		p_load = opts.calib_ns_per_loop;
		log_notice("pLoad = %dns", p_load);
	}

	/* Take the beginning time for everything */
	clock_gettime(CLOCK_MONOTONIC, &t_start);

	/* start threads */
	for (i = 0; i < nthreads; i++) {
		tdata = &opts.threads_data[i];
		if (!tdata->wait_before_start && (opts.spacing > 0)) {
			/* start the thread, then it will sleep accordingly
			 * to its position. We don't sleep here anymore as
			 * this would mean that
			 * duration = spacing * nthreads + duration */
			tdata->wait_before_start = opts.spacing * 1000 * (i+1);
		}

		tdata->duration = opts.duration;
		tdata->main_app_start = t_start;
		tdata->lock_pages = opts.lock_pages;

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

		if (pthread_create(&threads[i],
				  NULL,
				  thread_body,
				  (void*) tdata))
			goto exit_err;
	}

	/* print gnuplot files */
	if (opts.logdir && opts.gnuplot) {
		snprintf(tmp, PATH_LENGTH, "%s/%s-duration.plot",
			 opts.logdir, opts.logbasename);
		gnuplot_script = fopen(tmp, "w+");
		snprintf(tmp, PATH_LENGTH, "%s-duration.eps",
			 opts.logbasename);
		fprintf(gnuplot_script,
			"set terminal postscript enhanced color\n"
			"set output '%s'\n"
			"set grid\n"
			"set key outside right\n"
			"set title \"Measured exec time per period\"\n"
			"set xlabel \"Cycle start time [usec]\"\n"
			"set ylabel \"Exec Time [usec]\"\n"
			"plot ", tmp);

		for (i=0; i<nthreads; i++) {
			snprintf(tmp, PATH_LENGTH, "%s/%s-duration.plot",
				 opts.logdir, opts.logbasename);

			fprintf(gnuplot_script,
				"\"%s-%s.log\" u ($5/1000):9 w l"
				" title \"thread [%s] (%s)\"",
				opts.logbasename, opts.threads_data[i].name,
				opts.threads_data[i].name,
				opts.threads_data[i].sched_policy_descr);

			if ( i == nthreads-1)
				fprintf(gnuplot_script, "\n");
			else
				fprintf(gnuplot_script, ", ");
		}

		fprintf(gnuplot_script, "set terminal wxt\nreplot\n");
		fclose(gnuplot_script);

		snprintf(tmp, PATH_LENGTH, "%s/%s-slack.plot",
			 opts.logdir, opts.logbasename);
		gnuplot_script = fopen(tmp, "w+");
		snprintf(tmp, PATH_LENGTH, "%s-slack.eps",
			 opts.logbasename);

		fprintf(gnuplot_script,
			"set terminal postscript enhanced color\n"
			"set output '%s'\n"
			"set grid\n"
			"set key outside right\n"
			"set title \"Slack (negative = tardiness)\"\n"
			"set xlabel \"Cycle start time [msec]\"\n"
			"set ylabel \"Slack/Tardiness [usec]\"\n"
			"plot ", tmp);

		for (i=0; i < nthreads; i++) {
			fprintf(gnuplot_script,
				"\"%s-%s.log\" u ($5/1000):10 w l"
				" title \"thread [%s] (%s)\"",
				opts.logbasename, opts.threads_data[i].name,
				opts.threads_data[i].name,
				opts.threads_data[i].sched_policy_descr);

			if ( i == nthreads-1)
				fprintf(gnuplot_script, ", 0 notitle\n");
			else
				fprintf(gnuplot_script, ", ");

		}

		fprintf(gnuplot_script, "set terminal wxt\nreplot\n");
		fclose(gnuplot_script);
	}

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
		log_notice("stopping ftrace");
		log_ftrace(ft_data.marker_fd, "main ends\n");
		log_ftrace(ft_data.trace_fd, "0");
		close(ft_data.trace_fd);
		close(ft_data.marker_fd);
	}
	exit(EXIT_SUCCESS);


exit_err:
	exit(EXIT_FAILURE);
}
