#include "rtapp_utils.h"

unsigned int 
timespec_to_msec(struct timespec *ts)
{
	return (ts->tv_sec * 1E9 + ts->tv_nsec) / 1000000;
}

long 
timespec_to_lusec(struct timespec *ts)
{
	return round((ts->tv_sec * 1E9 + ts->tv_nsec) / 1000.0);
}

unsigned long 
timespec_to_usec(struct timespec *ts)
{
	return round((ts->tv_sec * 1E9 + ts->tv_nsec) / 1000.0);
}

struct timespec 
usec_to_timespec(unsigned long usec)
{
	struct timespec ts;

	ts.tv_sec = usec / 1000000;
	ts.tv_nsec = (usec % 1000000) * 1000;
	
	return ts;
}

struct timespec 
msec_to_timespec(unsigned int msec)
{
	struct timespec ts;

	ts.tv_sec = msec / 1000;
	ts.tv_nsec = (msec % 1000) * 1000000;

	return ts;
}

struct timespec 
timespec_add(struct timespec *t1, struct timespec *t2)
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

struct timespec 
timespec_sub(struct timespec *t1, struct timespec *t2)
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

int 
timespec_lower(struct timespec *what, struct timespec *than)
{
	if (what->tv_sec > than->tv_sec)
		return 0;

	if (what->tv_sec < than->tv_sec)
		return 1;

	if (what->tv_nsec < than->tv_nsec)
		return 1;

	return 0;
}

void
log_timing(FILE *handler, timing_point_t *t)
{
	fprintf(handler, 
		"%d\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\t%ld",
		t->ind,
		t->period,
		t->min_et,
		t->max_et,
		t->rel_start_time,
		t->abs_start_time,
		t->end_time,
		t->deadline,
		t->duration,
		t->slack
	);
#ifdef AQUOSA
	fprintf(handler,
		"\t" QRES_TIME_FMT "\t" QRES_TIME_FMT,
		t->budget, 
		t->used_budget
	);
#endif
	fprintf(handler, "\n");
}

