#include <stdlib.h>
#include <string.h>
#include <mntent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <errno.h>

#include "config.h"
#include "rt-app_utils.h"
#include "rt-app_taskgroups.h"

#define PIN "[tg] "

typedef struct _taskgroup_ctrl_t
{
	char *mount_point;
	taskgroup_data_t *tg_array;
	unsigned int nr_tgs;
} taskgroup_ctrl_t;

static taskgroup_ctrl_t ctrl;

const static unsigned int max_nbr_tgs = 32;

static void initialize_taskgroups(void)
{
	size_t size = max_nbr_tgs * sizeof(taskgroup_data_t);

	ctrl.tg_array = malloc(size);
	if (!ctrl.tg_array) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}
}

taskgroup_data_t *alloc_taskgroup(size_t size)
{
	taskgroup_data_t *tg;

	if (!ctrl.nr_tgs)
		initialize_taskgroups();

	if (ctrl.nr_tgs >= max_nbr_tgs) {
		log_error(PIN "# taskgroups exceeds max # taskgroups [%u]",
			   max_nbr_tgs);
		return NULL;
	}

	tg = &ctrl.tg_array[ctrl.nr_tgs++];

	tg->name = malloc(size);
	if (!tg->name) {
		perror("malloc");
		return NULL;
	}
	tg->offset = 0;

	log_debug(PIN "# taskgroups allocated [%u]", ctrl.nr_tgs);

	return tg;
}

taskgroup_data_t *find_taskgroup(char *name)
{
	taskgroup_data_t *tg = ctrl.tg_array;
	unsigned int i;

	for (i = 0; i < ctrl.nr_tgs; i++, tg++)
		if (!strcmp(tg->name, name))
			return tg;

	return NULL;
}
