#include "dl_syscalls.h"

int sched_setattr(pid_t pid,
		      const struct sched_attr *attr)
{
	return syscall(__NR_sched_setattr, pid, attr);
}

int sched_getattr(pid_t pid, struct sched_attr *attr)
{
	return syscall(__NR_sched_getattr, pid, attr);
}
