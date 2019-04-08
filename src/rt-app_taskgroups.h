#ifndef _RTAPP_TASKGROUPS_H
#define _RTAPP_TASKGROUPS_H

#include "rt-app_types.h"

taskgroup_data_t *alloc_taskgroup(size_t size);
taskgroup_data_t *find_taskgroup(char *name);

void initialize_cgroups(void);
void add_cgroups(void);
void remove_cgroups(void);

#endif /* _RTAPP_TASKGROUPS_H */
