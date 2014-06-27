/* 
This file is part of rt-app - https://launchpad.net/rt-app
Copyright (C) 2010  Giacomo Bagnoli <g.bagnoli@asidev.com>
Copyright (C) 2014  Juri Lelli <juri.lelli@gmail.com>

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
	to = json_object_object_get(where, what);
	if (!nullable && is_error(to)) {
		log_critical(PFX "Error while parsing config:\n" PFL
			  "%s", json_tokener_errors[-(unsigned long)to]);
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
	log_info(PIN "Init: %s mutex", data->name);

	pthread_mutexattr_init(&data->res.mtx.attr);
	if (opts->pi_enabled) {
		pthread_mutexattr_setprotocol(
				&data->res.mtx.attr,
				PTHREAD_PRIO_INHERIT);
	}
	pthread_mutex_init(&data->res.mtx.obj,
			&data->res.mtx.attr);
}

static int init_cond_resource(rtapp_resource_t *data, const rtapp_options_t *opts)
{
	log_info(PIN "Init: %s wait", data->name);

	pthread_condattr_init(&data->res.cond.attr);
	pthread_cond_init(&data->res.cond.obj,
			&data->res.cond.attr);
}

static int init_signal_resource(rtapp_resource_t *data, const rtapp_options_t *opts, char *target)
{
	log_info(PIN "Init: %s signal", data->name);

	int i = 0;
	while (strcmp(opts->resources[i].name, target) != 0) {
		if (data->index == i) {
			log_critical(PIN2 "Invalid target %s", target);
			exit(EXIT_INV_CONFIG);
		}
		i++;
	}

	data->res.signal.target = &(opts->resources[i].res.cond.obj);
}

static void
parse_resource_data(char *name, struct json_object *obj, int idx,
		  rtapp_resource_t *data, const rtapp_options_t *opts)
{
	char *type, *target;
	char def_type[RTAPP_RESOURCE_DESCR_LENGTH];
	int duration;

	log_info(PFX "Parsing resources %s [%d]", name, idx);

	/* common and defaults */
	data->index = idx;
	data->name = strdup(name);

	/* resource type */
	resource_to_string(0, def_type);
	type = get_string_value_from(obj, "type", TRUE, def_type);
	if (type) {
		if (string_to_resource(type, &data->type) != 0) {
			log_critical(PIN2 "Invalid type of resource %s", type);
			exit(EXIT_INV_CONFIG);
		}
	}

	switch (data->type) {
		case rtapp_mutex:
			init_mutex_resource(data, opts);
			break;
		case rtapp_wait:
			init_cond_resource(data, opts);
			break;
		case rtapp_signal:
		case rtapp_broadcast:
			target = get_string_value_from(obj, "target", FALSE, NULL);
			init_signal_resource(data, opts, target);
			break;
	}
}

static void parse_legacy_resources(int nresources, rtapp_options_t *opts)
{
	int i;
	char name[5];

	log_info(PIN "Creating %d mutex resources", nresources);

	opts->resources = malloc(sizeof(rtapp_resource_t) * nresources);
	for (i = 0; i < nresources; i++) {
		opts->resources[i].index = i;
		snprintf(name, 5, "%d", i);
		opts->resources[i].name = strdup(name);
		init_mutex_resource(&opts->resources[i], opts);
	}
	opts->nresources = nresources;
}

static void
parse_resources(struct json_object *resources, rtapp_options_t *opts)
{
	int i;
	struct lh_entry *entry; char *key; struct json_object *val; int idx;

	log_info(PFX "Parsing resource section");

	if (json_object_is_type(resources, json_type_int)) {
		parse_legacy_resources(json_object_get_int(resources), opts);
	}
	else {
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

static int get_resource_index(char *name, rtapp_resource_t *resources)
{
	int i=0;

	while (strcmp(resources[i].name, name) != 0)
		i++;

	return i;
}

static void
serialize_acl(rtapp_resource_access_list_t **acl,
	      char *name,
	      struct json_object *task_resources,
	      rtapp_resource_t *resources)
{
	int i, idx, found;
	struct json_object *access, *res, *next_res;
	rtapp_resource_access_list_t *tmp;
	char * next_name;

	idx = get_resource_index(name, resources);

	if (!(*acl)) {
		*acl = malloc( sizeof(rtapp_resource_access_list_t));
		(*acl)->res = &resources[idx];
//		(*acl)->index = idx;
		(*acl)->next = NULL;
		(*acl)->prev = NULL;
		tmp = *acl;
	} else {
		found = 0;
		tmp = *acl;
		while (tmp->next != NULL) {
			if (tmp->res->index == idx)
				found = 1;
			tmp = tmp->next;
		}
		if (found == 0) {
			/* add the resource to the acl only if it is not already
			 * present in the list */
			tmp->next = malloc ( sizeof (rtapp_resource_access_list_t));
			// tmp->next->index = idx;
			tmp->next->next = NULL;
			tmp->next->prev = tmp;
			tmp->next->res = &resources[idx];
		}
	}

	res = get_in_object(task_resources, name, TRUE);
	if (!res)
		return;
	assure_type_is(res, task_resources, name, json_type_object);

	access = get_in_object(res, "access", TRUE);
	if (!access)
		return;
	assure_type_is(access, res, "access", json_type_array);

	for (i=0; i<json_object_array_length(access); i++)
	{
		char res_name[5];

		next_res = json_object_array_get_idx(access, i);
		if (!json_object_is_type(next_res, json_type_string)){
			if (!json_object_is_type(next_res, json_type_int)){
				log_critical("Invalid resource index");
				exit(EXIT_INV_CONFIG);
			} else {
				snprintf(res_name, 5, "%d", json_object_get_int(next_res));
				next_name = res_name;
			}
			log_critical("Legacy resource index");
		} else
			next_name = json_object_get_string(next_res);
		/* recurse on the rest of resources */
		serialize_acl(&(*acl), next_name, task_resources, resources);
	}
}

static void
parse_thread_resources(const rtapp_options_t *opts, struct json_object *locks,
		       struct json_object *task_resources, thread_data_t *data)
{
	int i, j, usage_usec;
	struct json_object *res;
	int res_dur;
	char res_name[5];
	char *cur_res_name;

	rtapp_resource_access_list_t *tmp, *head, *last;
	char debug_msg[512], tmpmsg[512];

	data->nblockages = json_object_array_length(locks);
	data->blockages = malloc(sizeof(rtapp_tasks_resource_list_t) *
			data->nblockages);

	for (i = 0; i< data->nblockages; i++)
	{
		res = json_object_array_get_idx(locks, i);
		if (!json_object_is_type(res, json_type_string)){
			if (!json_object_is_type(res, json_type_int)){
				log_critical("Invalid resource index");
				exit(EXIT_INV_CONFIG);
			} else {
				snprintf(res_name, 5, "%d", json_object_get_int(res));
				cur_res_name = res_name;
			}
			log_critical("Legacy resource index");
		} else
			cur_res_name = json_object_get_string(res);

		data->blockages[i].usage = usec_to_timespec(0);
		data->blockages[i].acl = NULL;
		serialize_acl(&data->blockages[i].acl, cur_res_name,
				task_resources, opts->resources);

		/* since the "current" resource is returned as the first
		 * element in the list, we move it to the back  */
		tmp = data->blockages[i].acl;
		head = data->blockages[i].acl;
		do {
			last = tmp;
			tmp = tmp->next;
		} while (tmp != NULL);

		/* move first element to list end */
		if (last != head) {
			data->blockages[i].acl = head->next;
			data->blockages[i].acl->prev = NULL;
			last->next = head;
			head->next = NULL;
			head->prev = last;
		}

		tmp = data->blockages[i].acl;
		debug_msg[0] = '\0';
		do  {
			snprintf(tmpmsg, 512, "%s %d", debug_msg, tmp->res->index);
			strncpy(debug_msg, tmpmsg, 512);
			last = tmp;
			tmp = tmp->next;
		} while (tmp != NULL);

		log_info(PIN "key: acl %s", debug_msg);

		res = get_in_object(task_resources, cur_res_name, TRUE);
		if (!res) {
			usage_usec = 0;
			data->blockages[i].usage = usec_to_timespec(0);
		} else {
			assure_type_is(res, task_resources, res_name,
					json_type_object);
			usage_usec = get_int_value_from(res, "duration", TRUE, 0);
			data->blockages[i].usage = usec_to_timespec(usage_usec);
		}
		log_info(PIN "res %s, usage: %d acl: %s", cur_res_name,
				usage_usec, debug_msg);
	}
}

static void
parse_thread_data(char *name, struct json_object *obj, int idx,
		  thread_data_t *data, const rtapp_options_t *opts)
{
	long exec, period, dline;
	char *policy;
	char def_policy[RTAPP_POLICY_DESCR_LENGTH];
	struct array_list *cpuset;
	struct json_object *cpuset_obj, *cpu, *resources, *locks;
	int i, cpu_idx;

	log_info(PFX "Parsing thread %s [%d]", name, idx);

	/* common and defaults */
	data->ind = idx;
	data->name = strdup(name);
	data->lock_pages = opts->lock_pages;
	data->sched_prio = DEFAULT_THREAD_PRIORITY;
	data->cpuset = NULL;
	data->cpuset_str = NULL;

	/* loop */
	data->loop = get_int_value_from(obj, "loop", TRUE, -1);

	/* period */
	period = get_int_value_from(obj, "period", FALSE, 0);
	if (period <= 0) {
		log_critical(PIN2 "Cannot set negative period");
		exit(EXIT_INV_CONFIG);
	}
	data->period = usec_to_timespec(period);

	/* exec time */
	exec = get_int_value_from(obj, "exec", FALSE, 0);
	if (exec > period) {
		log_critical(PIN2 "Exec must be greather than period");
		exit(EXIT_INV_CONFIG);
	}
	if (exec < 0) {
		log_critical(PIN2 "Cannot set negative exec time");
		exit(EXIT_INV_CONFIG);
	}
	data->min_et = usec_to_timespec(exec);
	data->max_et = usec_to_timespec(exec);

	/* deadline */
	dline = get_int_value_from(obj, "deadline", TRUE, period);
	if (dline < exec) {
		log_critical(PIN2 "Deadline cannot be less than exec time");
		exit(EXIT_INV_CONFIG);
	}
	if (dline > period) {
		log_critical(PIN2 "Deadline cannot be greater than period");
		exit(EXIT_INV_CONFIG);
	}
	data->deadline = usec_to_timespec(dline);

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
	data->sched_prio = get_int_value_from(obj, "priority", TRUE,
			DEFAULT_THREAD_PRIORITY);

	/* delay */
	data->wait_before_start = get_int_value_from(obj, "delay", TRUE, 0);

	/* cpu set */
	cpuset_obj = get_in_object(obj, "cpus", TRUE);
	if (cpuset_obj) {
		assure_type_is(cpuset_obj, obj, "cpus", json_type_array);
		data->cpuset_str = json_object_to_json_string(cpuset_obj);
		log_info(PIN "key: cpus %s", data->cpuset_str);
		data->cpuset = malloc(sizeof(cpu_set_t));
		cpuset = json_object_get_array(cpuset_obj);
		CPU_ZERO(data->cpuset);
		for (i=0; i < json_object_array_length(cpuset_obj); i++) {
			cpu = json_object_array_get_idx(cpuset_obj, i);
			cpu_idx = json_object_get_int(cpu);
			CPU_SET(cpu_idx, data->cpuset);
		}
	} else {
		data->cpuset_str = strdup("-");
		data->cpuset = NULL;
		log_info(PIN "key: cpus %s", data->cpuset_str);
	}

	/* resources */
	resources = get_in_object(obj, "resources", TRUE);
	locks = get_in_object(obj, "lock_order", TRUE);
	if (locks) {
		assure_type_is(locks, obj, "lock_order", json_type_array);
		log_info(PIN "key: lock_order %s", json_object_to_json_string(locks));
		if (resources) {
			assure_type_is(resources, obj, "resources",
					json_type_object);
			log_info(PIN "key: resources %s",
				  json_object_to_json_string(resources));
		}
		parse_thread_resources(opts, locks, resources, data);
	}

}

static void
parse_tasks(struct json_object *tasks, rtapp_options_t *opts)
{
	/* used in the foreach macro */
	struct lh_entry *entry; char *key; struct json_object *val; int idx;

	log_info(PFX "Parsing threads section");
	opts->nthreads = 0;
	foreach(tasks, entry, key, val, idx) {
		opts->nthreads++;
	}
	log_info(PFX "Found %d threads", opts->nthreads);
	opts->threads_data = malloc(sizeof(thread_data_t) * opts->nthreads);
	foreach (tasks, entry, key, val, idx) {
		parse_thread_data(key, val, idx, &opts->threads_data[idx], opts);
	}
}

static void
parse_global(struct json_object *global, rtapp_options_t *opts)
{
	char *policy;
	log_info(PFX "Parsing global section");
	opts->spacing = get_int_value_from(global, "spacing", TRUE, 0);
	opts->duration = get_int_value_from(global, "duration", TRUE, -1);
	opts->gnuplot = get_bool_value_from(global, "gnuplot", TRUE, 0);
	policy = get_string_value_from(global, "default_policy",
				       TRUE, "SCHED_OTHER");
	if (string_to_policy(policy, &opts->policy) != 0) {
		log_critical(PFX "Invalid policy %s", policy);
		exit(EXIT_INV_CONFIG);
	}
	opts->logdir = get_string_value_from(global, "logdir", TRUE, NULL);
	opts->lock_pages = get_bool_value_from(global, "lock_pages", TRUE, 1);
	opts->logbasename = get_string_value_from(global, "log_basename",
						  TRUE, "rt-app");
	opts->ftrace = get_bool_value_from(global, "ftrace", TRUE, 0);
	opts->pi_enabled = get_bool_value_from(global, "pi_enabled", TRUE, 0);
#ifdef AQUOSA
	opts->fragment = get_int_value_from(global, "fragment", TRUE, 1);
#endif

}

static void
get_opts_from_json_object(struct json_object *root, rtapp_options_t *opts)
{
	struct json_object *global, *tasks, *resources;

	if (is_error(root)) {
		log_error(PFX "Error while parsing input JSON: %s",
			 json_tokener_errors[-(unsigned long)root]);
		exit(EXIT_INV_CONFIG);
	}
	log_info(PFX "Successfully parsed input JSON");
	log_info(PFX "root     : %s", json_object_to_json_string(root));

	global = get_in_object(root, "global", FALSE);
	log_info(PFX "global   : %s", json_object_to_json_string(global));

	tasks = get_in_object(root, "tasks", FALSE);
	log_info(PFX "tasks    : %s", json_object_to_json_string(tasks));

	resources = get_in_object(root, "resources", FALSE);
	log_info(PFX "resources: %s", json_object_to_json_string(resources));

	parse_global(global, opts);
	json_object_put(global);
	parse_resources(resources, opts);
	json_object_put(resources);
	parse_tasks(tasks, opts);
	json_object_put(tasks);

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
