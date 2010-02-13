#ifndef _TIMESPEC_UTILS_H_
#define _TIMESPEC_UTILS_H_

#include <time.h>
#include <math.h>
#include <stdio.h>

#ifndef LOG_PREFIX
#define LOG_PREFIX "[rt-app] "
#endif
#ifndef LOG_LEVEL
#define LOG_LEVEL 50
#endif

#define LOG_LEVEL_DEBUG 100
#define LOG_LEVEL_INFO 50
#define LOG_LEVEL_ERROR 10
#define LOG_LEVEL_CRITICAL 10

/* This prepend a string to a message */
#define rtapp_log_to(where, level, msg, args...)			\
do {									\
    if (level <= LOG_LEVEL) {						\
        fprintf(where, LOG_PREFIX msg "\n", ##args);			\
    }									\
} while (0);

#define log_info(msg, args...)						\
do {									\
    rtapp_log_to(stderr, LOG_LEVEL_INFO, msg, ##args);			\
} while (0);

#define log_error(msg, args...)						\
do {									\
    rtapp_log_to(stderr, LOG_LEVEL_ERROR, msg, ##args);			\
} while (0);

#define log_debug(msg, args...)						\
do {									\
    rtapp_log_to(stderr, LOG_LEVEL_DEBUG, msg, ##args);			\
} while (0);

#define log_critical(msg, args...)					\
do {									\
    rtapp_log_to(stderr, LOG_LEVEL_CRITICAL, msg, ##args);		\
} while (0);

unsigned int 
timespec_to_msec(struct timespec *ts);

long 
timespec_to_lusec(struct timespec *ts);

unsigned long 
timespec_to_usec(struct timespec *ts);

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

#endif // _TIMESPEC_UTILS_H_ 

/* vim: set ts=8 noexpandtab shiftwidth=8: */
