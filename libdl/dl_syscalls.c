#include "dl_syscalls.h"

int sched_setscheduler_ex(pid_t pid, int policy, unsigned len,
			  struct sched_param_ex *param)
{
	return syscall(__NR_sched_setscheduler_ex, pid, policy, len, param);
}

int sched_setparam_ex(pid_t pid, unsigned len, struct sched_param_ex *param)
{
	return syscall(__NR_sched_setparam_ex, pid, len, param);
}

int sched_getparam_ex(pid_t pid, unsigned len, struct sched_param_ex *param)
{
	return syscall(__NR_sched_getparam_ex, pid, len, param);
}

int sched_wait_interval_ex(const struct timespec *rqtp, struct timespec *rmtp)
{
	return syscall(__NR_sched_wait_interval, rqtp, rmtp);
}

