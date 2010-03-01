#ifndef _RTAPP_TYPES_H_
#define _RTAPP_TYPES_H_

#include <sched.h>
#include <time.h>
#include <stdio.h>
#include "config.h"
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

	int duration;
	unsigned long wait_before_start;
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

typedef struct _timing_point_t {
	int ind;
	unsigned long period;
	unsigned long min_et;
	unsigned long max_et;
	unsigned long rel_start_time;
	unsigned long abs_start_time;
	unsigned long end_time;
	unsigned long deadline;
	unsigned long duration;
	long slack;
#ifdef AQUOSA
	qres_time_t budget;
	qres_time_t used_budget;
#endif
} timing_point_t;

#endif // _RTAPP_TYPES_H_ 
