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

#ifndef _TIMESPEC_UTILS_H_
#define _TIMESPEC_UTILS_H_

#include <time.h>
#include <stdint.h>

#include "config.h"
#include "rt-app_types.h"

#ifndef LOG_PREFIX
#define LOG_PREFIX "[rt-app] "
#endif

#define LOG_LEVEL_DEBUG 100
#define LOG_LEVEL_INFO 75
#define LOG_LEVEL_NOTICE 50
#define LOG_LEVEL_ERROR 10
#define LOG_LEVEL_CRITICAL 10

#define BUF_SIZE 100

extern int log_level;
extern int ftrace_level;

/* This prepends a string to a message */
#define rtapp_log_to(where, level, level_pfx, msg, args...)		\
do {									\
    if (level <= log_level) {						\
        fprintf(where, LOG_PREFIX level_pfx msg "\n", ##args);		\
    }									\
} while (0)

#define FTRACE_NONE	0x00
#define FTRACE_MAIN	0x01
#define FTRACE_TASK	0x02
#define FTRACE_LOOP	0x04
#define FTRACE_EVENT	0x08
#define FTRACE_STATS	0x10
#define FTRACE_ATTRS	0x20

#define log_ftrace(mark_fd, level, msg, args...)			\
do {									\
    if (level & ftrace_level)						\
	ftrace_write(mark_fd, msg, ##args);				\
} while (0)

#define log_notice(msg, args...)					\
do {									\
    rtapp_log_to(stderr, LOG_LEVEL_NOTICE, "<notice> ", msg, ##args);	\
} while (0)

#define log_info(msg, args...)						\
do {									\
    rtapp_log_to(stderr, LOG_LEVEL_INFO, "<info> ", msg, ##args);	\
} while (0)

#define log_error(msg, args...)						\
do {									\
    rtapp_log_to(stderr, LOG_LEVEL_ERROR, "<error> ", msg, ##args);	\
} while (0)

#define log_debug(msg, args...)						\
do {									\
    rtapp_log_to(stderr, LOG_LEVEL_DEBUG, "<debug> ", msg, ##args);	\
} while (0)

#define log_critical(msg, args...)					\
do {									\
    rtapp_log_to(stderr, LOG_LEVEL_CRITICAL, "<crit> ", msg, ##args);	\
} while (0)

unsigned long
timespec_to_usec(struct timespec *ts);

unsigned long long
timespec_to_usec_ull(struct timespec *ts);

long
timespec_to_usec_long(struct timespec *ts);

struct timespec
usec_to_timespec(unsigned long usec);

struct timespec
usec_to_timespec(unsigned long usec);

struct timespec
msec_to_timespec(unsigned int msec);

struct timespec
timespec_add(struct timespec *t1, struct timespec *t2);

struct timespec
timespec_sub(struct timespec *t1, struct timespec *t2);

int
timespec_lower(struct timespec *what, struct timespec *than);

int64_t
timespec_sub_to_ns(struct timespec *t1, struct timespec *t2);

void
log_timing(FILE *handler, timing_point_t *t);

pid_t
gettid(void);

__u64
timespec_to_nsec(struct timespec *ts);

char *
policy_to_string(policy_t policy);

int
string_to_policy(const char *policy_name, policy_t *policy);

int
string_to_resource(const char *name, resource_t *resource);

int
resource_to_string(resource_t resource, char *name);

int
ftrace_setup(char *categories);

void
ftrace_write(int mark_fd, const char *fmt, ...);

#endif // _TIMESPEC_UTILS_H_

/* vim: set ts=8 noexpandtab shiftwidth=8: */
