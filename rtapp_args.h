#ifndef _RTAPP_ARGS_H
#define _RTAPP_ARGS_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "timespec_utils.h"
#include "rt-app.h"

void
usage (const char* msg);

void
parse_thread_args(char *arg, struct thread_data *tdata, policy_t def_policy);

#endif // _RTAPP_ARGS_H
