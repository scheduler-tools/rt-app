/*
This file is part of rt-app - https://launchpad.net/rt-app
Copyright (C) 2010  Giacomo Bagnoli <g.bagnoli@asidev.com>
Copyright (C) 2014  Juri Lelli <juri.lelli@gmail.com>
Copyright (C) 2014  Vincent Guittot <vincent.guittot@linaro.org>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

/* for CPU_SET macro */
#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <json-c/json.h>

#include "rt-app_utils.h"
#include "rt-app_taskgroups.h"
#include "rt-app_parse_config.h"

#define PFX "[json] "
#define PFL "         "PFX
#define PIN PFX"    "
#define PIN2 PIN"    "
#define PIN3 PIN2"    "
#define JSON_FILE_BUF_SIZE 4096
#define DEFAULT_MEM_BUF_SIZE (4 * 1024 * 1024)

#ifndef TRUE
#define TRUE true
#define FALSE false
#endif

/* redefine foreach as in <json/json_object.h> but to be ANSI
 * compatible */
#define foreach(obj, entry, key, val, idx)				\
	for ( ({ idx = 0; entry = json_object_get_object(obj)->head;});	\
		({ if (entry) { key = (char*)entry->k;			\
				val = (struct json_object*)entry->v;	\
			      };					\
		   entry;						\
		 }							\
		);							\
		({ entry = entry->next; idx++; })			\
	    )
/* this macro set a default if key not present, or give an error and exit
 * if key is present but does not have a default */
#define set_default_if_needed(key, value, have_def, def_value) do {	\
	if (!value) {							\
		if (have_def) {						\
			log_info(PIN "key: %s <default> %d", key, def_value);\
			return def_value;				\
		} else {						\
			log_critical(PFX "Key %s not found", key);	\
			exit(EXIT_INV_CONFIG);				\
		}							\
	}								\
} while(0)

/* same as before, but for string, for which we need to strdup in the
 * default value so it can be a literal */
#define set_default_if_needed_str(key, value, have_def, def_value) do {	\
	if (!value) {							\
		if (have_def) {						\
			if (!def_value) {				\
				log_info(PIN "key: %s <default> NULL", key);\
				return NULL;				\
			}						\
			log_info(PIN "key: %s <default> %s",		\
				  key, def_value);			\
			return strdup(def_value);			\
		} else {						\
			log_critical(PFX "Key %s not found", key);	\
			exit(EXIT_INV_CONFIG);				\
		}							\
	}								\
}while (0)

/* get an object obj and check if its type is <type>. If not, print a message
 * (this is what parent and key are used for) and exit
 */
static inline void
assure_type_is(struct json_object *obj,
	       struct json_object *parent,
	       const char *key,
	       enum json_type type)
{
	if (!json_object_is_type(obj, type)) {
		log_critical("Invalid type for key %s", key);
		log_critical("%s", json_object_to_json_string(parent));
		exit(EXIT_INV_CONFIG);
	}
}

/* search a key (what) in object "where", and return a pointer to its
 * associated object. If nullable is false, exit if key is not found */
static inline struct json_object*
get_in_object(struct json_object *where,
	      const char *what,
	      int nullable)
{
	struct json_object *to;
	json_bool ret;
	ret = json_object_object_get_ex(where, what, &to);
	if (!nullable && !ret) {
		log_critical(PFX "Error while parsing config\n" PFL);
		exit(EXIT_INV_CONFIG);
	}
	if (!nullable && strcmp(json_object_to_json_string(to), "null") == 0) {
		log_critical(PFX "Cannot find key %s", what);
		exit(EXIT_INV_CONFIG);
	}
	return to;
}

static inline bool
check_type_is(struct json_object *where,
	      const char *key,
	      enum json_type type)
{
	struct json_object *value = get_in_object(where, key, TRUE);
	if (!value)
		return false;
	return json_object_is_type(value, type);
}

static inline int
get_int_value_from(struct json_object *where,
		   const char *key,
		   int have_def,
		   int def_value)
{
	struct json_object *value;
	int i_value;
	value = get_in_object(where, key, have_def);
	set_default_if_needed(key, value, have_def, def_value);
	assure_type_is(value, where, key, json_type_int);
	i_value = json_object_get_int(value);
	log_info(PIN "key: %s, value: %d, type <int>", key, i_value);
	return i_value;
}

static inline int
get_bool_value_from(struct json_object *where,
		    const char *key,
		    int have_def,
		    int def_value)
{
	struct json_object *value;
	int b_value;
	value = get_in_object(where, key, have_def);
	set_default_if_needed(key, value, have_def, def_value);
	assure_type_is(value, where, key, json_type_boolean);
	b_value = json_object_get_boolean(value);
	log_info(PIN "key: %s, value: %d, type <bool>", key, b_value);
	return b_value;
}

static inline char*
get_string_value_from(struct json_object *where,
		      const char *key,
		      int have_def,
		      const char *def_value)
{
	struct json_object *value;
	char *s_value;
	value = get_in_object(where, key, have_def);
	set_default_if_needed_str(key, value, have_def, def_value);
	if (json_object_is_type(value, json_type_null)) {
		log_info(PIN "key: %s, value: NULL, type <string>", key);
		return NULL;
	}
	assure_type_is(value, where, key, json_type_string);
	s_value = strdup(json_object_get_string(value));
	log_info(PIN "key: %s, value: %s, type <string>", key, s_value);
	return s_value;
}

static void init_mutex_resource(rtapp_resource_t *data, const rtapp_options_t *opts)
{
	log_info(PIN3 "Init: %s mutex", data->name);

	pthread_mutexattr_init(&data->res.mtx.attr);
	if (opts->pi_enabled) {
		pthread_mutexattr_setprotocol(
				&data->res.mtx.attr,
				PTHREAD_PRIO_INHERIT);
	}
	pthread_mutex_init(&data->res.mtx.obj,
			&data->res.mtx.attr);
}

static void init_timer_resource(rtapp_resource_t *data, const rtapp_options_t *opts)
{
	log_info(PIN3 "Init: %s timer", data->name);
	data->res.timer.init = 0;
	data->res.timer.relative = 1;
}

static void init_cond_resource(rtapp_resource_t *data, const rtapp_options_t *opts)
{
	log_info(PIN3 "Init: %s wait", data->name);

	pthread_condattr_init(&data->res.cond.attr);
	pthread_cond_init(&data->res.cond.obj,
			&data->res.cond.attr);
}

static void init_membuf_resource(rtapp_resource_t *data, const rtapp_options_t *opts)
{
	log_info(PIN3 "Init: %s membuf", data->name);

	data->res.buf.ptr = malloc(opts->mem_buffer_size);
	data->res.buf.size = opts->mem_buffer_size;
}

static void init_iodev_resource(rtapp_resource_t *data, const rtapp_options_t *opts)
{
	log_info(PIN3 "Init: %s io device", data->name);

	data->res.dev.fd = open(opts->io_device, O_CREAT | O_WRONLY, 0644);
}

static void init_barrier_resource(rtapp_resource_t *data, const rtapp_options_t *opts)
{
	log_info(PIN3 "Init: %s barrier", data->name);

	/* each task waiting for this resource will increment this counter.
	 * start at -1 so that when we see this is zero we are the last man
	 * to enter the sync point and should wake everyone else.
	 */
	data->res.barrier.waiting = -1;
	pthread_mutexattr_init(&data->res.barrier.m_attr);
	if (opts->pi_enabled) {
		pthread_mutexattr_setprotocol(
				&data->res.barrier.m_attr,
				PTHREAD_PRIO_INHERIT);
	}
	pthread_mutex_init(&data->res.barrier.m_obj,
			&data->res.barrier.m_attr);

	pthread_cond_init(&data->res.barrier.c_obj, NULL);
}

static void
init_resource_data(const char *name, int type, rtapp_resources_t *resources_table,
		int idx, const rtapp_options_t *opts)
{
	rtapp_resource_t *data = &(resources_table->resources[idx]);

	/* common and defaults */
	data->index = idx;
	data->name = strdup(name);
	data->type = type;

	switch (data->type) {
		case rtapp_mutex:
			init_mutex_resource(data, opts);
			break;
		case rtapp_timer:
		case rtapp_timer_unique:
			init_timer_resource(data, opts);
			break;
		case rtapp_wait:
			init_cond_resource(data, opts);
			break;
		case rtapp_mem:
			init_membuf_resource(data, opts);
			break;
		case rtapp_iorun:
			init_iodev_resource(data, opts);
			break;
		case rtapp_barrier:
			init_barrier_resource(data, opts);
			break;
		default:
			break;
	}
}

static void
parse_resource_data(const char *name, struct json_object *obj, int idx,
		  rtapp_resource_t *data, const rtapp_options_t *opts)
{
	char *type;
	char def_type[RTAPP_RESOURCE_DESCR_LENGTH];

	log_info(PFX "Parsing resources %s [%d]", name, idx);

	/* resource type */
	resource_to_string(0, def_type);
	type = get_string_value_from(obj, "type", TRUE, def_type);
	if (string_to_resource(type, &data->type) != 0) {
		log_critical(PIN2 "Invalid type of resource %s", type);
		exit(EXIT_INV_CONFIG);
	}

	/*
	 * get_string_value_from allocate the string so with have to free it
	 * once useless
	 */
	free(type);

	init_resource_data(name, data->type, opts->resources, idx, opts);
}

static int
add_resource_data(const char *name, int type, rtapp_resources_t **resources_table, rtapp_options_t *opts)
{
	int idx, size;
	rtapp_resources_t *table = *resources_table;

	idx = table->nresources;

	log_info(PIN2 "Add new resource %s [%d] type %d", name, idx, type);

	table->nresources++;

	size = sizeof(rtapp_resources_t) + sizeof(rtapp_resource_t) * table->nresources;

	*resources_table = realloc(*resources_table, size);

	if (!*resources_table) {
		log_error("Failed to allocate memory for resource %s", name);
		return -1;
	}

	/*
	 * We can't reuse table as *resources_table might have changed following
	 * realloc
	 */
	init_resource_data(name, type, *resources_table, idx, opts);

	return idx;
}

static void
parse_resources(struct json_object *resources, rtapp_options_t *opts)
{
	struct lh_entry *entry; char *key; struct json_object *val; int idx;
	int size;

	log_info(PFX "Parsing resource section");

	/*
	 * Create at least an "empty" struct that will then be filled either will
	 * parsing resources table or when parsing task's event
	 */
	opts->resources = malloc(sizeof(rtapp_resources_t));
	if (!opts->resources) {
		log_error("Failed to allocate memory for resources");
		exit(EXIT_FAILURE);
	}

	opts->resources->nresources = 0;

	if (!resources) {
		log_info(PFX "No resource section Found");
		return;
	}

	if (json_object_is_type(resources, json_type_object)) {
		opts->resources->nresources = 0;
		foreach(resources, entry, key, val, idx) {
			opts->resources->nresources++;
		}

		log_info(PFX "Found %d Resources", opts->resources->nresources);
		size = sizeof(rtapp_resources_t) + sizeof(rtapp_resource_t) * opts->resources->nresources;

		opts->resources = realloc(opts->resources, size);
		if (!opts->resources) {
				log_error("Failed to allocate memory for resources");
				exit(EXIT_FAILURE);
		}

		foreach (resources, entry, key, val, idx) {
			parse_resource_data(key, val, idx, &opts->resources->resources[idx], opts);
		}
	}
}

static int get_resource_index(const char *name, int type, rtapp_resources_t **resources_table, rtapp_options_t *opts)
{
	int nresources = (*resources_table)->nresources;
	rtapp_resource_t *resources = (*resources_table)->resources;
	int i = 0;
	log_info(PIN "get_resource_index %d events", nresources);

	while ((i < nresources) && ((strcmp(resources[i].name, name) != 0) || (resources[i].type != type)))
		i++;

	if (i >= nresources)
		i = add_resource_data(name, type, resources_table, opts);

	return i;
}

static char* create_unique_name(char *tmp, int size, const char* ref, long tag)
{
	snprintf(tmp, size, "%s%lx", ref, (long)(tag));
	return tmp;
}

static void
parse_task_event_data(char *name, struct json_object *obj,
		  event_data_t *data, thread_data_t *tdata, rtapp_options_t *opts)
{
	rtapp_resources_t **resources_table = tdata->global_resources;
	rtapp_resource_t *rdata, *ddata;
	char unique_name[22];
	const char *ref;
	char *tmp;
	long tag = (long)tdata;
	int i;

	if (!strncmp(name, "run", strlen("run")) ||
			!strncmp(name, "sleep", strlen("sleep"))) {

		if (!json_object_is_type(obj, json_type_int))
			goto unknown_event;

		data->duration = json_object_get_int(obj);

		if (!strncmp(name, "sleep", strlen("sleep")))
			data->type = rtapp_sleep;
		else if (!strncmp(name, "runtime", strlen("runtime")))
			data->type = rtapp_runtime;
		else
			data->type = rtapp_run;

		log_info(PIN2 "type %d duration %d", data->type, data->duration);
		strncpy(data->name, name, sizeof(data->name)-1);
		return;
	}

	if (!strncmp(name, "mem", strlen("mem")) ||
			!strncmp(name, "iorun", strlen("iorun"))) {
		if (!json_object_is_type(obj, json_type_int))
			goto unknown_event;

		/* create an unique name for per-thread buffer */
		ref = create_unique_name(unique_name, sizeof(unique_name), "mem", tag);
		i = get_resource_index(ref, rtapp_mem, resources_table, opts);
		data->res = i;
		data->count = json_object_get_int(obj);

		/* A single IO devices for all threads */
		if (strncmp(name, "iorun", strlen("iorun")) == 0) {
			i = get_resource_index("io_device", rtapp_iorun, resources_table, opts);
			data->dep = i;
		};

		if (!strncmp(name, "mem", strlen("mem")))
			data->type = rtapp_mem;
		else
			data->type = rtapp_iorun;

		log_info(PIN2 "type %d count %d", data->type, data->count);
		strncpy(data->name, unique_name, sizeof(data->name)-1);
		return;
	}

	if (!strncmp(name, "lock", strlen("lock")) ||
			!strncmp(name, "unlock", strlen("unlock"))) {

		if (!json_object_is_type(obj, json_type_string))
			goto unknown_event;

		ref = json_object_get_string(obj);
		i = get_resource_index(ref, rtapp_mutex, resources_table, opts);

		data->res = i;

		if (!strncmp(name, "lock", strlen("lock")))
			data->type = rtapp_lock;
		else
			data->type = rtapp_unlock;

		rdata = &((*resources_table)->resources[data->res]);

		log_info(PIN2 "type %d target %s [%d]", data->type, rdata->name, rdata->index);
		snprintf(data->name, sizeof(data->name)-1, "%s:%s",
			 name, rdata->name);
		return;
	}

	if (!strncmp(name, "signal", strlen("signal")) ||
			!strncmp(name, "broad", strlen("broad"))) {

		if (!strncmp(name, "signal", strlen("signal")))
			data->type = rtapp_signal;
		else
			data->type = rtapp_broadcast;

		if (!json_object_is_type(obj, json_type_string))
			goto unknown_event;

		ref = json_object_get_string(obj);
		i = get_resource_index(ref, rtapp_wait, resources_table, opts);

		data->res = i;

		rdata = &((*resources_table)->resources[data->res]);

		log_info(PIN2 "type %d target %s [%d]", data->type, rdata->name, rdata->index);
		snprintf(data->name, sizeof(data->name)-1, "%s:%s",
			 name, rdata->name);
		return;
	}

	if (!strncmp(name, "wait", strlen("wait")) ||
			!strncmp(name, "sync", strlen("sync"))) {

		if (!strncmp(name, "wait", strlen("wait")))
			data->type = rtapp_wait;
		else
			data->type = rtapp_sig_and_wait;

		tmp = get_string_value_from(obj, "ref", TRUE, "unknown");
		i = get_resource_index(tmp, rtapp_wait, resources_table, opts);
		/*
		 * get_string_value_from allocate the string so with have to free it
		 * once useless
		 */
		free(tmp);

		data->res = i;

		tmp = get_string_value_from(obj, "mutex", TRUE, "unknown");
		i = get_resource_index(tmp, rtapp_mutex, resources_table, opts);
		/*
		 * get_string_value_from allocate the string so with have to free it
		 * once useless
		 */
		free(tmp);

		data->dep = i;

		rdata = &((*resources_table)->resources[data->res]);
		ddata = &((*resources_table)->resources[data->dep]);

		log_info(PIN2 "type %d target %s [%d] mutex %s [%d]", data->type, rdata->name, rdata->index, ddata->name, ddata->index);
		snprintf(data->name, sizeof(data->name)-1, "%s:%s:%s",
			 name, rdata->name, ddata->name);
		return;
	}

    if (!strncmp(name, "barrier", strlen("barrier"))) {

		if (!json_object_is_type(obj, json_type_string))
			goto unknown_event;

		data->type = rtapp_barrier;

		ref = json_object_get_string(obj);
		i = get_resource_index(ref, rtapp_barrier, resources_table, opts);

		data->res = i;
		rdata = &((*resources_table)->resources[data->res]);
		rdata->res.barrier.waiting += tdata->num_instances;

		log_info(PIN2 "type %d target %s [%d] %d users so far", data->type, rdata->name, rdata->index, rdata->res.barrier.waiting);
		snprintf(data->name, sizeof(data->name)-1, "%s:%s",
			 name, rdata->name);
		return;
	}

	if (!strncmp(name, "timer", strlen("timer"))) {

		tmp = get_string_value_from(obj, "ref", TRUE, "unknown");

		if (!strncmp(tmp, "unique", strlen("unique"))) {
			ref = create_unique_name(unique_name, sizeof(unique_name), tmp, tag);
			resources_table = &tdata->local_resources;
			data->type = rtapp_timer_unique;
		} else {
			ref = tmp;
			data->type = rtapp_timer;
		}

		data->res = get_resource_index(ref, data->type, resources_table, opts);

		/*
		 * get_string_value_from allocate the string so with have to free it
		 * once useless
		 */
		free(tmp);

		data->duration = get_int_value_from(obj, "period", TRUE, 0);

		rdata = &((*resources_table)->resources[data->res]);

		tmp = get_string_value_from(obj, "mode", TRUE, "relative");
		if (!strncmp(tmp, "absolute", strlen("absolute")))
			rdata->res.timer.relative = 0;
		free(tmp);

		log_info(PIN2 "type %d target %s [%d] period %d", data->type, rdata->name, rdata->index, data->duration);
		snprintf(data->name, sizeof(data->name)-1, "%s:%s",
			 name, rdata->name);
		return;
	}

	if (!strncmp(name, "resume", strlen("resume"))) {

		data->type = rtapp_resume;

		if (!json_object_is_type(obj, json_type_string))
			goto unknown_event;

		ref = json_object_get_string(obj);

		i = get_resource_index(ref, rtapp_wait, resources_table, opts);

		data->res = i;

		i = get_resource_index(ref, rtapp_mutex, resources_table, opts);

		data->dep = i;

		rdata = &((*resources_table)->resources[data->res]);
		ddata = &((*resources_table)->resources[data->dep]);

		log_info(PIN2 "type %d target %s [%d] mutex %s [%d]", data->type, rdata->name, rdata->index, ddata->name, ddata->index);
		snprintf(data->name, sizeof(data->name)-1, "%s:%s:%s",
			 name, rdata->name, ddata->name);
		return;
	}

	if (!strncmp(name, "suspend", strlen("suspend"))) {

		data->type = rtapp_suspend;

		if (!json_object_is_type(obj, json_type_string))
			goto unknown_event;

		ref = json_object_get_string(obj);

		i = get_resource_index(ref, rtapp_wait, resources_table, opts);

		data->res = i;

		i = get_resource_index(ref, rtapp_mutex, resources_table, opts);

		data->dep = i;

		rdata = &((*resources_table)->resources[data->res]);
		ddata = &((*resources_table)->resources[data->dep]);

		log_info(PIN2 "type %d target %s [%d] mutex %s [%d]", data->type, rdata->name, rdata->index, ddata->name, ddata->index);
		snprintf(data->name, sizeof(data->name)-1, "%s:%s:%s",
			 name, rdata->name, ddata->name);
		return;
	}

	if (!strncmp(name, "yield", strlen("yield"))) {
		data->type = rtapp_yield;
		log_info(PIN2 "type %d", data->type);
		strncpy(data->name, name, sizeof(data->name)-1);
		return;
	}

	if (!strncmp(name, "fork", strlen("fork"))) {

		data->type = rtapp_fork;

		if (!json_object_is_type(obj, json_type_string))
			goto unknown_event;

		ref = json_object_get_string(obj);

		i = get_resource_index(ref, rtapp_fork, resources_table, opts);

		data->res = i;

		rdata = &((*resources_table)->resources[data->res]);

		rdata->res.fork.ref = strdup(ref);
		rdata->res.fork.tdata = NULL;
		rdata->res.fork.nforks = 0;

		if (!rdata->res.fork.ref) {
			log_error("Failed to duplicate ref");
			exit(EXIT_FAILURE);
		}

		log_info(PIN2 "type %d target %s [%d]", data->type, rdata->name, rdata->index);
		snprintf(data->name, sizeof(data->name)-1, "%s:%s",
			 name, rdata->name);
		return;
	}

	log_error(PIN2 "Resource %s not found in the resource section !!!", ref);
	log_error(PIN2 "Please check the resource name or the resource section");

unknown_event:
	log_critical(PIN2 "Unknown or mismatch %s event type !!!", name);
	exit(EXIT_INV_CONFIG);
}

static char *events[] = {
	"lock",
	"unlock",
	"wait",
	"signal",
	"broad",
	"sync",
	"sleep",
	"runtime",
	"run",
	"timer",
	"suspend",
	"resume",
	"mem",
	"iorun",
	"yield",
	"barrier",
	"fork",
	NULL
};

static int
obj_is_event(char *name)
{
    char **pos;

    for (pos = events; *pos; pos++) {
	    char *event = *pos;
	    if (!strncmp(name, event, strlen(event)))
		    return 1;
    }

    return 0;
}

static void parse_cpuset_data(struct json_object *obj, cpuset_data_t *data)
{
	struct json_object *cpuset_obj, *cpu;
	unsigned int max_cpu = sysconf(_SC_NPROCESSORS_CONF) - 1;

	/* cpuset */
	cpuset_obj = get_in_object(obj, "cpus", TRUE);
	if (cpuset_obj) {
		unsigned int i;
		unsigned int cpu_idx;

		assure_type_is(cpuset_obj, obj, "cpus", json_type_array);
		data->cpuset_str = strdup(json_object_to_json_string(cpuset_obj));
		data->cpusetsize = sizeof(cpu_set_t);
		data->cpuset = malloc(data->cpusetsize);
		CPU_ZERO_S(data->cpusetsize, data->cpuset);
		for (i = 0; i < json_object_array_length(cpuset_obj); i++) {
			cpu = json_object_array_get_idx(cpuset_obj, i);
			cpu_idx = json_object_get_int(cpu);
			if (cpu_idx > max_cpu) {
				log_critical(PIN2 "Invalid cpu %u in cpuset %s", cpu_idx, data->cpuset_str);
				free(data->cpuset);
				free(data->cpuset_str);
				exit(EXIT_INV_CONFIG);
			}
			CPU_SET_S(cpu_idx, data->cpusetsize, data->cpuset);
		}
	} else {
		data->cpuset_str = strdup("-");
		data->cpuset = NULL;
		data->cpusetsize = 0;
	}
	log_info(PIN "key: cpus %s", data->cpuset_str);
}

static void parse_numa_data(struct json_object *obj, numaset_data_t *data)
{
	struct json_object *numaset_obj, *node;
	data->numaset = NULL;
	data->numaset_str = NULL;

	numaset_obj = get_in_object(obj, "nodes_membind", TRUE);
	if (numaset_obj) {
		unsigned int i;
		unsigned int node_idx;
		unsigned int max_node;

#if HAVE_LIBNUMA
		if(numa_available() == -1)
#endif
		{
			log_error(PIN2 "NUMA is not available");
			return;
		}

#if HAVE_LIBNUMA
		/* Get highest node number available on the current system */
		max_node = numa_max_node();

		assure_type_is(numaset_obj, obj, "nodes_membind", json_type_array);
		data->numaset_str = strdup(json_object_to_json_string(numaset_obj));
		data->numaset = numa_allocate_nodemask();
		numa_bitmask_clearall(data->numaset);
		for (i = 0; i < json_object_array_length(numaset_obj); i++) {
			node = json_object_array_get_idx(numaset_obj, i);
			node_idx = json_object_get_int(node);
			if (node_idx > max_node) {
				numa_bitmask_free(data->numaset);
				log_critical(PIN2 "Invalid node %u in numaset %s", node_idx, data->numaset_str);
				free(data->numaset_str);
				exit(EXIT_INV_CONFIG);
			}
			numa_bitmask_setbit(data->numaset, node_idx);
		}
		log_info(PIN "key: nodes_membind %s", data->numaset_str);
#endif
	}
}

static sched_data_t *parse_sched_data(struct json_object *obj, int def_policy)
{
	sched_data_t tmp_data = { .policy = same };
	char *def_str_policy;
	char *policy;
	int prior_def;

	/* Get default policy */
	def_str_policy = policy_to_string(def_policy);

	/* Get Policy */
	policy = get_string_value_from(obj, "policy", TRUE, def_str_policy);
	if (policy ){
		if (string_to_policy(policy, &tmp_data.policy) != 0) {
			log_critical(PIN2 "Invalid policy %s", policy);
			exit(EXIT_INV_CONFIG);
		}
	}

	/* Get priority */
	switch (tmp_data.policy) {
	case same:
		prior_def = THREAD_PRIORITY_UNCHANGED;
		break;
	case other:
	case idle:
		prior_def = DEFAULT_THREAD_NICE;
		break;
	case fifo:
	case rr:
		prior_def = DEFAULT_THREAD_PRIORITY;
		break;
	case deadline:
		prior_def = 0;
		break;
	default:
		/* unreachable due to string_to_policy() above */
		exit(EXIT_INV_CONFIG);
	}

	tmp_data.prio = get_int_value_from(obj, "priority", TRUE, prior_def);

	/* deadline params */
	tmp_data.runtime = get_int_value_from(obj, "dl-runtime", TRUE, 0);
	tmp_data.period = get_int_value_from(obj, "dl-period", TRUE, tmp_data.runtime);
	tmp_data.deadline = get_int_value_from(obj, "dl-deadline", TRUE, tmp_data.period);

	/* clamping params (-1: no changes ) */
	tmp_data.util_min = get_int_value_from(obj, "util_min", TRUE, -1);
	if (tmp_data.util_min != -1 && tmp_data.util_min > 1024) {
		log_critical(PIN2 "Invalid util_min %d (>1024)", tmp_data.util_min);
		exit(EXIT_INV_CONFIG);
	}
	tmp_data.util_max = get_int_value_from(obj, "util_max", TRUE, -1);
	if (tmp_data.util_max != -1 && tmp_data.util_max > 1024) {
		log_critical(PIN2 "Invalid util_max %d (>1024)", tmp_data.util_max);
		exit(EXIT_INV_CONFIG);
	}

	if (def_policy != same) {
		/* Support legacy grammar for thread object */
		if (!tmp_data.runtime)
			tmp_data.runtime = get_int_value_from(obj, "runtime", TRUE, 0);
		if (!tmp_data.period)
			tmp_data.period = get_int_value_from(obj, "period", TRUE, tmp_data.runtime);
		if (!tmp_data.deadline)
			tmp_data.deadline = get_int_value_from(obj, "deadline", TRUE, tmp_data.period);
	}

	/* Move from usec to nanosec */
	tmp_data.runtime *= 1000;
	tmp_data.period *= 1000;
	tmp_data.deadline *= 1000;

	/* Check if we found at least one meaningful scheduler parameter */
	if (tmp_data.prio != THREAD_PRIORITY_UNCHANGED ||
	    tmp_data.runtime || tmp_data.period || tmp_data.deadline ||
	    tmp_data.util_min != -1 || tmp_data.util_max != -1) {
		sched_data_t *new_data;

		/* At least 1 parameters has been set in the object */
		new_data = malloc(sizeof(sched_data_t));
		memcpy( new_data, &tmp_data,sizeof(sched_data_t));

		log_debug(PIN "key: set scheduler %d with priority %d", new_data->policy, new_data->prio);

		return new_data;
	}

	return NULL;
}

static taskgroup_data_t *parse_taskgroup_data(struct json_object *obj)
{
	char *name = get_string_value_from(obj, "taskgroup", TRUE, "");
	taskgroup_data_t *tg;

	if (!strlen(name))
		return NULL;

	if (*name != '/') {
		log_critical(PIN2 "Taskgroup [%s] has to start with [/]", name);
		exit(EXIT_INV_CONFIG);
	}

	tg = find_taskgroup(name);
	if (tg)
		return tg;
	tg = alloc_taskgroup(strlen(name) + 1);
	if (!tg) {
		log_critical(PIN2 "Cannot allocate taskgroup");
		exit(EXIT_FAILURE);
	}

	strcpy(tg->name, name);

	return tg;
}

static void check_taskgroup_policy_dep(phase_data_t *pdata, thread_data_t *tdata)
{
	/*
	 * Save sched_data as thread's current sched_data in case its policy_t is
	 * set to a valid scheduler policy.
	 */
	if (pdata->sched_data && pdata->sched_data->policy != same)
		tdata->curr_sched_data = pdata->sched_data;

	/* Save taskgroup_data as thread's current taskgroup_data. */
	if (pdata->taskgroup_data)
		tdata->curr_taskgroup_data = pdata->taskgroup_data;

	/*
	 * Detect policy/taskgroup misconfiguration: a task which specifies a
	 * taskgroup should not run in a policy other than SCHED_OTHER or
	 * SCHED_IDLE.
	 */
	if (tdata->curr_sched_data && tdata->curr_taskgroup_data) {
		policy_t policy = tdata->curr_sched_data->policy;

		if (policy != other && policy != idle) {
			log_critical(PIN2 "No taskgroup support for policy %s",
			             policy_to_string(policy));
			exit(EXIT_INV_CONFIG);
		}
	}
}

static void
parse_task_phase_data(struct json_object *obj,
		  phase_data_t *data, thread_data_t *tdata, rtapp_options_t *opts)
{
	/* used in the foreach macro */
	struct lh_entry *entry; char *key; struct json_object *val; int idx;
	int i;

	log_info(PFX "Parsing phase");

	/* loop */
	data->loop = get_int_value_from(obj, "loop", TRUE, 1);

	/* Count number of events */
	data->nbevents = 0;
	foreach(obj, entry, key, val, idx) {
		if (obj_is_event(key))
				data->nbevents++;
	}

	if (data->nbevents == 0) {
		log_critical(PIN "No events found. Task must have events or it's useless");
		exit(EXIT_INV_CONFIG);

	}

	log_info(PIN "Found %d events", data->nbevents);

	data->events = malloc(data->nbevents * sizeof(event_data_t));

	/* Parse events */
	i = 0;
	foreach(obj, entry, key, val, idx) {
		if (obj_is_event(key)) {
			log_info(PIN "Parsing event %s", key);
			parse_task_event_data(key, val, &data->events[i], tdata, opts);
			i++;
		}
	}
	parse_cpuset_data(obj, &data->cpu_data);
	parse_numa_data(obj, &data->numa_data);
	data->sched_data = parse_sched_data(obj, same);
	data->taskgroup_data = parse_taskgroup_data(obj);
}

static void
parse_task_data(char *name, struct json_object *obj, int index,
		  thread_data_t *data, rtapp_options_t *opts)
{
	struct json_object *phases_obj, *resources;

	log_info(PFX "Parsing task %s [%d]", name, index);

	/* common and defaults */
	/*
	 * Set a pointer to opt entry as we might end up to modify the localtion of
	 * global resources during realloc
	 */
	data->global_resources = &(opts->resources);

	/*
	 * Create at least an "empty" struct that will then be filled either will
	 * parsing resources table or when parsing task's event
	 */
	data->local_resources = malloc(sizeof(rtapp_resources_t));
	if (!data->local_resources) {
		log_error("Failed to allocate memory for local resources");
		exit(EXIT_FAILURE);
	}

	data->local_resources->nresources = 0;
	data->ind = index;
	data->name = strdup(name);
	data->lock_pages = opts->lock_pages;

	data->cpu_data.cpuset = NULL;
	data->cpu_data.cpuset_str = NULL;
	data->curr_cpu_data = NULL;
	data->def_cpu_data.cpuset = NULL;
	data->def_cpu_data.cpuset_str = NULL;

	data->numa_data.numaset = NULL;
	data->numa_data.numaset_str = NULL;
	data->curr_numa_data = NULL;

	/* cpuset */
	parse_cpuset_data(obj, &data->cpu_data);
	/* numa */
	parse_numa_data(obj, &data->numa_data);
	/* Scheduling policy */
	data->sched_data = parse_sched_data(obj, opts->policy);
	/* Taskgroup */
	data->taskgroup_data = parse_taskgroup_data(obj);

	/*
	 * Thread's current sched_data and taskgroup_data are used to detect
	 * policy/taskgroup misconfiguration.
	 */
	data->curr_sched_data = data->sched_data;
	data->curr_taskgroup_data = data->taskgroup_data;

	/* initial delay */
	data->delay = get_int_value_from(obj, "delay", TRUE, 0);

	/* It's the responsibility of the caller to set this if we were forked */
	data->forked = 0;
	data->num_instances = get_int_value_from(obj, "instance", TRUE, 1);

	/* Get phases */
	phases_obj = get_in_object(obj, "phases", TRUE);
	if (phases_obj) {
		/* used in the foreach macro */
		struct lh_entry *entry; char *key; struct json_object *val; int idx;

		assure_type_is(phases_obj, obj, "phases",
					json_type_object);

		log_info(PIN "Parsing phases section");
		data->nphases = 0;
		foreach(phases_obj, entry, key, val, idx) {
			data->nphases++;
		}

		log_info(PIN "Found %d phases", data->nphases);
		data->phases = malloc(sizeof(phase_data_t) * data->nphases);
		foreach(phases_obj, entry, key, val, idx) {
			log_info(PIN "Parsing phase %s", key);
			assure_type_is(val, phases_obj, key, json_type_object);
			parse_task_phase_data(val, &data->phases[idx], data, opts);
			/*
			 * Uses thread's current sched_data and taskgroup_data
			 * to detect policy/taskgroup misconfiguration.
			 */
			check_taskgroup_policy_dep(&data->phases[idx], data);
		}

		/* Get loop number */
		data->loop = get_int_value_from(obj, "loop", TRUE, -1);

	} else {
		data->nphases = 1;
		data->phases = malloc(sizeof(phase_data_t) * data->nphases);
		parse_task_phase_data(obj,  &data->phases[0], data, opts);

		/* There is no "phases" object which means that thread and phase will
		 * use same scheduling parameters. But thread object looks for default
		 * value when parameters are not defined whereas phase doesn't.
		 * We remove phase's scheduling policy which is a subset of thread's one
		 */
		free(data->phases[0].sched_data);
		data->phases[0].sched_data = NULL;

		/*
		 * Uses thread's current sched_data and taskgroup_data
		 * to detect policy/taskgroup misconfiguration.
		 */
		check_taskgroup_policy_dep(&data->phases[0], data);
		/*
		 * Get loop number:
		 *
		 * If loop is specified, we already parsed it in
		 * parse_task_phase_data() above, so we just need to remember
		 * that we don't want to loop forever.
		 *
		 * If not specified we want to loop forever.
		 *
		 */
		if (get_in_object(obj, "loop", TRUE))
			data->loop = 1;
		else
			data->loop = -1;
	}

	/* Reset thread's current sched_data and taskgroup_data after parsing. */
	data->curr_sched_data = NULL;
	data->curr_taskgroup_data = NULL;
}

static void
parse_tasks(struct json_object *tasks, rtapp_options_t *opts)
{
	/* used in the foreach macro */
	struct lh_entry *entry; char *key; struct json_object *val; int idx;

	int i = 0;
	int instance;

	log_info(PFX "Parsing tasks section");
	opts->nthreads = 0;
	opts->num_tasks = 0;
	foreach(tasks, entry, key, val, idx) {
		instance = get_int_value_from(val, "instance", TRUE, 1);
		opts->nthreads += instance;

		opts->num_tasks++;
	}

	log_info(PFX "Found %d threads of %d tasks", opts->nthreads, opts->num_tasks);

	/*
	 * Parse thread data of defined tasks so that we can use them later
	 * when creating the tasks at main() and fork event.
	 */
	opts->threads_data = malloc(sizeof(thread_data_t) * opts->num_tasks);
	foreach (tasks, entry, key, val, idx)
		parse_task_data(key, val, -1, &opts->threads_data[i++], opts);
}

static void
parse_global(struct json_object *global, rtapp_options_t *opts)
{
	char *policy, *tmp_str;
	struct json_object *tmp_obj;
	int scan_cnt;

	log_info(PFX "Parsing global section");

	if (!global) {
		log_info(PFX " No global section Found: Use default value");
		opts->duration = -1;
		opts->gnuplot = 0;
		opts->policy = other;
		opts->calib_cpu = 0;
		opts->calib_ns_per_loop = 0;
		opts->logdir = strdup("./");
		opts->logbasename = strdup("rt-app");
		opts->logsize = 0;
		opts->lock_pages = 1;
		opts->pi_enabled = 0;
		opts->io_device = strdup("/dev/null");
		opts->mem_buffer_size = DEFAULT_MEM_BUF_SIZE;
		opts->cumulative_slack = 0;
		return;
	}

	opts->duration = get_int_value_from(global, "duration", TRUE, -1);
	opts->gnuplot = get_bool_value_from(global, "gnuplot", TRUE, 0);
	policy = get_string_value_from(global, "default_policy",
				       TRUE, "SCHED_OTHER");
	if (string_to_policy(policy, &opts->policy) != 0) {
		log_critical(PFX "Invalid policy %s", policy);
		exit(EXIT_INV_CONFIG);
	}
	/*
	 * get_string_value_from allocate the string so with have to free it
	 * once useless
	 */
	free(policy);

	tmp_obj = get_in_object(global, "calibration", TRUE);
	if (tmp_obj == NULL) {
		/* no setting ? Calibrate CPU0 */
		opts->calib_cpu = 0;
		opts->calib_ns_per_loop = 0;
		log_error("missing calibration setting force CPU0");
	} else {
		if (json_object_is_type(tmp_obj, json_type_int)) {
			/* integer (no " ") detected. */
			opts->calib_ns_per_loop = json_object_get_int(tmp_obj);
			log_debug("ns_per_loop %d", opts->calib_ns_per_loop);
		} else {
			/* Get CPU number */
			tmp_str = get_string_value_from(global, "calibration",
					 TRUE, "CPU0");
			scan_cnt = sscanf(tmp_str, "CPU%d", &opts->calib_cpu);
			/*
			 * get_string_value_from allocate the string so with have to free it
			 * once useless
			 */
			free(tmp_str);
			if (!scan_cnt) {
				log_critical(PFX "Invalid calibration CPU%d", opts->calib_cpu);
				exit(EXIT_INV_CONFIG);
			}
			log_debug("calibrating CPU%d", opts->calib_cpu);
		}
	}

	tmp_obj = get_in_object(global, "log_size", TRUE);
	if (tmp_obj == NULL) {
		/* no size ? use file system */
		opts->logsize = -2;
	} else {
		if (json_object_is_type(tmp_obj, json_type_int)) {
			/* integer (no " ") detected. */
			/* buffer size is set in MB */
			opts->logsize = json_object_get_int(tmp_obj) << 20;
			log_notice("Log buffer size fixed to %dMB per threads", (opts->logsize >> 20));
		} else {
			/* Get CPU number */
			tmp_str = get_string_value_from(global, "log_size",
					 TRUE, "disable");

			if (!strcmp(tmp_str, "disable"))
				opts->logsize = 0;
			else if (!strcmp(tmp_str, "file"))
				opts->logsize = -2;
			else if (!strcmp(tmp_str, "auto"))
				opts->logsize = -2; /* Automatic buffer size computation is not supported yet so we fall back on file system mode */
			log_debug("Log buffer set to %s mode", tmp_str);

			/*
			 * get_string_value_from allocate the string so with have to free it
			 * once useless
			 */
			free(tmp_str);
		}
	}

	opts->logdir = get_string_value_from(global, "logdir", TRUE, "./");
	opts->logbasename = get_string_value_from(global, "log_basename",
						  TRUE, "rt-app");

	/*
	 * DEPRECATED: ftrace boolean values are deprecated, keep a default
	 * mapping to the new string values.
	 */
	if (check_type_is(global, "ftrace", json_type_boolean)) {
		log_notice("Usage of boolean [ftrace] attribute is DEPRECATED.");
		log_notice("Check [ftrace] attribute documentation for supported values.");
		/* Enable all categories by default */
		if (get_bool_value_from(global, "ftrace", TRUE, 0)) {
			log_notice("Enabling all [ftrace] categories by default");
			ftrace_level = ~0x0;
		}
	} else if (ftrace_setup(get_string_value_from(global, "ftrace", TRUE, ""))) {
	    log_critical(PFX "Invalid ftrace categories");
	    exit(EXIT_INV_CONFIG);
	}

	opts->lock_pages = get_bool_value_from(global, "lock_pages", TRUE, 1);
	opts->pi_enabled = get_bool_value_from(global, "pi_enabled", TRUE, 0);
	opts->io_device = get_string_value_from(global, "io_device", TRUE,
						"/dev/null");
	opts->mem_buffer_size = get_int_value_from(global, "mem_buffer_size",
							TRUE, DEFAULT_MEM_BUF_SIZE);
	opts->cumulative_slack = get_bool_value_from(global, "cumulative_slack", TRUE, 0);

}

static void
get_opts_from_json_object(struct json_object *root, rtapp_options_t *opts)
{
	struct json_object *global, *tasks, *resources;

	if (root == NULL) {
		log_error(PFX "Error while parsing input JSON");
		exit(EXIT_INV_CONFIG);
	}
	log_info(PFX "Successfully parsed input JSON");
	log_info(PFX "root     : %s", json_object_to_json_string(root));

	global = get_in_object(root, "global", TRUE);
	if (global)
		log_info(PFX "global   : %s", json_object_to_json_string(global));

	tasks = get_in_object(root, "tasks", FALSE);
	log_info(PFX "tasks    : %s", json_object_to_json_string(tasks));

	resources = get_in_object(root, "resources", TRUE);
	if (resources)
		log_info(PFX "resources: %s", json_object_to_json_string(resources));

	log_info(PFX "Parsing global");
	parse_global(global, opts);
	json_object_put(global);
	log_info(PFX "Parsing resources");
	parse_resources(resources, opts);
	json_object_put(resources);
	log_info(PFX "Parsing tasks");
	parse_tasks(tasks, opts);
	json_object_put(tasks);
	log_info(PFX "Free json objects");

}

void
parse_config_stdin(rtapp_options_t *opts)
{
	/*
	 * Read from stdin until EOF, write to temp file and parse
	 * as a "normal" config file
	 */
	size_t in_length;
	char buf[JSON_FILE_BUF_SIZE];
	struct json_object *js;
	log_info(PFX "Reading JSON config from stdin...");

	in_length = fread(buf, sizeof(char), JSON_FILE_BUF_SIZE, stdin);
	buf[in_length] = '\0';
	js = json_tokener_parse(buf);
	get_opts_from_json_object(js, opts);
	return;
}

void
parse_config(const char *filename, rtapp_options_t *opts)
{
	char *fn = strdup(filename);
	struct json_object *js;
	log_info(PFX "Reading JSON config from %s", fn);
	js = json_object_from_file(fn);
	get_opts_from_json_object(js, opts);
	return;
}
