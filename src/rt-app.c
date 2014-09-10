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
 * TODO: improve the waste loop with more heavy functions
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

static inline loadwait(unsigned long exec)
{
	unsigned long load_count;

	load_count = (exec * 1000)/p_load;
	waste_cpu_cycles(load_count);

	return load_count;
}

static int run_event(event_data_t *event, int dry_run,
		unsigned long *perf, unsigned long *duration)
{
	unsigned long lock = 0;

	switch(event->type) {
	case rtapp_lock:
		log_debug("lock %s ", event->res->name);
		pthread_mutex_lock(&(event->res->res.mtx.obj));
		lock = 1;
		break;
	case rtapp_unlock:
		log_debug("unlock %s ", event->res->name);
		pthread_mutex_unlock(&(event->res->res.mtx.obj));
		lock = -1;
		break;
	}

	if (dry_run)
		return lock;

	switch(event->type) {
	case rtapp_wait:
		log_debug("wait %s ", event->res->name);
		pthread_cond_wait(&(event->res->res.cond.obj), &(event->dep->res.mtx.obj));
		break;
	case rtapp_signal:
		log_debug("signal %s ", event->res->name);
		pthread_cond_signal(event->res->res.signal.target);
		break;
	case rtapp_sig_and_wait:
		log_debug("signal and wait %s", event->res->name);
		pthread_cond_signal(event->res->res.signal.target);
		pthread_cond_wait(event->res->res.signal.target, &(event->dep->res.mtx.obj));
		break;
	case rtapp_broadcast:
		pthread_cond_broadcast(event->res->res.signal.target);
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
			clock_gettime(CLOCK_MONOTONIC, &t_start);
			*perf += loadwait(event->duration);
			clock_gettime(CLOCK_MONOTONIC, &t_end);
			t_end = timespec_sub(&t_end, &t_start);
			*duration += timespec_to_usec(&t_end);
		}
		break;
		break;
	}

	return lock;
}

int run(int ind, event_data_t *events,
		int nbevents, unsigned long *duration)
{
	int i, lock = 0;
	unsigned long perf = 0;

	for (i = 0; i < nbevents; i++)
	{
		if (!continue_running && !lock)
			return;

		log_debug("[%d] runs events %d type %d ", ind, i, events[i].type);
		if (opts.ftrace)
				log_ftrace(ft_data.marker_fd,
						"[%d] locking %d",
						ind, events[i].type);
		lock += run_event(&events[i], !continue_running, &perf, duration);
	}

	return perf;
}

static void
shutdown(int sig)
{
	int i;
	/* notify threads, join them, then exit */
	continue_running = 0;

	/* Force wake up of all waiting threads */
	for (i = 0; i <  opts.nresources; i++) {
		switch (opts.resources[i].type) {
		case rtapp_sig_and_wait:
		case rtapp_signal:
		case rtapp_broadcast:
			pthread_cond_broadcast(opts.resources[i].res.signal.target);
		}
	}

	/* wait up all waiting threads */
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
	phase_data_t *pdata;
	struct sched_param param;
	struct timespec t_start, t_end;
	unsigned long t_start_usec;
	unsigned long perf, duration;
	timing_point_t *curr_timing;
	timing_point_t *timings;
	timing_point_t tmp_timing;
	pid_t tid;
	struct sched_attr attr;
	unsigned int flags = 0;
	int ret, i, j, loop;

	/* Set thread name */
	ret = pthread_setname_np(pthread_self(), data->name);
	if (ret !=  0) {
		perror("pthread_setname_np thread name over 16 characters");
	}

	/* Get the 1st phase's data */
	pdata = &data->phases[0];

	/* Set thread affinity */
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

	/* Set scheduling policy and print pretty info on stdout */
	log_notice("[%d] Using %s policy with priority %d", data->ind, data->sched_policy_descr, data->sched_prio);
	switch (data->sched_policy)
	{
		case rr:
		case fifo:
			fprintf(data->log_handler, "# Policy : %s priority : %d\n",
					(data->sched_policy == rr ? "SCHED_RR" : "SCHED_FIFO"), data->sched_prio);
			param.sched_priority = data->sched_prio;
			ret = pthread_setschedparam(pthread_self(),
					data->sched_policy,
					&param);
			if (ret != 0) {
				errno = ret;
				perror("pthread_setschedparam");
				exit(EXIT_FAILURE);
			}
			break;

		case other:
			fprintf(data->log_handler, "# Policy : SCHED_OTHER priority : %d\n", data->sched_prio);

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
		break;
#endif

		default:
			log_error("Unknown scheduling policy %d",
					data->sched_policy);

			exit(EXIT_FAILURE);
	}

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

	log_notice("[%d] starting thread ...\n", data->ind);

	timings = NULL;

	fprintf(data->log_handler, "#idx\tperf\trun\tperiod\tstart\t\tend\t\trel_st\n");

	if (opts.ftrace)
		log_ftrace(ft_data.marker_fd, "[%d] starts", data->ind);

#ifdef DLSCHED
	/* TODO find a better way to handle that constraint */
	/*
	 * Set the task to SCHED_DEADLINE as far as possible touching its
	 * budget as little as possible for the first iteration.
	 */
	if (data->sched_policy == SCHED_DEADLINE) {
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
	i = j = loop = 0;

	while (continue_running && (i != data->loop)) {
		struct timespec t_diff;

		if (opts.ftrace)
			log_ftrace(ft_data.marker_fd, "[%d] begins loop %d phase %d step %d", data->ind, i, j, loop);
		log_debug("[%d] begins loop %d phase %d step %d", data->ind, i, j, loop);;

		duration = 0;
		clock_gettime(CLOCK_MONOTONIC, &t_start);
		perf = run(data->ind, pdata->events, pdata->nbevents, &duration);
		clock_gettime(CLOCK_MONOTONIC, &t_end);

		if (timings)
			curr_timing = &timings[loop];
		else
			curr_timing = &tmp_timing;

		t_diff = timespec_sub(&t_end, &t_start);

		t_start_usec = timespec_to_usec(&t_start);

		curr_timing->ind = data->ind;
		curr_timing->rel_start_time =
			t_start_usec - timespec_to_usec(&data->main_app_start);
		curr_timing->start_time = t_start_usec;
		curr_timing->end_time = timespec_to_usec(&t_end);
		curr_timing->period = timespec_to_usec(&t_diff);
		curr_timing->duration = duration;
		curr_timing->perf = perf;

		if (!timings)
			log_timing(data->log_handler, curr_timing);

		if (opts.ftrace)
			log_ftrace(ft_data.marker_fd, "[%d] end loop %d phase %d step %d",
				   data->ind, i, j, loop);

		loop++;
		if (loop == pdata->loop) {
			loop = 0;

			j++;
			if (j == data->nphases) {
				j = 0;
				i++;
			}

			pdata = &data->phases[j];
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

	if (timings)
		for (j=0; j < loop; j++)
			log_timing(data->log_handler, &timings[j]);

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

	/* Needs to calibrate 'calib_cpu' core */
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

	/* Prepare log file of each thread before starting the use case */
	for (i = 0; i < nthreads; i++) {
		tdata = &opts.threads_data[i];

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
				"\"%s-%s.log\" u ($5/1000):4 w l"
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
				"\"%s-%s.log\" u ($5/1000):3 w l"
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
	}

	/* Start the use case */
	for (i = 0; i < nthreads; i++) {
		tdata = &opts.threads_data[i];

		if (pthread_create(&threads[i],
				  NULL,
				  thread_body,
				  (void*) tdata))
			goto exit_err;
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
