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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <getopt.h>
#include <limits.h>
#include <errno.h>

#include "rt-app_utils.h"
#include "rt-app_parse_config.h"
#include "rt-app_utils.h"

char help_usage[] = \
"Usage: rt-app [-l <debug_level>] <taskset.json>\n"
"Try 'rt-app --help' for more information.\n";

char help_full[] = \
"Usage:\n"
"      rt-app [-l <debug_level>] <taskset.json>\n"
"      cat taskset.json | rt-app -\n\n"
"taskset.json is a json file describing the workload that will be generated"
"by rt-app.\n\n"
"In the first example, the json file is opened and parsed by rt-app.\n"
"In the second example, rt-app reads the workload description in json format\n"
"through the standard input.\n\n"
"Miscellaneous:\n"
"  -v, --version      display version information and exit\n"
"  -l, --log          set verbosity level (10: ERROR/CRITICAL, 50: NOTICE (default)\n"
"                                    75: INFO, 100: DEBUG)\n"
"  -h, --help         display this help text and exit\n";

void
usage(const char* msg, int ex_code)
{
	printf("%s", help_usage);

	if (msg != NULL)
		printf("\n%s\n", msg);
	exit(ex_code);
}

struct option long_args[] = {
	{"help",	no_argument,		0,	'h'},
	{"version",	no_argument,		0,	'v'},
	{"log",		required_argument,	0,	'l'},
	{0,		0,			0,	0}
};

static char* main_install_path = NULL;

char const* get_shared_library_base_path()
{
	return main_install_path;
}

void
parse_command_line(int argc, char **argv, rtapp_options_t *opts)
{
	struct stat config_file_stat;
	int c;
	int full_path_len;
	int main_install_path_len;

	const char shared_lib_dir_name[] = "/../rtapp_function/";

	/* get the rt-app base directory full name */
	char* full_path = malloc(PATH_MAX);
	int index;
	int return_snprintf = 0;

	/* get the program invocation string and extract the base directory content */
	if (!realpath(argv[0], full_path)) {
		log_error("failed to get rt-app base path: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	for(index=strlen(full_path)-1; index>0; index--) {
		if(full_path[index]=='/') {
			full_path[index]='\0';
			break;
		}
	}

	full_path_len = strlen(full_path);

	main_install_path_len = full_path_len + sizeof(shared_lib_dir_name) + 1;
	main_install_path = malloc(main_install_path_len);
	if (!main_install_path) {
		log_error("failed to allocate %d bytes: %s", main_install_path_len, strerror(errno));
		exit(EXIT_FAILURE);
	}

	return_snprintf = snprintf(main_install_path, main_install_path_len,"%s%s", full_path, shared_lib_dir_name);
	if(return_snprintf >= main_install_path_len || return_snprintf < 0) {
		log_error("failed to compose install path\n");
		exit(EXIT_FAILURE);
	}

	while (1) {
		c = getopt_long(argc, argv, "hvl:", long_args, 0);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			printf("%s", help_full);
			exit(0);
			break;
		case 'v':
			printf("%s %s (%s)\n",
			       PACKAGE,
			       VERSION,
			       BUILD_DATE);
			exit(0);
			break;
		case 'l':
			if (!optarg) {
				usage(NULL, EXIT_INV_COMMANDLINE);
			} else {
				char *endptr;
				long int ll = strtol(optarg, &endptr, 10);
				if (*endptr) {
					usage(NULL, EXIT_INV_COMMANDLINE);
					break;
				}
				log_level = ll;
			}
			break;
		default:
			usage(NULL, EXIT_INV_COMMANDLINE);
			break;
		}
	}

	if (optind >= argc)
		usage(NULL, EXIT_INV_COMMANDLINE);

	if (stat(argv[optind], &config_file_stat) == 0)
		parse_config(argv[optind], opts);
	else if (strcmp(argv[optind], "-") == 0)
		parse_config_stdin(opts);
	else
		usage(NULL, EXIT_FAILURE);
}
