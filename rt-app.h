#ifndef _RT_APP_H_
#define _RT_APP_H__

#include <stdlib.h>
#include <stdio.h>
#include <error.h>
#include <time.h>
#include <sched.h>
#include <pthread.h>
#include <getopt.h>
#include <sys/stat.h>
#include <string.h>
#include <libgen.h>
#include <signal.h>
#include <math.h>
#include <unistd.h>

#ifdef AQUOSA
#include <aquosa/qres_lib.h>
#define BUDGET_PERC_INCR 10 // percent
#endif /* AQUOSA */

#define PATH_LENGTH 256
#define DEFAULT_THREAD_PRIORITY 10

typedef enum policy_t 
{ 
	other = SCHED_OTHER, 
	rr = SCHED_RR, 
	fifo = SCHED_FIFO
#ifdef AQUOSA
	, aquosa = 1000 
#endif
} policy_t;

void *thread_body(void *arg);

struct thread_data {
	int ind;

	struct timespec min_et, max_et;
	struct timespec period, deadline;
	struct timespec main_app_start;
    
	FILE *log_handler;
	policy_t sched_policy;
    char sched_policy_descr[16];
	int sched_prio;

#ifdef AQUOSA
	int fragment;
	int sid;
	qres_params_t params;
#endif
};

#endif /* _RT_APP_H_ */

