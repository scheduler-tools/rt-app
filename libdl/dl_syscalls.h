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
#include <time.h>
#include <linux/types.h>

#define SCHED_DEADLINE	6

/* XXX use the proper syscall numbers */
#ifdef __x86_64__
#define __NR_sched_setparam2		313
#define __NR_sched_getparam2		314
#define __NR_sched_setscheduler2	315
#endif

#ifdef __i386__
#define __NR_sched_setparam2		350
#define __NR_sched_getparam2		351
#define __NR_sched_setscheduler2	352
#endif

#ifdef __arm__
#define __NR_sched_setscheduler2	379
#define __NR_sched_setparam2		380
#define __NR_sched_getparam2		381
#endif

#define SF_SIG_RORUN		2
#define SF_SIG_DMISS		4
#define SF_BWRECL_DL		8
#define SF_BWRECL_RT		16
#define SF_BWRECL_OTH		32

#define RLIMIT_DLDLINE		16
#define RLIMIT_DLRTIME		17

struct sched_param2 {
	int sched_priority;
	unsigned int sched_flags;
	__u64 sched_runtime;
	__u64 sched_deadline;
	__u64 sched_period;

	__u64 __unused[12];
};

int sched_setscheduler2(pid_t pid, int policy,
			  const struct sched_param2 *param);

int sched_setparam2(pid_t pid,
		      const struct sched_param2 *param);

int sched_getparam2(pid_t pid, struct sched_param2 *param);

#endif /* __DL_SYSCALLS__ */

