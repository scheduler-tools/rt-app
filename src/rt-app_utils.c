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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>

#include "rt-app_utils.h"

int log_level = 50;   // default

unsigned long
timespec_to_usec(struct timespec *ts)
{
	return lround((ts->tv_sec * 1E9 + ts->tv_nsec) / 1000.0);
}

unsigned long long
timespec_to_usec_ull(struct timespec *ts)
{
	return llround((ts->tv_sec * 1E9 + ts->tv_nsec) / 1000.0);
}

long
timespec_to_usec_long(struct timespec *ts)
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

int64_t
timespec_sub_to_ns(struct timespec *t1, struct timespec *t2)
{
	int64_t diff;

	if (t1->tv_nsec < t2->tv_nsec) {
		diff = 1E9 * (int64_t)((int) t1->tv_sec -
			(int) t2->tv_sec - 1);
		diff += ((int) t1->tv_nsec + 1E9 - (int) t2->tv_nsec);
	} else {
		diff = 1E9 * (int64_t)((int) t1->tv_sec - (int) t2->tv_sec);
		diff += ((int) t1->tv_nsec - (int) t2->tv_nsec);
	}
	return diff;
}


void
log_timing(FILE *handler, timing_point_t *t)
{
	fprintf(handler,
		"%4d %8lu %8lu %8lu %15llu %15llu %15llu %10ld %10lu %10lu %10lu",
		t->ind,
		t->perf,
		t->duration,
		t->period,
		t->start_time,
		t->end_time,
		t->rel_start_time,
		t->slack,
		t->c_duration,
		t->c_period,
		t->wu_latency
	);
	fprintf(handler, "\n");
}

pid_t
gettid(void)
{
	return syscall(__NR_gettid);
}

int
string_to_policy(const char *policy_name, policy_t *policy)
{
	if (strcmp(policy_name, "SCHED_OTHER") == 0)
		*policy = other;
	else if (strcmp(policy_name, "SCHED_IDLE") == 0)
		*policy = idle;
	else if (strcmp(policy_name, "SCHED_RR") == 0)
		*policy =  rr;
	else if (strcmp(policy_name, "SCHED_FIFO") == 0)
		*policy =  fifo;
#ifdef DLSCHED
	else if (strcmp(policy_name, "SCHED_DEADLINE") == 0)
		*policy =  deadline;
#endif
	else
		return 1;
	return 0;
}

char *
policy_to_string(policy_t policy)
{
	switch (policy) {
		case other:
			return "SCHED_OTHER";
		case idle:
			return "SCHED_IDLE";
		case rr:
			return "SCHED_RR";
		case fifo:
			return "SCHED_FIFO";
#ifdef DLSCHED
		case deadline:
			return "SCHED_DEADLINE";
#endif
		default:
			return NULL;
	}
	return NULL;
}

int
string_to_resource(const char *name, resource_t *resource)
{
	if (strcmp(name, "mutex") == 0)
		*resource = rtapp_mutex;
	else if (strcmp(name, "signal") == 0)
		*resource = rtapp_signal;
	else if (strcmp(name, "wait") == 0)
		*resource = rtapp_wait;
	else if (strcmp(name, "broadcast") == 0)
		*resource = rtapp_broadcast;
	else if (strcmp(name, "sync") == 0)
		*resource = rtapp_sig_and_wait;
	else if (strcmp(name, "sleep") == 0)
		*resource = rtapp_sleep;
	else if (strcmp(name, "run") == 0)
		*resource = rtapp_run;
	else if (strcmp(name, "timer") == 0)
		*resource = rtapp_timer;
	else
		return 1;
	return 0;
}

int
resource_to_string(resource_t resource, char *resource_name)
{
	switch (resource) {
		case rtapp_mutex:
			strcpy(resource_name, "mutex");
			break;
		case rtapp_wait:
			strcpy(resource_name, "wait");
			break;
		case rtapp_signal:
			strcpy(resource_name, "signal");
			break;
		case rtapp_broadcast:
			strcpy(resource_name, "broadcast");
			break;
		case rtapp_sig_and_wait:
			strcpy(resource_name, "sync");
			break;
		case rtapp_sleep:
			strcpy(resource_name, "sleep");
			break;
		case rtapp_run:
			strcpy(resource_name, "run");
			break;
		case rtapp_timer:
			strcpy(resource_name, "timer");
			break;
		case rtapp_barrier:
			strcpy(resource_name, "barrier");
			break;
		default:
			return 1;
	}
	return 0;
}

void ftrace_write(int mark_fd, const char *fmt, ...)
{
	va_list ap;
	int n, size = BUF_SIZE, ret;
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
			ret = write(mark_fd, tmp, n);
			free(tmp);
			if (ret < 0) {
				log_error("Cannot write mark_fd: %s\n",
						strerror(errno));
				exit(EXIT_FAILURE);
			} else if (ret < n) {
				log_debug("Cannot write all bytes at once into mark_fd\n");
			}
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
