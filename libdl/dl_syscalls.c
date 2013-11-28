#include "dl_syscalls.h"

int sched_setscheduler2(pid_t pid, int policy,
			  const struct sched_attr *attr)
{
	return syscall(__NR_sched_setscheduler2, pid, policy, attr);
}

int sched_setattr(pid_t pid,
		      const struct sched_attr *attr)
{
	return syscall(__NR_sched_setattr, pid, attr);
}

int sched_getattr(pid_t pid, struct sched_attr *attr)
{
	return syscall(__NR_sched_getattr, pid, attr);
}
