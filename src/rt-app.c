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

#include <fcntl.h>
#include "rt-app.h"
#include "rt-app_utils.h"

static int errno;
static volatile int continue_running;
static pthread_t *threads;
static int nthreads;
rtapp_options_t opts;

static ftrace_data_t ft_data = {
	.debugfs = "/sys/kernel/debug",
	.trace_fd = -1,
	.marker_fd = -1,
};

static inline busywait(struct timespec *to)
{
	struct timespec t_step;
	while (1) {
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t_step);
		if (!timespec_lower(&t_step, to))
			break;
	}
}

void run(int ind, struct timespec *min, struct timespec *max,
	 rtapp_tasks_resource_list_t *blockages, int nblockages)
{
	int i;
	struct timespec t_start, now, t_exec, t_totexec = *max;
	rtapp_resource_access_list_t *lock, *last;

	/* Get the start time */
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t_start);

	for (i = 0; i < nblockages; i++)
	{

		/* Lock resources */
		lock = blockages[i].acl;
		while (lock != NULL) {
			log_debug("[%d] locking %d", ind, lock->res->index);
			if (opts.ftrace)
				log_ftrace(ft_data.marker_fd,
						"[%d] locking %d",
						ind, lock->res->index);
			pthread_mutex_lock(&lock->res->mtx);
			last = lock;
			lock = lock->next;
		}

		/* Busy wait */
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
		t_exec = timespec_add(&now, &blockages[i].usage);
		log_debug("[%d] busywait for %lu", ind, timespec_to_usec(&blockages[i].usage));
		if (opts.ftrace)
			log_ftrace(ft_data.marker_fd,
				   "[%d] busywait for %d",
				   ind, timespec_to_usec(&blockages[i].usage));
		busywait(&t_exec);

		/* Unlock resources */
		lock = last;
		while (lock != NULL) {
			log_debug("[%d] unlocking %d", ind, lock->res->index);
			if (opts.ftrace)
				log_ftrace(ft_data.marker_fd,
						"[%d] unlocking %d",
						ind, lock->res->index);
			pthread_mutex_unlock(&lock->res->mtx);
			lock = lock->prev;
		}
	}

	/* Compute finish time for CPUTIME_ID clock */
	t_exec = timespec_add(&t_start, &t_totexec);
	log_debug("[%d] busywait for %lu", ind, timespec_to_usec(&t_exec));
	if (opts.ftrace)
		log_ftrace(ft_data.marker_fd,
				"[%d] busywait for %d",
				ind, timespec_to_usec(&t_exec));
	busywait(&t_exec);
}

static void
shutdown(int sig)
{
	int i;
	// notify threads, join them, then exit
	continue_running = 0;
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
#ifdef AQUOSA
	qres_time_t prev_abs_used_budget = 0;
	qres_time_t abs_used_budget;
#endif
#ifdef DLSCHED
	pid_t tid;
	struct sched_attr attr;
	unsigned int flags = 0;
#endif
	int ret, i = 0;
	int j;

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

			/* add priority setting */

			log_notice("[%d] starting thread with period: %lu, exec: %lu,"
					"deadline: %lu",
					data->ind,
					timespec_to_usec(&data->period),
					timespec_to_usec(&data->min_et),
					timespec_to_usec(&data->deadline));

			data->lock_pages = 0; /* forced off for SCHED_OTHER */
			break;

#ifdef AQUOSA
		case aquosa:
			fprintf(data->log_handler, "# Policy : AQUOSA\n");
			data->params.Q_min = round((timespec_to_usec(&data->min_et)
						* (( 100.0 + data->sched_prio ) / 100)) / (data->fragment * 1.0));
			data->params.Q = round((timespec_to_usec(&data->max_et)
						* (( 100.0 + data->sched_prio ) / 100)) / (data->fragment * 1.0));
			data->params.P = round(timespec_to_usec(&data->period)
						/ (data->fragment * 1.0));
			data->params.flags = 0;

			log_notice("[%d] Creating QRES Server with Q=%ld, P=%ld",
					data->ind,data->params.Q, data->params.P);

			qos_chk_ok_exit(qres_init());
			qos_chk_ok_exit(qres_create_server(&data->params, 
					&data->sid));
			log_notice("[%d] AQuoSA server ID: %d", data->ind, data->sid);
			log_notice("[%d] attaching thread (deadline: %lu) to server %d",
					data->ind,
					timespec_to_usec(&data->deadline),
					data->sid);

			qos_chk_ok_exit(qres_attach_thread(data->sid, 0, 0));
		break;
#endif

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
			attr.sched_deadline = timespec_to_nsec(&data->period);
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

	if (data->wait_before_start > 0) {
		log_notice("[%d] Waiting %ld usecs... ", data->ind, 
			 data->wait_before_start);
		clock_gettime(CLOCK_MONOTONIC, &t_now);
		t_next = msec_to_timespec(data->wait_before_start);
		t_next = timespec_add(&t_now, &t_next);
		clock_nanosleep(CLOCK_MONOTONIC, 
				TIMER_ABSTIME, 
				&t_next,
				NULL);
		log_notice("[%d] Starting...", data->ind);
	}

	/* if we know the duration we can calculate how many periods we will
	 * do at most, and the log to memory, instead of logging to file.
	 */
	timings = NULL;
	if (data->duration > 0) {
		my_duration_usec = (data->duration * 10e6) - 
				(data->wait_before_start * 1000);
		nperiods = (int) ceil( my_duration_usec / 
				(double) timespec_to_usec(&data->period));
		timings = malloc ( nperiods * sizeof(timing_point_t));
	}

	fprintf(data->log_handler, "#idx\tperiod\tmin_et\tmax_et\trel_st\tstart"
				"\t\tend\t\tdeadline\tdur.\tslack"
				"\tBudget\tUsed Budget\n");

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

	while (continue_running) {
		struct timespec t_start, t_end, t_diff, t_slack;

		if (opts.ftrace)
			log_ftrace(ft_data.marker_fd, "[%d] begins loop %d", data->ind, i);

		clock_gettime(CLOCK_MONOTONIC, &t_start);
		run(data->ind, &data->min_et, &data->max_et, data->blockages,
		    data->nblockages);
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

#ifdef AQUOSA
		if (data->sched_policy == aquosa) {
			curr_timing->budget = data->params.Q;
			qres_get_exec_time(data->sid,
					&abs_used_budget,
					NULL);
			curr_timing->used_budget =
					abs_used_budget - prev_abs_used_budget;
			prev_abs_used_budget = abs_used_budget;

		} else {
			curr_timing->budget = 0;
			curr_timing->used_budget = 0;
		}
#endif
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
		if (timespec_lower(&t_now, &t_next))
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

#ifdef AQUOSA
	if (data->sched_policy == aquosa) {
		qres_destroy_server(data->sid);
		qres_cleanup();
	}
#endif

	pthread_exit(NULL);
}


int main(int argc, char* argv[])
{
	struct timespec t_curr, t_next, t_start;
	FILE *gnuplot_script = NULL;
	int i, res;
	thread_data_t *tdata;
	char tmp[PATH_LENGTH];

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

	/* Take the beginning time for everything */
	clock_gettime(CLOCK_MONOTONIC, &t_start);

	/* start threads */
	for (i = 0; i < nthreads; i++) {
		tdata = &opts.threads_data[i];
		if (opts.spacing > 0 ) {
			/* start the thread, then it will sleep accordingly
			 * to its position. We don't sleep here anymore as
			 * this would mean that
			 * duration = spacing * nthreads + duration */
			tdata->wait_before_start = opts.spacing * (i+1);
		} else {
			tdata->wait_before_start = 0;
		}

		tdata->duration = opts.duration;
		tdata->main_app_start = t_start;
		tdata->lock_pages = opts.lock_pages;
#ifdef AQUOSA
		tdata->fragment = opts.fragment;
#endif

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
