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

#include "rt-app_args.h"

void
usage (const char* msg, int ex_code)
{
	printf("usage:\n"
	       "rt-app <taskset.json>\n");

	if (msg != NULL)
		printf("\n%s\n", msg);
	exit(ex_code);
}

void
parse_command_line(int argc, char **argv, rtapp_options_t *opts)
{
	struct stat config_file_stat;

	if (argc < 2)
		usage(NULL, EXIT_SUCCESS);

	if (stat(argv[1], &config_file_stat) == 0) {
		parse_config(argv[1], opts);
		return;
	} else if (strcmp(argv[1], "-") == 0) {
		parse_config_stdin(opts);
		return;
	}

	usage(NULL, EXIT_SUCCESS);
}

