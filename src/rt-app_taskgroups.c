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

static taskgroup_ctrl_t tg_ctrl;

static int cgroup_check_cpu_controller(void)
{
	int dummy[3], ret = 0;
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
		 * Only interested in 'subsys_name', not in 'hierarchy',
		 * 'num_cgroups' or 'enabled' column of /proc/cgroups .
		 */
		if (fscanf(cgroups, "%s %d %d %d", buf, &dummy[0], &dummy[1],
			   &dummy[2]) < 4) {
			perror("fscanf");
			goto err;
		}

		if (!strcmp(buf, "cpu"))
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

	mounts = fopen("/proc/mounts", "re");

	if (!mounts) {
		perror("fopen");
		return ret;
	}

	while (ent = getmntent(mounts)) {
		if (strcmp(ent->mnt_type, "cgroup"))
			continue;

		if (!hasmntopt(ent, "cpu"))
			continue;

		strcpy(tg_ctrl.mount_point, ent->mnt_dir);

		log_debug("cgroup cpu controller mountpoint [%s] found", ent->mnt_dir);
		ret = 0;
		break;
	}

	fclose(mounts);
	return ret;
}

static int cgroup_attach_task(char *name, pid_t tid)
{
	char *path, *file;
	FILE *tasks;
	int ret = -1;

	path = malloc(FILENAME_MAX);
	if (!path) {
		perror("malloc");
		goto error;
	}

	file = strcmp(name, "/") ? "/tasks" : "tasks";

	if (strlen(tg_ctrl.mount_point) + strlen(name) + strlen(file) > FILENAME_MAX - 1) {
		log_error("cgroup [%s] no space to create cgroup filename", name);
		goto error;
	}

	sprintf(path, "%s%s%s", tg_ctrl.mount_point, name, file);

	tasks = fopen(path, "we");
	if (!tasks) {
		perror("fopen");
		goto error;
	}

	if (fprintf(tasks, "%d", tid) < 0) {
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

static int cgroup_mkdir(const char *name, int *offset)
{
	char *dir = NULL, *path = NULL, sep;
	int i = 0, ret = 0, y;

	*offset = -1;

	if (!name || !strcmp(name, "/"))
		goto error;

	dir = strdup(name);
	if (!dir) {
		log_error("cannot duplicate string [%s]", name);
		ret = -1;
		goto error;
	}

	path = malloc(FILENAME_MAX);
	if (!path) {
		perror("malloc");
		ret = -1;
		goto error;
	}

	do {
		y = i;

		while (dir[i] == '/')
			i++;

		while (dir[i] != '\0' && dir[i] != '/')
			i++;

		sep = dir[i];
		dir[i] = '\0';

		snprintf(path, FILENAME_MAX, "%s%s", tg_ctrl.mount_point, dir);

		ret = mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		if (ret) {
			switch (errno) {
			case EEXIST:
				log_debug("cgroup [%s] exists, continue ...", dir);
				ret = 0;
				goto next;
			default:
				log_error("cgroup [%s] unhandled error (%s)", dir, strerror(errno));
				goto error;
			}
		} else if (*offset == -1) {
			*offset = y;
		}

		log_debug("cgroup [%s] created", dir);
next:
		dir[i] = sep;
	} while (dir[i]);
error:
	free(dir);
	free(path);
	return ret;
}

static int cgroup_rmdir(char *name, int offset)
{
	char *dir = NULL, *path = NULL, *sep;
	int ret = 0;

	if (offset < 0)
		goto error;

	dir = strdup(name);
	if (!dir) {
		log_error("cannot duplicate string [%s]", name);
		ret = -1;
		goto error;
	}

	path = malloc(FILENAME_MAX);
	if (!path) {
		perror("malloc");
		ret = -1;
		goto error;
	}

	while (1) {
		snprintf(path, FILENAME_MAX, "%s%s", tg_ctrl.mount_point, dir);

		ret = rmdir(path);
		if (ret) {
			switch (errno) {
			case ENOTEMPTY:
				log_debug("cgroup [%s] not empty, continue ...", dir);
				ret = 0;
				break;
			case EBUSY:
				log_debug("cgroup [%s] is busy, continue ...", dir);
				ret = 0;
				break;
			case ENOENT:
				log_debug("cgroup [%s] doesn't exist, continue ...", dir);
				ret = 0;
				break;
			default:
				log_error("cgroup [%s] unhandled error (%s)", dir, strerror(errno));
				break;
			}
			break;
		}

		log_debug("cgroup [%s] removed", dir);

		sep = strrchr(dir, '/');
		if (!sep || sep <= dir + offset)
			break;
		*sep = '\0';
	}
error:
	free(dir);
	free(path);
	return ret;
}

static void create_taskgroup(taskgroup_t *tg)
{
	if (!tg)
		return;

	log_debug("create taskgroup [%s]", tg->name);

	if (cgroup_mkdir(tg->name, &tg->offset)) {
		log_critical("cannot create taskgroup [%s]", tg->name);
		exit(EXIT_FAILURE);
	}
}

void create_taskgroups(rtapp_options_t *opts)
{
	int i, y;

	for (i = 0; i < opts->nthreads; i++) {
		thread_data_t *tdata = &opts->threads_data[i];

		for (y = 0; y < tdata->nphases; y++) {
			phase_data_t *pdata = &tdata->phases[y];

			create_taskgroup(pdata->taskgroup);
		}
	}
}

static void remove_taskgroup(taskgroup_t *tg)
{
	if (!tg)
		return;

	log_debug("remove taskgroup [%s]", tg->name);

	if (cgroup_rmdir(tg->name, tg->offset)) {
		log_critical("cannot remove taskgroup [%s]", tg->name);
		exit(EXIT_FAILURE);
	}
}

void remove_taskgroups(rtapp_options_t *opts)
{
	int i, y;

	/*
	 * Remove taskgroups in the reverse order they were created in to let
	 * the taskgroup->offset ownership mechanism work.
	 */
	for (i = opts->nthreads - 1; i >= 0; i--) {
		thread_data_t *tdata = &opts->threads_data[i];

		for (y = tdata->nphases - 1; y >= 0; y--) {
			phase_data_t *pdata = &tdata->phases[y];

			remove_taskgroup(pdata->taskgroup);
		}
	}
}

void set_taskgroup(thread_data_t *data, taskgroup_t *tg)
{
	if (!tg)
		return;

	if (data->curr_taskgroup && !strcmp(data->curr_taskgroup->name, tg->name))
		return;

	log_debug("[%d] set task [%s] taskgroup [%s]", data->ind, data->name, tg->name);

	if (cgroup_attach_task(tg->name, data->tid)) {
		log_critical("cannot attach task to taskgroup [%s]", tg->name);
		exit(EXIT_FAILURE);
	}

	data->curr_taskgroup = tg;
}

void reset_taskgroup(thread_data_t *data)
{
	taskgroup_t tg;

	if (!tg_ctrl.in_use)
		return;

	strcpy(tg.name, "/");
	set_taskgroup(data, &tg);
}

int init_taskgroups(void)
{
	if (!tg_ctrl.in_use)
		return 0;

	if (cgroup_check_cpu_controller()) {
		log_error("no cgroup cpu controller found");
		return -1;
	}

	if (cgroup_get_cpu_controller_mount_point()) {
		log_error("no cgroup cpu controller mointpoint found");
		return -1;
	}

	return 0;
}

taskgroup_ctrl_t *get_taskgroup_ctrl(void)
{
	return &tg_ctrl;
}
