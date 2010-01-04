#define QOS_DEBUG_LEVEL QOS_LEVEL_ERROR
#include <linux/aquosa/qos_debug.h>

#include <aquosa/qres_lib.h>
#include <aquosa/qmgr_util.h>

#include <aquosa/qosutil.h>

#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <stdlib.h>
#include <time.h>

#include <errno.h>
#include <string.h>
#include <sched.h>
#include <libgen.h>

#include "queue.h"

queue_t q;

unsigned long counted_in_period = 0;
long first_time = 0;
long sum_delta_time = 0;	//< Since start of program
long max_delta_time = 0;	//< Since start of program
long enqueued_sum_dt = 0;	//<
long job_cnt = 0;
FILE *loghandle;


int frag = 4;

int job(void *);

#define UNASSIGNED (-1)

int use_qos = 0;
int use_rt = 0;
int use_load = 0;
int ratio = 25;
int queue_size = UNASSIGNED;

/************************************************************/

int enqueued;		/* Number of enqueued jobs */

long period_nsec = 40000000; /* Is approximated according to timer resolution */

struct timespec ref_ts;
long unsigned deadline_nsec;
struct timeval tv;

qmgr_period_spec_t ps;

int job(void *ptr) {
	
	struct timespec ts, dl_ts;
	unsigned long curr_time;
	job_cnt++;
	count(counted_in_period * ratio / 100);
	clock_gettime(CLOCK_MONOTONIC, &ts);
	curr_time = (ts.tv_sec - ref_ts.tv_sec) * 1000000l + 
		(ts.tv_nsec - ref_ts.tv_nsec) / 1000l + 
		period_nsec / 1000l;
	qmgr_periodic_get_deadline(&ps, &dl_ts);
	unsigned long delta_time = (ts.tv_sec - dl_ts.tv_sec) * 1000000l + 
		(ts.tv_nsec - dl_ts.tv_nsec) / 1000l + 
		period_nsec / 1000l;

	if (delta_time > max_delta_time)
		max_delta_time = delta_time;

	int num_elems = queue_get_num_elements(&q);
	if (num_elems == queue_size) {
		long extracted_time = queue_extract(&q);
		enqueued_sum_dt -= extracted_time;
	}
	queue_insert(&q, delta_time);
	num_elems = queue_get_num_elements(&q);

	sum_delta_time += delta_time;
	enqueued_sum_dt += delta_time;

	queue_iterator_t qit = qit = queue_begin(&q);
	long max_qdt = queue_it_next(&qit);
	
	while (queue_it_has_next(&qit)) {
		long dt = queue_it_next(&qit);
		if (dt > max_qdt)
			max_qdt = dt;
	}

	if (num_elems > 0 && (job_cnt % queue_size) == 0) {
		printf( "time=%ld, avg delay ever=%ld, max delay ever=%ld, "
			"avg delay=%ld, max delay=%ld\n", curr_time, 
			sum_delta_time / job_cnt, max_delta_time, 
			enqueued_sum_dt / num_elems, max_qdt);
		fflush(stdout);
		if (loghandle) {
			fprintf(loghandle, "%ld\t%ld\t%ld\t%ld\t%ld\n",
				curr_time, sum_delta_time / job_cnt, max_delta_time, 
				enqueued_sum_dt / num_elems, max_qdt);
		}
		fflush(loghandle);
	
	}

	return 0;
}

void usage() {
	printf("\n");
	printf("Usage: rt-app [options]\n");
	printf("  -P <period>            Set period in microseconds.\n");
	printf("  --rt                   Use SCHED_RR scheduling policy\n");
	printf("  --qos                  Use AQuoSA reservation\n");
	printf("  (--ratio|-r) <0-100>   Set workload ratio\n");
	printf("  (--frag|-f) <1-16>     Set division factor for reservation\n");
	printf("  -d <num>               Set number of loops to execute in each job\n");
	printf("  -qs|--queue-size <num> Set size of history queue for response times\n");
	printf("  -l <logfilename>	 Full path of the log file\n");
	printf("\n");
}

int main(int argc, char **argv) {
	qres_sid_t sid;
	argc--;  argv++;
	char *base, *dir;
	char plotfile[256];
	FILE *ploth;
	while (argc > 0) {
		if (strcmp(argv[0], "-P") == 0) {
			qos_chk_exit(argc > 0);
			argc--;  argv++;
			period_nsec = atol(argv[0]) * 1000ul;
		} else if (strcmp(argv[0], "--qos") == 0) {
			use_qos = 1;
		} else if (strcmp(argv[0], "--rt") == 0) {
			use_rt = 1;
		} else if (strcmp(argv[0], "--load") == 0) {
			use_load = 1;
		} else if (strcmp(argv[0], "-r") == 0 || strcmp(argv[0], "--ratio") == 0) {
			qos_chk_exit(argc > 0);
			argc--;  argv++;
			ratio = atoi(argv[0]);
		} else if (strcmp(argv[0], "-qs") == 0 || strcmp(argv[0], "--queue-size") == 0) {
			qos_chk_exit(argc > 0);
			argc--;  argv++;
			queue_size = atoi(argv[0]);
			qos_chk_exit(queue_size >= 1);
		} else if (strcmp(argv[0], "-f") == 0 || strcmp(argv[0], "--frag") == 0) {
			qos_chk_exit(argc > 0);
			argc--;  argv++;
			frag = atoi(argv[0]);
			if (frag < 1 || frag > 16) {
				printf("Argument to --frag option must be in the range 1..16\n");
				exit(-1);
			}
		} else if (strcmp(argv[0], "--help") == 0 || strcmp(argv[0], "-h") == 0) {
			usage();
			exit(0);
		} else if (strcmp(argv[0], "-d") == 0) {
			qos_chk_exit(argc > 0);
			argc--;  argv++;
			counted_in_period = atol(argv[0]);
		} else if (strcmp(argv[0], "-l") == 0) {
			qos_chk_exit(argc > 0);
			argc--; argv++;
			loghandle = fopen(argv[0], "w");
			if (!loghandle) {
				printf("Cannot open log file : %s\n", strerror(errno));
				exit(-1);
			}
			fprintf(loghandle, "#time | avg delay ever | max delay ever"
		  			   " | avg delay | max delay\n");
			base = basename(argv[0]);
			dir = dirname(argv[0]);
			snprintf(plotfile, 256, "%s/rtapp-delay.plot", dir);
			ploth = fopen(plotfile, "w");
			if (!ploth) {
				fprintf(stderr, "Cannot write gnuplot file %s\n", plotfile);
			} else { 
				fprintf(ploth, 
					"set terminal wx\n"
					"plot \"%s\" u 0:4 title \"Avg Delay\" w lines,"
					"\"%s\" u 0:2 title \"Avg Delay Ever\" w lines,"
					"\"%s\" u 0:5 title \"Max Delay\" w lines\n"
					"set terminal pdf\n"
					"set output 'rtapp-avg-delay.pdf'\n"
					"plot \"%s\" u 0:4 title \"Avg Delay\" w lines,"
					"\"%s\" u 0:2 title \"Avg Delay Ever\" w lines,"
					"\"%s\" u 0:5 title \"Max Delay\" w lines\n",
					base, base, base, base, base, base);
				fclose(ploth);
			}	

		} else {
			fprintf(stderr, "Unknown option: %s\n", argv[0]);
			usage();
			exit(-1);
		}
		argc--;  argv++;
	}

	struct sched_param sp = {
		.sched_priority = 20
	};
	if (use_rt) {
		printf("Setting RT priority...\n");
		if (sched_setscheduler(0, SCHED_RR, &sp) < 0) {
			fprintf(stderr, "Warning: couldn't set RT priority: %s\n", strerror(errno));
		}
	}

	if (counted_in_period == 0) {
		printf("Counting for 1 period...\n");
		counted_in_period = count_and_wait(1000000ul);
		printf("Counted in 1 period: %lu\n", counted_in_period);
	}

	if (queue_size == UNASSIGNED)
		queue_size = 1000000000 / period_nsec;

	qos_chk_exit(queue_init(&q, queue_size) == OK);

	if (use_qos) {
		if (use_rt) {
			sp.sched_priority = 0;
			printf("Unsetting RT priority...\n");
			if (sched_setscheduler(0, SCHED_OTHER, &sp) < 0) {
				fprintf(stderr, "Warning: couldn't set OTHER priority: %s\n", strerror(errno));
			}
		}
		qres_params_t params = {
			.Q_min = period_nsec / 1000 * ratio / 100 * 4 / 3 / frag,
			.Q = period_nsec / 1000 * ratio / 100 * 4 / 3 / frag,
			.P = period_nsec / 1000 / frag,
			.flags = 0,
		};
		printf("Creating QRES Server with Q=%llu, P=%llu\n", params.Q, params.P);
		qos_chk_ok_exit(qres_init());
		qos_chk_ok_exit(qres_create_server(&params, &sid));
		qos_chk_ok_exit(qres_attach_thread(sid, 0, 0));
	}

	if (use_load) {
		printf("Computing and sleeping ...\n");
		int i;
		for (i = 0; i < 10; i++) {
			count(counted_in_period);
			unsigned long ns = (unsigned long) (1.0e9 / 25 * (rand() / (RAND_MAX + 1.0)));
			printf("%d: %lu\n", i, ns);
			struct timespec ts = {
				.tv_sec = 0lu,
				.tv_nsec = ns
			};
			nanosleep(&ts, NULL);
		}
	}

	printf("Starting periodic task (using ratio: %d%%)...\n", ratio);
	struct timespec ts = {
		.tv_sec = period_nsec / 1000000000L,
		.tv_nsec = period_nsec % 1000000000L
	};
	qos_chk_ok_exit(qmgr_periodic_start(&ps, &ts));
	qmgr_periodic_get_deadline(&ps, &ref_ts);

	while (1) {
		job(NULL);
		qos_chk_ok_exit(qmgr_periodic_wait(&ps));
	}
	
	return 0;
}
