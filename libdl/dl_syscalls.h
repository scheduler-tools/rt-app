/*
 * Libdl
 *  (C) Dario Faggioli <raistlin@linux.it>, 2009, 2010
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License (COPYING file) for more details.
 *
 */

#ifndef __DL_SYSCALLS__
#define __DL_SYSCALLS__

#include <linux/kernel.h>
#include <linux/unistd.h>
#include <linux/types.h>
#include <time.h>

#define SCHED_DEADLINE	6

/* XXX use the proper syscall numbers */
#ifdef __x86_64__
#define __NR_sched_setscheduler_ex	300
#define __NR_sched_setparam_ex		301
#define __NR_sched_getparam_ex		302
#define __NR_sched_wait_interval	303
#endif

#ifdef __i386__
#define __NR_sched_setscheduler_ex	338
#define __NR_sched_setparam_ex		339
#define __NR_sched_getparam_ex		340
#define __NR_sched_wait_interval	341
#endif

#ifdef __arm__
#define __NR_sched_setscheduler_ex	366
#define __NR_sched_setparam_ex		367
#define __NR_sched_getparam_ex		368
#define __NR_sched_wait_interval	369
#endif

#define SCHED_SIG_RORUN		0x0001
#define SCHED_SIG_DMISS		0x0002
#define SCHED_BWRECL_DL         0x0004
#define SCHED_BWRECL_RT         0x0008
#define SCHED_BWRECL_OTH        0x0010

#define RLIMIT_DLDLINE		16
#define RLIMIT_DLRTIME		17

struct sched_param_ex {
	int sched_priority;
	struct timespec sched_runtime;
	struct timespec sched_deadline;
	struct timespec sched_period;
	unsigned int sched_flags;
};

int sched_setscheduler_ex(pid_t pid, int policy, unsigned int len,
			  struct sched_param_ex *param);

int sched_setparam_ex(pid_t pid, unsigned len, struct sched_param_ex *param);

int sched_getparam_ex(pid_t pid, unsigned len, struct sched_param_ex *param);

int sched_wait_interval(const struct timespec *rqtp, struct timespec *rmtp);

#endif /* __DL_SYSCALLS__ */

