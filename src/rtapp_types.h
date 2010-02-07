#ifndef _RTAPP_TYPES_H_
#define _RTAPP_TYPES_H_

#include <sched.h>
#include <time.h>
#include <stdio.h>
#ifdef AQUOSA
#include <aquosa/qres_lib.h>
#endif /* AQUOSA */

typedef enum policy_t 
{ 
	other = SCHED_OTHER, 
	rr = SCHED_RR, 
	fifo = SCHED_FIFO
#ifdef AQUOSA
	, aquosa = 1000 
#endif
} policy_t;

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

#endif // _RTAPP_TYPES_H_ 
