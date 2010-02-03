#include "rt-app.h"

#define handle_error(en, msg) \
	do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

static int errno;
static int exit_cycle;
static pthread_t *threads;
static int nthreads;

static inline
unsigned int timespec_to_msec(struct timespec *ts)
{
	return (ts->tv_sec * 1E9 + ts->tv_nsec) / 1000000;
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
	ts.tv_sec = t1->tv_sec - t2->tv_sec;
	ts.tv_nsec = t1->tv_nsec - t2->tv_nsec;

	while (ts.tv_nsec < 0) {
		ts.tv_nsec = 1E9 + ts.tv_nsec;
		ts.tv_sec--;
	}

	return ts;

}

static inline
unsigned int max_run(int min, int max)
{
        return min + (((double) rand()) / RAND_MAX) * (max - min);
}

void run(int ind, struct timespec *min, struct timespec *max)
{
	int m = max_run(timespec_to_msec(min), timespec_to_msec(max));
	struct timespec t_start, t_step, t_exec = msec_to_timespec(m);

	/* get the start time */
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t_start);
	/* compute finish time for CPUTIME_ID clock */
	t_exec = timespec_add(&t_start, &t_exec);

	while (1) {
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t_step);
		if (timespec_to_msec(&t_step) >= timespec_to_msec(&t_exec))
			break;
	}
}

static void
shutdown(int sig)
{
	int i;
	// notify threads, join them, then exit
	exit_cycle = 0;
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
	int ret, i = 0;

	clock_gettime(CLOCK_MONOTONIC, &t);
	printf("\tThread %d started running at %u\n",
	       data->ind, timespec_to_msec(&t));
	
	switch (data->sched_policy)
	{
		case rr:
		case fifo:
		case batch:
			printf("Setting POSIX scheduling class:\n");
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
			/* TODO: create server. */
			printf("Creating AQuoSA reservation\n");
			break;
#endif
		default:
			printf("Unknown scheduling policy %d\n", data->sched_policy);
			exit(EXIT_FAILURE);
	}
		
	t_next = t;
	while (exit_cycle) {
		struct timespec t_start, t_end, t_diff;

		clock_gettime(CLOCK_MONOTONIC, &t_start);
		run(data->ind, &data->min_et, &data->max_et);
		clock_gettime(CLOCK_MONOTONIC, &t_end);
		
		t_diff = timespec_sub(&t_end, &t_start);
		
		t_next = timespec_add(&t_next, &data->period);
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t_next, NULL);
		
		i++;
	}
	printf("[%d] Exiting.\n", data->ind);
	fclose(data->log_handler);
	free(data);

	pthread_exit(NULL);
}

void
usage (const char* msg)
{
	printf("usage: rt-app -p <PERIOD> -e <EXEC_TIME> [ options... ]\n");
	printf("-h, --help\t:\tshow this help\n");
	printf("-f, --fifo\t:\trun with SCHED_FIFO policy\n");
	printf("-r, --rr\t:\tuse SCHED_RR policy\n");
	printf("-b, --batch\t:\tuse SCHED_BATCH policy\n");
	printf("-P, --priority\t:\tset scheduling priority\n");
	printf("-p, --period\t:\ttask period in usec (mandatory)\n");
	printf("-e, --exec\t:\ttask execution time\n");
	printf("-d, --deadline\t:\tdeadline (slack relative to period"
				   "default=0)\n");
	printf("-n, --nthreads\t:\tnumber of threads to start (default=1)\n");
	printf("-s, --spacing\t:\tusec to wait beetween thread starts\n");
	printf("-l, --logdir\t:\tsave logs to different directory\n");
	
#ifdef AQUOSA
	printf("-q, --qos\t:\tcreate AQuoSA reservation\n");
	printf("-g, --frag\t:\tfragment for the reservation\n");
#endif

	if (msg != NULL)
		printf("\n%s\n", msg);
	exit(0);
}


int main(int argc, char* argv[])
{
	char ch;
	int longopt_idx;
	char tmp[PATH_LENGTH];
	int i;

	struct thread_data *tdata;

	policy_t policy = other;
	int priority = DEFAULT_THREAD_PRIORITY;
	unsigned long deadline,period,exec,spacing;
	char *logdir = NULL;

	struct stat dirstat;

	struct timespec t_curr, t_next;
#ifdef AQUOSA
	int fragment;
#endif
	
	static struct option long_options[] = {
	                   {"help", 0, 0, 'h'},
			   {"fifo", 0, 0, 'f'},
			   {"rr", 0, 0, 'r'},
	                   {"batch", 0, 0, 'b'},
			   {"priority", 1, 0, 'P'},
	                   {"period",1, 0, 'p'},
	                   {"exec",1, 0, 'e'},
			   {"deadline", 1, 0, 'd'},
			   {"nthreads", 1, 0, 'n'},
			   {"spacing", 1, 0, 's'},
			   {"logdir", 1, 0, 'l'},
#ifdef AQUOSA
			   {"qos", 0, 0, 'q'},
			   {"frag",1, 0, 'g'},
#endif
	                   {0, 0, 0, 0}
	               };

	// set defaults.
	nthreads = 1;
	spacing = 0;
	period = exec = deadline = -1; 

#ifdef AQUOSA
	fragment = 1;
	
	while (( ch = getopt_long(argc,argv,"hfrbp:e:d:n:s:l:qg:P:", 
				  long_options, &longopt_idx)) != -1)
#else
	while (( ch = getopt_long(argc,argv,"hfrbp:e:d:n:s:l:P:", 
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
				if (policy != other)
					usage("Cannot set multiple policies");
				policy = batch;
				break;
			case 'P':
				priority = strtol(optarg, NULL, 10);
				// don't check, will fail on pthread_setschedparam
				break;
			case 'p':
				period = strtol(optarg, NULL, 10);
				if (period <= 0)
					usage("Cannot set negative period.");
				break;
			case 'e':
				exec = strtol(optarg, NULL, 10);
				if (exec >= period)
					usage("Exec time cannot be greater than"
					      " period.");
				if (exec <= 0 )
					usage("Cannot set negative exec time");
				
				break;
			case 'd':
				deadline = strtol(optarg, NULL, 10);
				if (deadline < exec)
					usage ("Deadline cannot be less than "
						"execution time");
				if (deadline > period)
					usage ("Deadline cannot be greater than "
						"period");
				if (deadline <= 0 )
					usage ("Cannot set negative deadline");
				break;
			case 'n':
				nthreads = strtol(optarg, NULL, 0);
				if (nthreads < 1 || nthreads > 16)
					usage ("Thread number must be between 0 and 1");
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
#ifdef AQUOSA				
			case 'q':
				if (policy != other)
					usage("Cannot set multiple policies");
				policy = aquosa;
				break;
			case 'g':
				fragment = strtol(optarg, NULL, 10);
				if (fragment < 1 || fragment > 16)
					usage("Fragment divisor must be between 1 and 16");
				break;
#endif
			default:
				usage(NULL);
		}
	}
	if (period == -1 || exec == -1)
		usage("Period and execution time are mandatory");
	if (deadline == -1)
		deadline = period;

	// install a signal handler for proper shutdown.
	signal(SIGQUIT, shutdown);
	signal(SIGTERM, shutdown);
	signal(SIGHUP, shutdown);
	signal(SIGINT, shutdown);

	threads = malloc(nthreads * sizeof(pthread_t));
	exit_cycle = 1;

	for (i = 0; i<nthreads; i++)
	{
		tdata = malloc(sizeof(struct thread_data));
		tdata->ind = i;
		tdata->sched_policy = policy;
		tdata->period = usec_to_timespec(period) ;
		tdata->deadline = usec_to_timespec(deadline);
		tdata->sched_prio = priority; 
		tdata->min_et = usec_to_timespec(exec);
		tdata->max_et = usec_to_timespec(exec);
		if (logdir) {
			snprintf(tmp, PATH_LENGTH, "%s/rt-app-t%d.log",
				 logdir,i);
			tdata->log_handler = fopen(tmp, "w");
			if (!tdata->log_handler){
				printf("Cannot open logfile %s\n", tmp);
				exit(EXIT_FAILURE);
			}
		} else {
			tdata->log_handler = stdout;
		}
		
		if (pthread_create(&threads[i], NULL, thread_body, (void*) tdata))
			goto exit_err;

		if (spacing > 0) {
			printf("Waiting %ld usec before starting next thread\n",
				spacing);
			clock_gettime(CLOCK_MONOTONIC, &t_curr);
			t_next = msec_to_timespec((long) spacing / 1000.0);
			t_next = timespec_add(&t_curr, &t_next);
			clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t_next,
					NULL);
		}
	}
	for (i = 0; i < nthreads; i++)
	{
		pthread_join(threads[i], NULL);
	}
	exit(EXIT_SUCCESS);

exit_err:
	exit(EXIT_FAILURE);
}



