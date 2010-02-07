#ifndef _RT_APP_H_
#define _RT_APP_H_

#include <stdlib.h>
#include <stdio.h>
#include <error.h>
#include <time.h>
#include <sched.h>
#include <pthread.h>
#include <getopt.h>
#include <sys/stat.h>
#include <string.h>
#include <signal.h>
#include "rtapp_types.h"
#include "rtapp_args.h"

#ifdef AQUOSA
#include <aquosa/qres_lib.h>
#define BUDGET_PERC_INCR 10 // percent
#endif /* AQUOSA */

#define PATH_LENGTH 256

void *thread_body(void *arg);


#endif /* _RT_APP_H_ */

