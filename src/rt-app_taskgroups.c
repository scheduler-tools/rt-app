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

static int cgroup_attach_task(char *name);

void set_thread_taskgroup(thread_data_t *data, taskgroup_data_t *tg)
{
	if (!tg)
		return;

	if (data->curr_taskgroup_data == tg)
		return;

	log_debug("[%d] set task [%s] taskgroup [%s]", data->ind, data->name, tg->name);

	if (cgroup_attach_task(tg->name)) {
		log_critical(PIN "cannot attach task to taskgroup [%s]", tg->name);
		exit(EXIT_FAILURE);
	}

	data->curr_taskgroup_data = tg;
}

void reset_thread_taskgroup(void)
{
	if (!ctrl.nr_tgs)
		return;

	cgroup_attach_task("/");
}

static int cgroup_check_cpu_controller(void)
{
	int dummy[2], enabled, ret = 0;
	FILE *cgroups;
	char buf[512];

	cgroups = fopen("/proc/cgroups", "re");

	if (!cgroups) {
		perror("fopen");
		goto err;
	}

	/* Ignore the first line as it contains the header. */
	if (!fgets(buf, sizeof(buf), cgroups)) {
		perror("fgets");
		goto err;
	}

	while (!feof(cgroups)) {
		/*
		 * Only interested in 'subsys_name' and 'enabled' column
		 * of /proc/cgroups, not in 'hierarchy' or 'num_cgroups'.
		 */
		if (fscanf(cgroups, "%s %d %d %d", buf, &dummy[0], &dummy[1],
			   &enabled) < 4) {
			perror("fscanf");
			goto err;
		}

		if (!strcmp(buf, "cpu") && enabled == 1)
			goto done;
	}
err:
	ret = -1;
done:
	if (cgroups)
		fclose(cgroups);

	return ret;
}

static int cgroup_get_cpu_controller_mount_point(void)
{
	struct mntent *ent;
	FILE *mounts;
	int ret = -1;

	mounts = setmntent("/proc/mounts", "re");

	if (!mounts) {
		perror("setmntent");
		return ret;
	}

	while (ent = getmntent(mounts)) {
		if (strcmp(ent->mnt_type, "cgroup"))
			continue;

		if (!hasmntopt(ent, "cpu"))
			continue;

		ctrl.mount_point = malloc(strlen(ent->mnt_dir) + 1);
		if (!ctrl.mount_point) {
			perror("malloc");
			break;
		}

		strcpy(ctrl.mount_point, ent->mnt_dir);

		log_debug(PIN "cgroup cpu controller mountpoint [%s] found", ent->mnt_dir);
		ret = 0;
		break;
	}

	endmntent(mounts);
	return ret;
}

void initialize_cgroups(void)
{
	if (!ctrl.nr_tgs)
		return;

	if (cgroup_check_cpu_controller()) {
		log_error(PIN "no cgroup cpu controller found");
		exit(EXIT_FAILURE);
	}

	if (cgroup_get_cpu_controller_mount_point()) {
		log_error(PIN "no cgroup cpu controller mointpoint found");
		exit(EXIT_FAILURE);
	}
}

static int cgroup_mkdir(const char *name, int *offset)
{
	char *dir = NULL, *path = NULL, del;
	int i = 0, ret = 0, y;
	size_t size;

	*offset = -1;

	if (!name || !strcmp(name, "/"))
		goto error;

	dir = strdup(name);
	if (!dir) {
		log_error(PIN "cannot duplicate string [%s]", name);
		ret = -1;
		goto error;
	}

	size = strlen(ctrl.mount_point) + strlen(dir) + 1;
	path = malloc(size);
	if (!path) {
		perror("malloc");
		ret = -1;
		goto error;
	}

	do {
		y = i;

		while (dir[i] == '/')
			i++;

		if (dir[i] == '\0')
			goto error;

		while (dir[i] != '\0' && dir[i] != '/')
			i++;

		del = dir[i];
		dir[i] = '\0';

		snprintf(path, size, "%s%s", ctrl.mount_point, dir);

		ret = mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		if (ret) {
			switch (errno) {
			case EEXIST:
				log_debug(PIN "cgroup [%s] exists, continue ...", dir);
				ret = 0;
				goto next;
			default:
				log_error(PIN "cgroup [%s] unhandled error (%s)", dir, strerror(errno));
				goto error;
			}
		} else if (*offset == -1) {
			*offset = y;
		}
next:
		dir[i] = del;
	} while (dir[i]);
error:
	free(dir);
	free(path);
	return ret;
}

static int cgroup_rmdir(char *name, int offset)
{
	char *dir = NULL, *path = NULL, *del;
	int ret = 0;
	size_t size, last;

	if (offset < 0)
		goto error;

	dir = strdup(name);
	if (!dir) {
		log_error(PIN "cannot duplicate string [%s]", name);
		ret = -1;
		goto error;
	}

	/* Remove trailing slashes. */
	while (last = strlen(dir) - 1, dir[last] == '/')
		dir[last] = '\0';

	size = strlen(ctrl.mount_point) + strlen(dir) + 1;
	path = malloc(size);
	if (!path) {
		perror("malloc");
		ret = -1;
		goto error;
	}

	while (1) {
		snprintf(path, size, "%s%s", ctrl.mount_point, dir);

		ret = rmdir(path);
		if (ret) {
			switch (errno) {
			case ENOTEMPTY:
				log_debug(PIN "cgroup [%s] not empty, continue ...", dir);
				ret = 0;
				break;
			case EBUSY:
				log_debug(PIN "cgroup [%s] is busy, continue ...", dir);
				ret = 0;
				break;
			case ENOENT:
				log_debug(PIN "cgroup [%s] doesn't exist, continue ...", dir);
				ret = 0;
				break;
			default:
				log_error(PIN "cgroup [%s] unhandled error (%s)", dir, strerror(errno));
				break;
			}
			break;
		}

		del = strrchr(dir, '/');
		if (!del)
			break;
		*del = '\0';

		/* Remove trailing slashes and adapt delimiter. */
		while (last = strlen(dir) - 1, dir[last] == '/') {
			del = &dir[last];
			*del = '\0';
		}

		if (del <= dir + offset)
			break;
	}
error:
	free(dir);
	free(path);
	return ret;
}

void add_cgroups(void)
{
	taskgroup_data_t *tg = ctrl.tg_array;
	int i;

	for (i = 0; i < ctrl.nr_tgs; i++, tg++) {
		if (cgroup_mkdir(tg->name, &tg->offset)) {
			log_critical(PIN "cannot create cgroup [%s]", tg->name);
			exit(EXIT_FAILURE);
		}
		log_debug(PIN "cgroup [%s] added", tg->name);
	}
}

void remove_cgroups(void)
{
	taskgroup_data_t *tg = &ctrl.tg_array[ctrl.nr_tgs - 1];
	int i;

	if (!ctrl.mount_point)
		return;

	for (i = ctrl.nr_tgs - 1; i >= 0; i--, tg--) {
		if (cgroup_rmdir(tg->name, tg->offset)) {
			log_critical(PIN "cannot remove cgroup [%s]", tg->name);
			exit(EXIT_FAILURE);
		}
		log_debug(PIN "cgroup [%s] removed", tg->name);
	}
}

static int cgroup_attach_task(char *name)
{
	char *file, *path;
	int ret = -1;
	FILE *tasks;
	size_t size;

	file = strcmp(name, "/") ? "/tasks" : "tasks";
	size = strlen(ctrl.mount_point) + strlen(name) + strlen(file) + 1;
	path = malloc(size);
	if (!path) {
		perror("malloc");
		goto error;
	}

	sprintf(path, "%s%s%s", ctrl.mount_point, name, file);
	tasks = fopen(path, "we");
	if (!tasks) {
		perror("fopen");
		goto error;
	}

	if (fprintf(tasks, "%d", gettid()) < 0) {
		perror("fprintf");
		goto error;
	}

	if (fclose(tasks)) {
		perror("fclose");
		goto error;
	}

	ret = 0;
error:
	free(path);
	return ret;
}
