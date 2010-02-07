#ifndef _TIMESPEC_UTILS_H_
#define _TIMESPEC_UTILS_H_

#include <time.h>
#include <math.h>

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
