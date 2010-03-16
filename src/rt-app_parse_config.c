/* 
This file is part of rt-app - https://launchpad.net/rt-app
Copyright (C) 2010  Giacomo Bagnoli <g.bagnoli@asidev.com>

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

#define JSON_FILE_BUF_SIZE 4096

static inline struct json_object* 
get_in_object(struct json_object *where, 
	      const char *what)
{
	struct json_object *to;	
	to = json_object_object_get(where, what);
	if (is_error(to)) {
		log_critical(PFX "Error while parsing config:\n" PFL 
			  "%s", json_tokener_errors[-(unsigned long)to]);
		exit(EXIT_FAILURE);
	}
	if (strcmp(json_object_to_json_string(to), "null") == 0) {
		log_critical(PFX "Cannot find key %s", what);
		exit(EXIT_FAILURE);
	}
	return to;
}

static void
parse_resources(struct json_object *resources, rtapp_options_t *opts)
{
	// TODO
}

static void
parse_tasks(struct json_object *tasks, rtapp_options_t *opts)
{
	// TODO
}

static void
parse_global(struct json_object *global, rtapp_options_t *opts)
{
	// TODO
}

static void
get_opts_from_json_object(struct json_object *root, rtapp_options_t *opts)
{
	struct json_object *global, *tasks, *resources;

	if (is_error(root)) {
		log_error(PFX "Error while parsing input JSON: %s",
			 json_tokener_errors[-(unsigned long)root]);
		exit(EXIT_FAILURE);
	}
	log_info(PFX "Successfully parsed input JSON");
	log_debug(PFX "root     : %s", json_object_to_json_string(root));
	
	global = get_in_object(root, "global");
	log_debug(PFX "global   : %s", json_object_to_json_string(global));
	
	tasks = get_in_object(root, "tasks");
	log_debug(PFX "tasks    : %s", json_object_to_json_string(tasks));
	
	resources = get_in_object(root, "resources");
	log_debug(PFX "resources: %s", json_object_to_json_string(resources));

	parse_resources(resources, opts);
	parse_global(global, opts);
	parse_tasks(global, opts);
	
}

void
parse_config_stdin(rtapp_options_t *opts)
{
	/* read from stdin until EOF, write to temp file and parse
	 * as a "normal" config file */
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
