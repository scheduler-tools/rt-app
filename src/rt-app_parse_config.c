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

#define PFX "[yaml] "
#define PFL "         "PFX

static void
log_parser_error(yaml_parser_t *parser)
{
	switch (parser->error)
	{
		case YAML_MEMORY_ERROR:
			log_critical(PFX "Memory error: Not enough memory for "
				     "parsing");
			break;

		case YAML_READER_ERROR:
			if (parser->problem_value != -1) {
				log_critical(PFX "Reader error: %s: #%X at %d",
					     parser->problem,
					     parser->problem_value, 
					     (int) parser->problem_offset);
			}
			else {
				log_critical(PFX "Reader error: %s at %d", 
					     parser->problem,
					     (int) parser->problem_offset);
			}
			break;

		case YAML_SCANNER_ERROR:
			if (parser->context) {
				log_critical(PFX 
					     "Scanner error: %s at line %d,"
					     "column %d\n"PFL "%s at line %d,"
					     "column %d", parser->context,
					     (int) parser->context_mark.line+1,
					     (int) parser->context_mark.column+1,
					     parser->problem,
					     (int) parser->problem_mark.line+1,
					     (int) parser->problem_mark.column+1);
			}
			else {
				log_critical(PFX "Scanner error:"
					     " %s at line %d, column %d",
					     parser->problem, 
					     (int) parser->problem_mark.line+1,
					     (int) parser->problem_mark.column+1);
			}
			break;

		case YAML_PARSER_ERROR:
			if (parser->context) {
				log_critical(PFX "Parser error: %s at line %d,"
					     " column %d\n"PFL "%s at line %d,"
					     " column %d", parser->context,
					     (int) parser->context_mark.line+1,
					     (int) parser->context_mark.column+1,
					     parser->problem, 
					     (int) parser->problem_mark.line+1,
					     (int) parser->problem_mark.column+1);
			}
			else {
				log_critical(PFX "Parser error: %s at line %d,"
					     " column %d",
					     parser->problem, 
					     (int) parser->problem_mark.line+1,
					     (int) parser->problem_mark.column+1);
			}
			break;

		default:
			/* Couldn't happen. */
			log_critical(PFX "Internal error");
			break;
	}
	yaml_parser_delete(parser);
	exit(EXIT_FAILURE);
}

int
parse_token(yaml_token_t *token, int *done)
{
	yaml_token_type_t t = token->type;
	switch (token->type) {
		case YAML_STREAM_START_TOKEN:
			log_info(PFX "Start stream");
			break;
		case YAML_STREAM_END_TOKEN:
			log_info(PFX "End stream");
			*done = 1;
			break;
		case YAML_DOCUMENT_START_TOKEN:
			log_info(PFX "Start document");
			break;
		case YAML_DOCUMENT_END_TOKEN:
			log_info(PFX "End document");
			break;
		case YAML_KEY_TOKEN:
			log_info(PFX "Key token %s", token->data.scalar.value);
		default:
			/* ignore */
			log_info(PFX "Ingnoring token %d", token->type);
			break;
	}
	return 0;
}

void
parse_config(const char *filename, rtapp_options_t *opts)
{
	yaml_parser_t parser;
	yaml_token_t token;
	FILE *config;
	int done;

	log_info(PFX "Reading %s filename", filename);
	config = fopen(filename, "r");
	if (!config)
		log_error(PFX "Cannot open %s, aborting", filename);

	if (!yaml_parser_initialize(&parser))
		log_parser_error(&parser);

	yaml_parser_set_input_file(&parser, config);

	while (!done)
	{
		/* Get the next event. */
		if (!yaml_parser_scan(&parser, &token))
			log_parser_error(&parser);	
		
		/* TODO maintain status */
		parse_token(&token, &done);
		
		/* Are we finished? */
		done = (token.type == YAML_STREAM_END_TOKEN);
		yaml_token_delete(&token);
	}

	log_info(PFX "Successfully read config file %s", filename);
	yaml_parser_delete(&parser);
	fclose(config);
	exit(EXIT_SUCCESS);
	return;


}
