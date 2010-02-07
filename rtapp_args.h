#ifndef _RTAPP_ARGS_H_
#define _RTAPP_ARGS_H_

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "timespec_utils.h"
#include "rtapp_types.h"

#define DEFAULT_THREAD_PRIORITY 10

void
usage (const char* msg);

void
parse_thread_args(char *arg, struct thread_data *tdata, policy_t def_policy);

#endif // _RTAPP_ARGS_H_
