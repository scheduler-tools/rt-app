#ifndef _RTAPP_TASKGROUPS_H
#define _RTAPP_TASKGROUPS_H

#include "rt-app_types.h"

taskgroup_data_t *alloc_taskgroup(size_t size);
taskgroup_data_t *find_taskgroup(char *name);
void set_thread_taskgroup(thread_data_t *data, taskgroup_data_t *tg);
void reset_thread_taskgroup(void);

void initialize_cgroups(void);
void add_cgroups(void);
void remove_cgroups(void);

#endif /* _RTAPP_TASKGROUPS_H */
