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

#include "rt-app_args.h"

void
usage (const char* msg, int ex_code)
{
#ifdef JSON
	printf("usage:\n"
	       "rt-app <taskset.json>\nOR\n");
#endif	       
	printf("rt-app [options] -t <period>:<exec>[:policy"
		"[:CPU affinity[:prio[:deadline]]]] -t ...\n\n");
	printf("-h, --help\t\t:\tshow this help\n");
	printf("-f, --fifo\t\t:\tset default policy for threads to SCHED_FIFO\n");
	printf("-r, --rr\t\t:\tset default policy fior threads to SCHED_RR\n");
	printf("-s, --spacing\t\t:\tmsec to wait beetween thread starts\n");
	printf("-l, --logdir\t\t:\tsave logs to different directory\n");
	printf("-b, --baselog\t\t:\tbasename for logs (implies -l . if not set)\n");
	printf("-G, --gnuplot\t\t:\tgenerate gnuplot script (needs -l)\n");
	printf("-D, --duration\t\t:\ttime (in seconds) before stopping threads\n");
	printf("-K, --no-mlock\t\t:\tDo not lock pages in memory\n");
	printf("-T, --ftrace\t\t:\tenable ftrace prints\n");
	printf("-P, --pi_enabled\t:\tenable priority inheritance on resources\n");
	printf("-M, --die_on_dmiss\t:\texit with an error if a task misses a deadline\n");
	
#ifdef AQUOSA
	printf("-q, --qos\t:\tcreate AQuoSA reservation\n");
	printf("-g, --frag\t:\tfragment for the reservation\n\n");
#else
#endif
	printf("\nPOLICY: f=SCHED_FIFO, r=SCHED_RR, o=SCHED_OTHER");
#ifdef DLSCHED
	printf(", d=SCHED_DEADLINE");
#endif
#ifdef AQUOSA
	printf(", q=AQuoSA\n");
	printf("when using AQuoSA scheduling, priority is used as"
		" percent increment \nfor budget over exec time\n");
#else
	printf("\n");
#endif
	printf("AFFINITY: comma-separated cpu index (starting from 0)\n");
	printf("\ti.e. 0,2,3 for first, third and fourth CPU\n");

	if (msg != NULL)
		printf("\n%s\n", msg);
	exit(ex_code);
}


void
parse_thread_args(char *arg, int idx, thread_data_t *tdata, policy_t def_policy)
{
	char *str = strdup(arg);
	char *token;
	long period, exec, dline;
	char tmp[256];
	int i = 0;
	int cpu;
	dline = 0;

	token = strtok(str, ":");
	tdata->name = malloc(sizeof(char) * 5);
	tdata->ind = idx;
	/* default name for command line threads */
	snprintf(tdata->name, 1, "t%d", tdata->ind); 
	tdata->sched_prio = DEFAULT_THREAD_PRIORITY;
	tdata->sched_policy = def_policy;
	tdata->cpuset = NULL;
	tdata->cpuset_str = NULL;

	while ( token != NULL)
	{
		switch(i) {
		case 0:
			period = strtol(token, NULL, 10);
			if (period <= 0 )
				usage("Cannot set negative period.", 
				      EXIT_INV_COMMANDLINE);
			tdata->period = usec_to_timespec(period);
			i++;
			break;

		case 1:
			exec = strtol(token,NULL, 10);
			//TODO: add support for max_et somehow
			if (exec > period)
				usage("Exec time cannot be greater than"
				      " period.", EXIT_INV_COMMANDLINE);
			if (exec <= 0 )
				usage("Cannot set negative exec time",
				      EXIT_INV_COMMANDLINE);
			tdata->min_et = usec_to_timespec(exec);
			tdata->max_et = usec_to_timespec(exec);
			i++;
			break;

		case 2:
#ifdef AQUOSA
			if (strcmp(token,"q") == 0)
				tdata->sched_policy = aquosa;
			else 
#endif
#ifdef DLSCHED
			if (strcmp(token,"d") == 0)
				tdata->sched_policy = deadline;
			else
#endif
			if (strcmp(token,"f") == 0)
				tdata->sched_policy = fifo;
			else if (strcmp(token,"r") == 0)
				tdata->sched_policy = rr ;
			else if (strcmp(token,"o") == 0)
				tdata->sched_policy = other;
			else {
				snprintf(tmp, 256, 
					"Invalid scheduling policy %s in %s",
					token, arg);
				usage(tmp, EXIT_INV_COMMANDLINE);
			}
			policy_to_string(tdata->sched_policy, 
					 tdata->sched_policy_descr);	

			i++;
			break;
		case 3:
			if (strcmp(token, "-") == 0)
				tdata->cpuset = NULL;
			else {
				tdata->cpuset = malloc (sizeof(cpu_set_t));
				tdata->cpuset_str = strdup(token);
			}
			i++;
			break;
		case 4:
			tdata->sched_prio = strtol(token, NULL, 10);
			// do not check, will fail in pthread_setschedparam
			i++;
			break;
		case 5:
			dline = strtol(token, NULL, 10);
			if (dline < exec)
				usage("Deadline cannot be less than "
				      "execution time", EXIT_INV_COMMANDLINE);
			if (dline > period)
				usage("Deadline cannot be greater than "
				      "period", EXIT_INV_COMMANDLINE);
			if (dline <= 0 )
				usage("Cannot set negative deadline",
				      EXIT_INV_COMMANDLINE);
			tdata->deadline = usec_to_timespec(dline);
			i++;
			break;
		}
		token = strtok(NULL, ":");
	}
	if ( i < 2 ) {
		printf("Period and exec time are mandatory\n");
		exit(EXIT_INV_COMMANDLINE);
	}

	if (dline == 0)
		tdata->deadline = tdata->period;
	
	/* set cpu affinity mask */
	if (tdata->cpuset_str)
	{
		snprintf(tmp, 256, "%s", tdata->cpuset_str);
		token = strtok(tmp, ",");
		while (token != NULL && i < 1000) {
			cpu = strtol(token, NULL, 10);
			CPU_SET(cpu, tdata->cpuset);
			strtok(NULL, ",");
			i++;
		}
	} else 
		tdata->cpuset_str = strdup("-");
	
	free(str);
}

static void
parse_command_line_options(int argc, char **argv, rtapp_options_t *opts)
{
	char tmp[PATH_LENGTH];
	signed char ch;
	int longopt_idx;
	int i;

	struct stat dirstat;

	/* set defaults */
	opts->spacing = 0;
	opts->gnuplot = 0;
	opts->lock_pages = 1;
	opts->duration = -1;
	opts->logbasename = strdup("rt-app");
	opts->logdir = NULL;
	opts->nthreads = 0;
	opts->ftrace = 0;
	opts->die_on_dmiss = 0;
	opts->pi_enabled = 0;
	opts->policy = other;
	opts->threads_data = malloc(sizeof(thread_data_t));
#ifdef AQUOSA
	opts->fragment = 1;
#endif
	static struct option long_options[] = {
	                   {"help", 0, 0, 'h'},
			   {"fifo", 0, 0, 'f'},
			   {"rr", 0, 0, 'r'},
			   {"thread", 1, 0, 't'},
			   {"spacing", 1, 0, 's'},
			   {"logdir", 1, 0, 'l'},
	                   {"baselog", 1, 0, 'b'},
			   {"gnuplot", 1, 0, 'G'},
			   {"duration", 1, 0, 'D'},
			   {"ftrace", 0, 0, 'T'},
			   {"pi_enabled", 0, 0, 'T'},
			   {"die_on_dmiss", 0, 0, 'M'},
#ifdef AQUOSA
			   {"qos", 0, 0, 'q'},
			   {"frag",1, 0, 'g'},
#endif
	                   {0, 0, 0, 0}
	               };
#ifdef AQUOSA
	while (( ch = getopt_long(argc,argv,"D:GKhfrb:s:l:qg:t:TM", 
				  long_options, &longopt_idx)) != -1)
#else
	while (( ch = getopt_long(argc,argv,"D:GKhfrb:s:l:t:TM", 
				  long_options, &longopt_idx)) != -1)
#endif
	{
		switch (ch)
		{
			case 'h':
				usage(NULL, EXIT_SUCCESS);
				break;
			case 'f':
				if (opts->policy != other)
					usage("Cannot set multiple policies",
					      EXIT_INV_COMMANDLINE);
				opts->policy = fifo;
				break;
			case 'r':
				if (opts->policy != other)
					usage("Cannot set multiple policies",
					      EXIT_INV_COMMANDLINE);
				opts->policy = rr;
				break;
			case 'b':
				if (!opts->logdir)	
					opts->logdir = strdup(".");
				opts->logbasename = strdup(optarg);
				break;
			case 's':
				opts->spacing  = strtol(optarg, NULL, 0);
				if (opts->spacing < 0)
					usage("Cannot set negative spacing",
					      EXIT_INV_COMMANDLINE);
				break;
			case 'l':
				opts->logdir = strdup(optarg);	
				lstat(opts->logdir, &dirstat);
				if (! S_ISDIR(dirstat.st_mode))
					usage("Cannot stat log directory",
					      EXIT_INV_COMMANDLINE);
				break;
			case 't':
				if (opts->nthreads > 0)
				{
					opts->threads_data = realloc(
						opts->threads_data, 
						(opts->nthreads+1) * \
							sizeof(thread_data_t));
				}
				parse_thread_args(optarg, opts->nthreads,
					&opts->threads_data[opts->nthreads],
					opts->policy);
				opts->nthreads++;
				break;
			case 'G':
				opts->gnuplot = 1;
				break;
			case 'D':
				opts->duration = strtol(optarg, NULL, 10);
				if (opts->duration < 0)
					usage("Cannot set negative duration",
					      EXIT_INV_COMMANDLINE);
				break;
			case 'K':
				opts->lock_pages = 0;
				break;
			case 'T':
				opts->ftrace = 1;
				break;
			case 'P':
				opts->pi_enabled = 1;
				break;
			case 'M':
				opts->die_on_dmiss = 1;
				break;
#ifdef AQUOSA				
			case 'q':
				if (opts->policy != other)
					usage("Cannot set multiple policies",
					      EXIT_INV_COMMANDLINE);
				opts->policy = aquosa;
				break;
			case 'g':
				opts->fragment = strtol(optarg, NULL, 10);
				if (opts->fragment < 1 || opts->fragment > 16)
					usage("Fragment divisor must be between"
					      "1 and 16", EXIT_INV_COMMANDLINE);
				break;
#endif
			default:
				log_error("Invalid option %c", ch);
				usage(NULL, EXIT_INV_COMMANDLINE);

		}

	}
	if ( opts->nthreads < 1)
		usage("You have to set parameters for at least one thread",
		      EXIT_INV_COMMANDLINE);
	
}

void
parse_command_line(int argc, char **argv, rtapp_options_t *opts)
{
#ifdef JSON
	if (argc < 2)
		usage(NULL, EXIT_SUCCESS);
	struct stat config_file_stat;
	if (stat(argv[1], &config_file_stat) == 0) {
		parse_config(argv[1], opts);
		return;
	}
	else if (strcmp(argv[1], "-") == 0) {
		parse_config_stdin(opts);
		return;
	} 
#endif
	parse_command_line_options(argc, argv, opts);
	opts->resources = NULL;
	opts->nresources = 0;
}

