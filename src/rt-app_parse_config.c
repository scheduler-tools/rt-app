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

#include "rt-app_parse_config.h"

#define PFX "[json] "
#define PFL "         "PFX
#define PIN PFX"    "
#define PIN2 PIN"    "
#define PIN3 PIN2"    "
#define JSON_FILE_BUF_SIZE 4096

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

static int init_mutex_resource(rtapp_resource_t *data, const rtapp_options_t *opts)
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

static int init_timer_resource(rtapp_resource_t *data, const rtapp_options_t *opts)
{
	log_info(PIN3 "Init: %s timer", data->name);
	data->res.timer.init = 0;
}

static int init_cond_resource(rtapp_resource_t *data, const rtapp_options_t *opts)
{
	log_info(PIN3 "Init: %s wait", data->name);

	pthread_condattr_init(&data->res.cond.attr);
	pthread_cond_init(&data->res.cond.obj,
			&data->res.cond.attr);
}

static void
init_resource_data(const char *name, int type, int idx, const rtapp_options_t *opts)
{
	rtapp_resource_t *data = &(opts->resources[idx]);

	/* common and defaults */
	data->index = idx;
	data->name = strdup(name);
	data->type = type;

	switch (data->type) {
		case rtapp_mutex:
			init_mutex_resource(data, opts);
			break;
		case rtapp_timer:
			init_timer_resource(data, opts);
			break;
		case rtapp_wait:
			init_cond_resource(data, opts);
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

	init_resource_data(name, data->type, idx, opts);
}

static int
add_resource_data(const char *name, int type, rtapp_options_t *opts)
{
	int idx;

	idx = opts->nresources;

	log_info(PIN2 "Add new resource %s [%d] type %d", name, idx, type);

	opts->nresources++;
	opts->resources = realloc(opts->resources, sizeof(rtapp_resource_t) * opts->nresources);

	init_resource_data(name, type, idx, opts);

	return idx;
}

static void
parse_resources(struct json_object *resources, rtapp_options_t *opts)
{
	int i;
	struct lh_entry *entry; char *key; struct json_object *val; int idx;

	log_info(PFX "Parsing resource section");

	if (!resources) {
		log_info(PFX "No resource section Found");
		return;
	}

	if (json_object_is_type(resources, json_type_object)) {
		opts->nresources = 0;
		foreach(resources, entry, key, val, idx) {
			opts->nresources++;
		}

		log_info(PFX "Found %d Resources", opts->nresources);
		opts->resources = malloc(sizeof(rtapp_resource_t) * opts->nresources);

		foreach (resources, entry, key, val, idx) {
			parse_resource_data(key, val, idx, &opts->resources[idx], opts);
		}
	}
}

static int get_resource_index(const char *name, int type, rtapp_options_t *opts)
{
	rtapp_resource_t *resources = opts->resources;
	int nresources = opts->nresources;
	int i = 0;

	while ((i < nresources) && ((strcmp(resources[i].name, name) != 0) || (resources[i].type != type)))
		i++;

	if (i >= nresources)
		i = add_resource_data(name, type, opts);

	return i;
}

static char* create_unique_name(char *tmp, int size, const char* ref, long tag)
{
	snprintf(tmp, size, "%s%lx", ref, (long)(tag));
	return tmp;
}

static void
parse_thread_event_data(char *name, struct json_object *obj,
		  event_data_t *data, rtapp_options_t *opts, long tag)
{
	rtapp_resource_t *rdata, *ddata;
	char unique_name[22];
	const char *ref, *tmp;
	int i;

	if (!strncmp(name, "run", strlen("run")) ||
			!strncmp(name, "sleep", strlen("sleep"))) {

		if (!json_object_is_type(obj, json_type_int))
			goto unknown_event;

		data->duration = json_object_get_int(obj);

		if (!strncmp(name, "sleep", strlen("sleep")))
			data->type = rtapp_sleep;
		else
			data->type = rtapp_run;

		log_info(PIN2 "type %d duration %d", data->type, data->duration);
		return;
	}

	if (!strncmp(name, "lock", strlen("lock")) ||
			!strncmp(name, "unlock", strlen("unlock"))) {

		if (!json_object_is_type(obj, json_type_string))
			goto unknown_event;

		ref = json_object_get_string(obj);
		i = get_resource_index(ref, rtapp_mutex, opts);

		data->res = i;

		if (!strncmp(name, "lock", strlen("lock")))
			data->type = rtapp_lock;
		else
			data->type = rtapp_unlock;

		rdata = &(opts->resources[data->res]);
		ddata = &(opts->resources[data->dep]);

		log_info(PIN2 "type %d target %s [%d]", data->type, rdata->name, rdata->index);
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
		i = get_resource_index(ref, rtapp_wait, opts);

		data->res = i;

		rdata = &(opts->resources[data->res]);
		ddata = &(opts->resources[data->dep]);

		log_info(PIN2 "type %d target %s [%d]", data->type, rdata->name, rdata->index);
		return;
	}

	if (!strncmp(name, "wait", strlen("wait")) || 
			!strncmp(name, "sync", strlen("sync"))) {

		if (!strncmp(name, "wait", strlen("wait")))
			data->type = rtapp_wait;
		else
			data->type = rtapp_sig_and_wait;

		ref = get_string_value_from(obj, "ref", TRUE, "unknown");
		i = get_resource_index(ref, rtapp_wait, opts);
		/*
		 * get_string_value_from allocate the string so with have to free it
		 * once useless
		 */
		free(ref);

		data->res = i;

		ref = get_string_value_from(obj, "mutex", TRUE, "unknown");
		i = get_resource_index(ref, rtapp_mutex, opts);
		/*
		 * get_string_value_from allocate the string so with have to free it
		 * once useless
		 */
		free(ref);

		data->dep = i;

		rdata = &(opts->resources[data->res]);
		ddata = &(opts->resources[data->dep]);

		log_info(PIN2 "type %d target %s [%d] mutex %s [%d]", data->type, rdata->name, rdata->index, ddata->name, ddata->index);
		return;
	}

	if (!strncmp(name, "timer", strlen("timer"))) {

		tmp = get_string_value_from(obj, "ref", TRUE, "unknown");
		if (!strncmp(tmp, "unique", strlen("unique")))
				ref = create_unique_name(unique_name, sizeof(unique_name), tmp, tag);
		else
				ref = tmp;

		i = get_resource_index(ref, rtapp_timer, opts);

		/*
		 * get_string_value_from allocate the string so with have to free it
		 * once useless
		 */
		free(tmp);

		data->res = i;

		data->duration = get_int_value_from(obj, "period", TRUE, 0);

		data->type = rtapp_timer;

		rdata = &(opts->resources[data->res]);
		ddata = &(opts->resources[data->dep]);

		log_info(PIN2 "type %d target %s [%d] period %d", data->type, rdata->name, rdata->index, data->duration);
		return;
	}

	if (!strncmp(name, "resume", strlen("resume"))) {

		data->type = rtapp_resume;

		if (!json_object_is_type(obj, json_type_string))
			goto unknown_event;

		ref = json_object_get_string(obj);

		i = get_resource_index(ref, rtapp_wait, opts);

		data->res = i;

		i = get_resource_index(ref, rtapp_mutex, opts);

		data->dep = i;

		rdata = &(opts->resources[data->res]);
		ddata = &(opts->resources[data->dep]);

		log_info(PIN2 "type %d target %s [%d] mutex %s [%d]", data->type, rdata->name, rdata->index, ddata->name, ddata->index);
		return;
	}

	if (!strncmp(name, "suspend", strlen("suspend"))) {

		data->type = rtapp_suspend;

		if (!json_object_is_type(obj, json_type_string))
			goto unknown_event;

		ref = json_object_get_string(obj);

		i = get_resource_index(ref, rtapp_wait, opts);

		data->res = i;

		i = get_resource_index(ref, rtapp_mutex, opts);

		data->dep = i;

		rdata = &(opts->resources[data->res]);
		ddata = &(opts->resources[data->dep]);

		log_info(PIN2 "type %d target %s [%d] mutex %s [%d]", data->type, rdata->name, rdata->index, ddata->name, ddata->index);
		return;
	}

unknown_resource:
	log_error(PIN2 "Resource %s not found in the resource section !!!", ref);
	log_error(PIN2 "Please check the resource name or the resource section");

unknown_event:
	data->duration = 0;
	data->type = rtapp_run;
	log_error(PIN2 "Unknown or mismatch %s event type !!!", name);

}

static int
obj_is_event(char *name)
{
	if (!strncmp(name, "lock", strlen("lock")))
			return rtapp_mutex;
	if (!strncmp(name, "unlock", strlen("unlock")))
			return rtapp_lock;
	if (!strncmp(name, "wait", strlen("wait")))
			return rtapp_unlock;
	if (!strncmp(name, "signal", strlen("signal")))
			return rtapp_signal;
	if (!strncmp(name, "broad", strlen("broad")))
			return rtapp_broadcast;
	if (!strncmp(name, "sync", strlen("sync")))
			return rtapp_sig_and_wait;
	if (!strncmp(name, "sleep", strlen("sleep")))
			return rtapp_sleep;
	if (!strncmp(name, "run", strlen("run")))
			return rtapp_run;
	if (!strncmp(name, "timer", strlen("timer")))
			return rtapp_timer;
	if (!strncmp(name, "suspend", strlen("suspend")))
			return rtapp_suspend;
	if (!strncmp(name, "resume", strlen("resume")))
			return rtapp_resume;

	return 0;
}

static void
parse_thread_phase_data(struct json_object *obj,
		  phase_data_t *data, rtapp_options_t *opts, long tag)
{
	/* used in the foreach macro */
	struct lh_entry *entry; char *key; struct json_object *val; int idx;
	int i;

	/* loop */
	data->loop = get_int_value_from(obj, "loop", TRUE, 1);

	/* Count number of events */
	data->nbevents = 0;
	foreach(obj, entry, key, val, idx) {
		if (obj_is_event(key))
				data->nbevents++;
	}
	log_info(PIN "Found %d events", data->nbevents);

	if (data->nbevents == 0)
		return;

	data->events = malloc(data->nbevents * sizeof(event_data_t));

	/* Parse events */
	i = 0;
	foreach(obj, entry, key, val, idx) {
		if (obj_is_event(key)) {
			log_info(PIN "Parsing event %s", key);
			parse_thread_event_data(key, val, &data->events[i], opts, tag);
			i++;
		}
	}
}

static void
parse_thread_data(char *name, struct json_object *obj, int index,
		  thread_data_t *data, rtapp_options_t *opts)
{
	char *policy;
	char def_policy[RTAPP_POLICY_DESCR_LENGTH];
	struct array_list *cpuset;
	struct json_object *cpuset_obj, *phases_obj, *cpu, *resources, *locks;
	int i, cpu_idx, prior_def;

	log_info(PFX "Parsing thread %s [%d]", name, index);

	/* common and defaults */
	data->resources = &opts->resources;
	data->ind = index;
	data->name = strdup(name);
	data->lock_pages = opts->lock_pages;
	data->cpuset = NULL;
	data->cpuset_str = NULL;

	/* policy */
	policy_to_string(opts->policy, def_policy);
	policy = get_string_value_from(obj, "policy", TRUE, def_policy);
	if (policy) {
		if (string_to_policy(policy, &data->sched_policy) != 0) {
			log_critical(PIN2 "Invalid policy %s", policy);
			exit(EXIT_INV_CONFIG);
		}
	}
	policy_to_string(data->sched_policy, data->sched_policy_descr);

	/* priority */
	if (data->sched_policy == other)
		prior_def = DEFAULT_THREAD_NICE;
	else
		prior_def = DEFAULT_THREAD_PRIORITY;

	data->sched_prio = get_int_value_from(obj, "priority", TRUE,
				 prior_def);

	/* deadline params */
	data->runtime = get_int_value_from(obj, "runtime", TRUE, 0);
	data->period = get_int_value_from(obj, "period", TRUE, data->runtime);
	data->deadline = get_int_value_from(obj, "period", TRUE, data->period);

	/* cpuset */
	cpuset_obj = get_in_object(obj, "cpus", TRUE);
	if (cpuset_obj) {
		assure_type_is(cpuset_obj, obj, "cpus", json_type_array);
		data->cpuset_str = strdup(json_object_to_json_string(cpuset_obj));
		data->cpuset = malloc(sizeof(cpu_set_t));
		cpuset = json_object_get_array(cpuset_obj);
		CPU_ZERO(data->cpuset);
		for (i = 0; i < json_object_array_length(cpuset_obj); i++) {
			cpu = json_object_array_get_idx(cpuset_obj, i);
			cpu_idx = json_object_get_int(cpu);
			CPU_SET(cpu_idx, data->cpuset);
		}
	} else {
		data->cpuset_str = strdup("-");
		data->cpuset = NULL;
	}
	log_info(PIN "key: cpus %s", data->cpuset_str);

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
			parse_thread_phase_data(val, &data->phases[idx], opts, (long)data);
		}

		/* Get loop number */
		data->loop = get_int_value_from(obj, "loop", TRUE, -1);

	} else {
		data->nphases = 1;
		data->phases = malloc(sizeof(phase_data_t) * data->nphases);
		parse_thread_phase_data(obj,  &data->phases[0], opts, (long)data);
		/* Get loop number */
		data->loop = 1;
	}

}

static void
parse_tasks(struct json_object *tasks, rtapp_options_t *opts)
{
	/* used in the foreach macro */
	struct lh_entry *entry; char *key; struct json_object *val; int idx;

	int i, instance;

	log_info(PFX "Parsing tasks section");
	opts->nthreads = 0;
	foreach(tasks, entry, key, val, idx) {
		instance = get_int_value_from(val, "instance", TRUE, 1);
		opts->nthreads += instance;
	}

	log_info(PFX "Found %d tasks", opts->nthreads);
	opts->threads_data = malloc(sizeof(thread_data_t) * opts->nthreads);
	i = instance = 0;
	foreach (tasks, entry, key, val, idx) {
		instance += get_int_value_from(val, "instance", TRUE, 1);
		for (; i < instance; i++)
			parse_thread_data(key, val, i, &opts->threads_data[i], opts);
	}
}

static void
parse_global(struct json_object *global, rtapp_options_t *opts)
{
	char *policy, *cal_str;
	struct json_object *cal_obj;
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
		opts->lock_pages = 1;
		opts->logbasename = strdup("rt-app");
		opts->ftrace = 0;
		opts->pi_enabled = 0;
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

	cal_obj = get_in_object(global, "calibration", TRUE);
	if (cal_obj == NULL) {
		/* no setting ? Calibrate CPU0 */
		opts->calib_cpu = 0;
		opts->calib_ns_per_loop = 0;
		log_error("missing calibration setting force CPU0");
	} else {
		if (json_object_is_type(cal_obj, json_type_int)) {
			/* integer (no " ") detected. */
			opts->calib_ns_per_loop = json_object_get_int(cal_obj);
			log_debug("ns_per_loop %d", opts->calib_ns_per_loop);
		} else {
			/* Get CPU number */
			cal_str = get_string_value_from(global, "calibration",
					 TRUE, "CPU0");
			scan_cnt = sscanf(cal_str, "CPU%d", &opts->calib_cpu);
			/*
			 * get_string_value_from allocate the string so with have to free it
			 * once useless
			 */
			free(cal_str);
			if (!scan_cnt) {
				log_critical(PFX "Invalid calibration CPU%d", opts->calib_cpu);
				exit(EXIT_INV_CONFIG);
			}
			log_debug("calibrating CPU%d", opts->calib_cpu);
		}
	}

	opts->logdir = get_string_value_from(global, "logdir", TRUE, "./");
	opts->lock_pages = get_bool_value_from(global, "lock_pages", TRUE, 1);
	opts->logbasename = get_string_value_from(global, "log_basename",
						  TRUE, "rt-app");
	opts->ftrace = get_bool_value_from(global, "ftrace", TRUE, 0);
	opts->pi_enabled = get_bool_value_from(global, "pi_enabled", TRUE, 0);

}

static void
get_opts_from_json_object(struct json_object *root, rtapp_options_t *opts)
{
	struct json_object *global, *tasks, *resources;

	if (is_error(root)) {
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
	int done;
	char *fn = strdup(filename);
	struct json_object *js;
	log_info(PFX "Reading JSON config from %s", fn);
	js = json_object_from_file(fn);
	get_opts_from_json_object(js, opts);
	return;
}
