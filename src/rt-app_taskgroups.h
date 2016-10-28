#ifndef _RTAPP_TASKGROUPS_H
#define _RTAPP_TASKGROUPS_H

#include "rt-app_types.h"

typedef struct _taskgroup_ctrl_t
{
	char mount_point[FILENAME_MAX];
	int in_use;
} taskgroup_ctrl_t;

int init_taskgroups(void);
void create_taskgroups(rtapp_options_t *opts);
void remove_taskgroups(rtapp_options_t *opts);
void set_taskgroup(thread_data_t *data, taskgroup_t *tg);
void reset_taskgroup(thread_data_t *data);
taskgroup_ctrl_t *get_taskgroup_ctrl(void);

#endif /* _RTAPP_TASKGROUPS_H */
