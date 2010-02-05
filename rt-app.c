#include "rt-app.h"

#define handle_error(en, msg) \
	do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

static int errno;
static int continue_running;
static pthread_t *threads;
static int nthreads;

static inline
unsigned int timespec_to_msec(struct timespec *ts)
{
	return (ts->tv_sec * 1E9 + ts->tv_nsec) / 1000000;
}

static inline
long timespec_to_lusec(struct timespec *ts)
{
	return round((ts->tv_sec * 1E9 + ts->tv_nsec) / 1000.0);
}

static inline
unsigned long timespec_to_usec(struct timespec *ts)
{
	return round((ts->tv_sec * 1E9 + ts->tv_nsec) / 1000.0);
}

static inline
struct timespec usec_to_timespec(unsigned long usec)
{
	struct timespec ts;

	ts.tv_sec = usec / 1000000;
	ts.tv_nsec = (usec % 1000000) * 1000;
	
	return ts;
}

static inline
struct timespec msec_to_timespec(unsigned int msec)
{
	struct timespec ts;

	ts.tv_sec = msec / 1000;
	ts.tv_nsec = (msec % 1000) * 1000000;

	return ts;
}

static inline
struct timespec timespec_add(struct timespec *t1, struct timespec *t2)
{
	struct timespec ts;

	ts.tv_sec = t1->tv_sec + t2->tv_sec;
	ts.tv_nsec = t1->tv_nsec + t2->tv_nsec;

	while (ts.tv_nsec >= 1E9) {
		ts.tv_nsec -= 1E9;
		ts.tv_sec++;
	}

	return ts;
}

static inline
struct timespec timespec_sub(struct timespec *t1, struct timespec *t2)
{
	struct timespec ts;
	
	if (t1->tv_nsec < t2->tv_nsec) {
		ts.tv_sec = t1->tv_sec - t2->tv_sec -1;
		ts.tv_nsec = t1->tv_nsec  + 1000000000 - t2->tv_nsec; 
	} else {
		ts.tv_sec = t1->tv_sec - t2->tv_sec;
		ts.tv_nsec = t1->tv_nsec - t2->tv_nsec; 
	}

	return ts;

}

static inline
int timespec_lower(struct timespec *what, struct timespec *than)
{
	if (what->tv_sec > than->tv_sec)
		return 0;

	if (what->tv_sec < than->tv_sec)
		return 1;

	if (what->tv_nsec < than->tv_nsec)
		return 1;

	return 0;
}

static inline
unsigned int max_run(int min, int max)
{
        return min + (((double) rand()) / RAND_MAX) * (max - min);
}

void run(int ind, struct timespec *min, struct timespec *max)
{
	//int m = max_run(timespec_to_msec(min), timespec_to_msec(max));
	//struct timespec t_start, t_step, t_exec = msec_to_timespec(m);
	struct timespec t_start, t_step, t_exec = *max;

	/* get the start time */
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t_start);
	/* compute finish time for CPUTIME_ID clock */
	t_exec = timespec_add(&t_start, &t_exec);

	while (1) {
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t_step);
		if (!timespec_lower(&t_step, &t_exec))
			break;
	}
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
	exit(EXIT_SUCCESS);
}

void *thread_body(void *arg)
{
	struct thread_data *data = (struct thread_data*) arg;
	struct sched_param param;
	struct timespec t, t_next;
	unsigned long t_start_usec;
	int ret, i = 0;
	printf("Thread %d started with period: %ld, exec: %ld,"
	       "deadline: %ld, priority: %d\n",
		data->ind, 
		timespec_to_usec(&data->period), 
		timespec_to_usec(&data->min_et),
		timespec_to_usec(&data->deadline),
		data->sched_prio
	);

	clock_gettime(CLOCK_MONOTONIC, &t);
	switch (data->sched_policy)
	{
		case rr:
			printf("Setting POSIX scheduling class: SCHED_RR");
			goto posixrtcommon;
		case fifo:
			printf("Setting POSIX scheduling class: SCHED_FIFO");
posixrtcommon:			
			printf(" prio: %d\n", data->sched_prio);
			param.sched_priority = data->sched_prio;
			ret = pthread_setschedparam(pthread_self(), 
						    data->sched_policy, 
						    &param);
			if (ret != 0)
				handle_error(ret, "pthread_setschedparam");
			break;
		case other:
			printf("[%d] Using SCHED_OTHER policy\n", data->ind);
			break;
#ifdef AQUOSA			
		case aquosa:
			data->params.Q_min = round((timespec_to_usec(&data->min_et) * (( 100.0 + BUDGET_PERC_INCR ) / 100)) / (data->fragment * 1.0)); 
			data->params.Q = round((timespec_to_usec(&data->max_et) * (( 100.0 + BUDGET_PERC_INCR ) / 100)) / (data->fragment * 1.0));
			data->params.P = round(timespec_to_usec(&data->period) / (data->fragment * 1.0));
			data->params.flags = 0;
			printf("Creating QRES Server with Q=%ld, P=%ld\n",
				data->params.Q, data->params.P);
			qos_chk_ok_exit(qres_init());
			qos_chk_ok_exit(qres_create_server(&data->params, 
							   &data->sid));
			qos_chk_ok_exit(qres_attach_thread(data->sid, 0, 0));

			break;
#endif
		default:
			printf("Unknown scheduling policy %d\n",
				data->sched_policy);
			exit(EXIT_FAILURE);
	}
		
	t_next = t;
	data->deadline = timespec_add(&t, &data->deadline);

	fprintf(data->log_handler, "#idx\tperiod\tmin_et\tmax_et\trel_st\tstart"
				   "\t\tend\t\tdeadline\tdur.\tslack\n");
	while (continue_running) {
		struct timespec t_start, t_end, t_diff, t_slack;

		clock_gettime(CLOCK_MONOTONIC, &t_start);
		run(data->ind, &data->min_et, &data->max_et);
		clock_gettime(CLOCK_MONOTONIC, &t_end);
		
		t_diff = timespec_sub(&t_end, &t_start);
		t_slack = timespec_sub(&data->deadline, &t_end);

		t_start_usec = timespec_to_usec(&t_start); 
			
		fprintf(data->log_handler, 
			"%d\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\t%ld\n",
			data->ind,
			timespec_to_usec(&data->period),
			timespec_to_usec(&data->min_et),
			timespec_to_usec(&data->max_et),
			t_start_usec - timespec_to_usec(&data->main_app_start),
			t_start_usec,
			timespec_to_usec(&t_end),
			timespec_to_usec(&data->deadline),
			timespec_to_usec(&t_diff),
			timespec_to_lusec(&t_slack)
		);

		t_next = timespec_add(&t_next, &data->period);
		data->deadline = timespec_add(&data->deadline, &data->period);
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t_next, NULL);
		i++;
	}
	printf("[%d] Exiting.\n", data->ind);
	fclose(data->log_handler);

	pthread_exit(NULL);
}

void
usage (const char* msg)
{
	printf("usage: rt-app [options] <period>:<exec>[:<deadline>[:prio]] ...\n");
	printf("-h, --help\t:\tshow this help\n");
	printf("-f, --fifo\t:\trun with SCHED_FIFO policy\n");
	printf("-r, --rr\t:\tuse SCHED_RR policy\n");
	printf("-s, --spacing\t:\tmsec to wait beetween thread starts\n");
	printf("-l, --logdir\t:\tsave logs to different directory\n");
	printf("-b, --baselog\t:\tbasename for logs (implies -l . if not set)\n");
	printf("-G, --gnuplot\t:\tgenerate gnuplot script (needs -l)\n");
	printf("-D, --duration\t:\ttime (in seconds) before stopping threads\n");
	
#ifdef AQUOSA
	printf("-q, --qos\t:\tcreate AQuoSA reservation\n");
	printf("-g, --frag\t:\tfragment for the reservation\n");
#endif

	if (msg != NULL)
		printf("\n%s\n", msg);
	exit(0);
}

/* parse a thread token in the form  $period:$exec:$deadline:$prio and
 * fills the thread_data structure
 */
void
parse_thread_args(char *arg, struct thread_data *tdata, policy_t def_policy)
{
	char *str = strdup(arg);
	char *token;
	long period, exec, deadline;
	int i = 0;
	token = strtok(str, ":");
	while ( token != NULL)
	{
		switch(i) {
		case 0:
			period = strtol(token, NULL, 10);
			if (period <= 0 )
				usage("Cannot set negative period.");
			tdata->period = usec_to_timespec(period);
			i++;
			break;

		case 1:
			exec = strtol(token,NULL, 10);
			//TODO: add support for max_et somehow
			if (exec >= period)
				usage("Exec time cannot be greater than"
				      " period.");
			if (exec <= 0 )
				usage("Cannot set negative exec time");
			tdata->min_et = usec_to_timespec(exec);
			tdata->max_et = usec_to_timespec(exec);
			i++;
			break;

		case 2:
			deadline = strtol(token, NULL, 10);
			if (deadline < exec)
				usage ("Deadline cannot be less than "
						"execution time");
			if (deadline > period)
				usage ("Deadline cannot be greater than "
						"period");
			if (deadline <= 0 )
				usage ("Cannot set negative deadline");
			tdata->deadline = usec_to_timespec(deadline);
			i++;
			break;
		case 3:
			tdata->sched_prio = strtol(token, NULL, 10);
			// do not check, will fail in pthread_setschedparam
			i++;
			break;
		case 4:
			if (strcmp(token,"q") == 0)
				tdata->sched_policy = aquosa;
			else if (strcmp(token,"f") == 0)
				tdata->sched_policy = fifo;
			else if (strcmp(token,"r") == 0)
				tdata->sched_policy = rr ;
			else if (strcmp(token,"o") == 0)
				tdata->sched_policy = other;
			else 
				tdata->sched_policy = def_policy;

			i++;
			break;
		}
		token = strtok(NULL, ":");
	}
	if ( i < 2 ) {
		printf("Period and exec time are mandatory\n");
		exit(EXIT_FAILURE);
	}
	if ( i < 3 ) 
		tdata->deadline = usec_to_timespec(period); 
	
	if ( i < 4 ) 
		tdata->sched_prio = DEFAULT_THREAD_PRIORITY;

	// descriptive name for policy
	switch(tdata->sched_policy)
	{
		case rr:
			sprintf(tdata->sched_policy_descr, "SCHED_RR");
			break;
		case fifo:
			sprintf(tdata->sched_policy_descr, "SCHED_FIFO");
			break;
		case other:
			sprintf(tdata->sched_policy_descr, "SCHED_OTHER");
			break;
#ifdef AQUOSA
		case aquosa:
			sprintf(tdata->sched_policy_descr, "AQuoSA");
			break;
#endif
	}

	free(str);

}

int main(int argc, char* argv[])
{
	char ch;
	int longopt_idx;
	char tmp[PATH_LENGTH];
	int i,j,gnuplot;

	struct thread_data *threads_data, *tdata;

	policy_t policy = other;
	unsigned long spacing;
	char *logdir = NULL;
	char *logbasename = NULL;
	FILE *gnuplot_script = NULL;

	struct stat dirstat;

	struct timespec t_curr, t_next, t_start;
	int duration;

#ifdef AQUOSA
	int fragment;
#endif
	
	static struct option long_options[] = {
	                   {"help", 0, 0, 'h'},
			   {"fifo", 0, 0, 'f'},
			   {"rr", 0, 0, 'r'},
			   {"thread", 1, 0, 't'},
			   {"spacing", 1, 0, 's'},
			   {"logdir", 1, 0, 'l'},
	                   {"baselog", 1, 0, 'b'},
			   {"gnuplot", 1, 0, 'G'},
			   {"duration", 1, 0, 'D'},
#ifdef AQUOSA
			   {"qos", 0, 0, 'q'},
			   {"frag",1, 0, 'g'},
#endif
	                   {0, 0, 0, 0}
	               };

	// set defaults.
	nthreads = 0;
	spacing = 0;
	gnuplot = 0;
	duration = -1;
	logbasename = strdup("rt-app");
	threads = malloc( sizeof(pthread_t));
	threads_data = malloc( sizeof(struct thread_data));

#ifdef AQUOSA
	fragment = 1;
	
	while (( ch = getopt_long(argc,argv,"D:Ghfrb:s:l:qg:t:", 
				  long_options, &longopt_idx)) != -1)
#else
	while (( ch = getopt_long(argc,argv,"D:Ghfrb:s:l:t:", 
				  long_options, &longopt_idx)) != -1)
#endif	
	{
		switch (ch)
		{
			case 'h':
				usage(NULL);
				break;
			case 'f':
				if (policy != other)
					usage("Cannot set multiple policies");
				policy = fifo;
				break;
			case 'r':
				if (policy != other)
					usage("Cannot set multiple policies");
				policy = rr;
				break;
			case 'b':
				if (!logdir)	
					logdir = strdup(".");
				logbasename = strdup(optarg);
				break;
			case 's':
				spacing  = strtol(optarg, NULL, 0);
				if (spacing < 0)
					usage("Cannot set negative spacing");
				break;
			case 'l':
				logdir = strdup(optarg);	
				lstat(logdir, &dirstat);
				if (! S_ISDIR(dirstat.st_mode))
					usage("Cannot stat log directory");
				break;
			case 't':
				if (nthreads > 0)
				{
					threads = realloc(threads, (nthreads+1) * sizeof(pthread_t));
					threads_data = realloc(threads_data, (nthreads+1) * sizeof(struct thread_data));
				}
				parse_thread_args(optarg, 
						  &threads_data[nthreads],
						  policy);
				nthreads++;
				break;
			case 'G':
				gnuplot = 1;
				break;
			case 'D':
				duration = strtol(optarg, NULL, 10);
				if (duration < 0)
					usage("Cannot set negative duration");
				break;
#ifdef AQUOSA				
			case 'q':
				if (policy != other)
					usage("Cannot set multiple policies");
				policy = aquosa;
				break;
			case 'g':
				fragment = strtol(optarg, NULL, 10);
				if (fragment < 1 || fragment > 16)
					usage("Fragment divisor must be between"
					      "1 and 16");
				break;

#endif
			default:
				printf("Invalid option %c\n", ch);
				usage(NULL);

		}
	}

	if ( nthreads < 1)
		usage("You have to set parameters for at least one thread");
	
	// install a signal handler for proper shutdown.
	signal(SIGQUIT, shutdown);
	signal(SIGTERM, shutdown);
	signal(SIGHUP, shutdown);
	signal(SIGINT, shutdown);

	continue_running = 1;

	// Take the beginning time for everything 
	clock_gettime(CLOCK_MONOTONIC, &t_start);

	for (i = 0; i < nthreads; i++)
	{
		tdata = &threads_data[i];
		tdata->ind = i;
		tdata->main_app_start = t_start;
#ifdef AQUOSA
		tdata->fragment = fragment;
#endif
		if (logdir) {
			snprintf(tmp, PATH_LENGTH, "%s/%s-t%d.log",
				 logdir,logbasename,i);
			tdata->log_handler = fopen(tmp, "w");
			if (!tdata->log_handler){
				printf("Cannot open logfile %s\n", tmp);
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

		if (spacing > 0) {
			printf("Waiting %ld usecs... \n", spacing);
			clock_gettime(CLOCK_MONOTONIC, &t_curr);
			t_next = msec_to_timespec(spacing);
			t_next = timespec_add(&t_curr, &t_next);
			clock_nanosleep(CLOCK_MONOTONIC, 
					TIMER_ABSTIME, 
					&t_next,
					NULL);
		}
	}
	
	if (logdir && gnuplot)
	{
		gnuplot_script = fopen(tmp, "w+");
		fprintf(gnuplot_script,
			"set grid\n"
			"set key outside right\n"
			"set title \"Measured exec time per period\"\n"
			"set xlabel \"Cycle start time [usec]\"\n"
			"set ylabel \"Exec Time [usec]\"\n"
			"plot ");

		for (i=0; i<nthreads; i++)
		{
			snprintf(tmp, PATH_LENGTH, "%s/%s-duration.plot", 
				 logdir, logbasename);

			fprintf(gnuplot_script, 
				"\"%s-t%d.log\" u ($5/1000):9 w l"
				" title \"thread%d (%s)\"", 
				logbasename, i, i, 
				threads_data[i].sched_policy_descr);

			if ( i == nthreads-1)
				fprintf(gnuplot_script, "\n");
			else
				fprintf(gnuplot_script, ", ");
		}
		fclose(gnuplot_script);
		snprintf(tmp, PATH_LENGTH, "%s/%s-slack.plot", 
		 	 logdir,logbasename);
		gnuplot_script = fopen(tmp, "w+");
		fprintf(gnuplot_script,
			"set grid\n"
			"set key outside right\n"
			"set title \"Slack (negative = tardiness)\"\n"
			"set xlabel \"Cycle start time [msec]\"\n"
			"set ylabel \"Slack/Tardiness [usec]\"\n"
			"plot ");

		for (i=0; i<nthreads; i++)
		{
			fprintf(gnuplot_script, 
				"\"%s-t%d.log\" u ($5/1000):10 w l"
				" title \"thread%d (%s)\"", 
				logbasename, i, i,
				threads_data[i].sched_policy_descr);

			if ( i == nthreads-1) 
				fprintf(gnuplot_script, ", 0 notitle\n");
			else
				fprintf(gnuplot_script, ", ");

		}
		fclose(gnuplot_script);
	}
	
	if (duration > 0)
	{
		sleep(duration);
		shutdown(SIGTERM);
	}
	
	for (i = 0; i < nthreads; i++) 	{
		pthread_join(threads[i], NULL);
	}
	exit(EXIT_SUCCESS);


exit_err:
	exit(EXIT_FAILURE);
}



