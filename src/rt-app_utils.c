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

#include "rt-app_utils.h"

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

#ifdef DLSCHED
__u64
timespec_to_nsec(struct timespec *ts)
{
	return round(ts->tv_sec * 1E9 + ts->tv_nsec);
}
#endif

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
		"%d\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\t%ld\t%lu",
		t->ind,
		t->period,
		t->min_et,
		t->max_et,
		t->rel_start_time,
		t->abs_start_time,
		t->end_time,
		t->deadline,
		t->duration,
		t->slack,
		t->resp_time
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

#ifdef DLSCHED
pid_t
gettid(void)
{
	return syscall(__NR_gettid);
}
#endif

int
string_to_phase(const char *phase_name, phase_t *phase)
{
	if (strncmp(phase_name, "r", 1) == 0)
		*phase = RUN;
	else if (strncmp(phase_name, "s", 1) == 0)
		*phase =  SLEEP;
	else
		return 1;
	return 0;
}

int
string_to_policy(const char *policy_name, policy_t *policy)
{
	if (strcmp(policy_name, "SCHED_OTHER") == 0)
		*policy = other;
	else if (strcmp(policy_name, "SCHED_RR") == 0)
		*policy =  rr;
	else if (strcmp(policy_name, "SCHED_FIFO") == 0)
		*policy =  fifo;
#ifdef DLSCHED
	else if (strcmp(policy_name, "SCHED_DEADLINE") == 0)
		*policy =  deadline;
#endif
#ifdef AQUOSA
	else if ( (strcmp(policy_name, "AQUOSA") == 0) || \
		  (strcmp(policy_name, "AQuoSA") == 0))
		*policy =  aquosa;
#endif
	else
		return 1;
	return 0;
}

int
policy_to_string(policy_t policy, char *policy_name)
{
	switch (policy) {
		case other:
			strcpy(policy_name, "SCHED_OTHER");
			break;
		case rr:
			strcpy(policy_name, "SCHED_RR");
			break;
		case fifo:
			strcpy(policy_name, "SCHED_FIFO");
			break;
#ifdef DLSCHED			
		case deadline:
			strcpy(policy_name, "SCHED_DEADLINE");
			break;
#endif
#ifdef AQUOSA
		case aquosa:
			strcpy(policy_name, "AQuoSA");
			break;
#endif
		default:
			return 1;
	}
	return 0;
}


void ftrace_write(int mark_fd, const char *fmt, ...)
{
	va_list ap;
	int n, size = BUF_SIZE;
	char *tmp, *ntmp;

	if (mark_fd < 0) {
		log_error("invalid mark_fd");
		exit(EXIT_FAILURE);
	}

	if ((tmp = malloc(size)) == NULL) {
		log_error("Cannot allocate ftrace buffer");
		exit(EXIT_FAILURE);
	}
	
	while(1) {
		/* Try to print in the allocated space */
		va_start(ap, fmt);
		n = vsnprintf(tmp, BUF_SIZE, fmt, ap);
		va_end(ap);
		/* If it worked return success */
		if (n > -1 && n < size) {
			write(mark_fd, tmp, n);
			free(tmp);
			return;
		}
		/* Else try again with more space */
		if (n > -1)	/* glibc 2.1 */
			size = n+1;
		else		/* glibc 2.0 */
			size *= 2;
		if ((ntmp = realloc(tmp, size)) == NULL) {
			free(tmp);
			log_error("Cannot reallocate ftrace buffer");
			exit(EXIT_FAILURE);
		} else {
			tmp = ntmp;
		}
	}

}
