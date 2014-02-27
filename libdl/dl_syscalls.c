#include "dl_syscalls.h"

int sched_setattr(pid_t pid,
		      const struct sched_attr *attr,
		      unsigned int flags)
{
	return syscall(__NR_sched_setattr, pid, attr, flags);
}

int sched_getattr(pid_t pid,
		      struct sched_attr *attr,
		      unsigned int size,
		      unsigned int flags)
{
	return syscall(__NR_sched_getattr, pid, attr, size, flags);
}
