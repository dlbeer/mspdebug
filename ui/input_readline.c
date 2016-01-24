/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2016 Daniel Beer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "input_readline.h"
#include "cmddb.h"
#include "opdb.h"
#include "stab.h"
#include "util.h"

#define HISTORY_FILENAME "~/.mspdebug_history"

struct find_context {
	const char *search_text;
	const char *skip_until;
	const char *result;
};

static int cmddb_find_command(void *user_data, const struct cmddb_record *r)
{
	struct find_context *find_data = user_data;

	if (find_data->skip_until) {
		if (r->name == find_data->skip_until)
			find_data->skip_until = NULL;
	} else if (!find_data->result) {
		size_t len = strlen(find_data->search_text);
		if (strncasecmp(r->name, find_data->search_text, len) == 0)
			find_data->result = r->name;
	}

	return 0;
}

static char *command_generator(const char *text, int state)
{
	static struct find_context find_data;
	if (state == 0) {
		// start a new search
		find_data.search_text = text;
		find_data.skip_until = NULL;
	} else {
		// continue searching after the last match
		find_data.skip_until = find_data.result;
	}
	find_data.result = NULL;

	cmddb_enum(cmddb_find_command, &find_data);
	if (find_data.result)
		return strdup(find_data.result);
	return NULL;
}

static int opdb_find_option(void *user_data, const struct opdb_key *key,
			    const union opdb_value *value)
{
	(void)value;
	struct find_context *find_data = user_data;

	if (find_data->skip_until) {
		if (key->name == find_data->skip_until)
			find_data->skip_until = NULL;
	} else if (!find_data->result) {
		size_t len = strlen(find_data->search_text);
		if (strncasecmp(key->name, find_data->search_text, len) == 0)
			find_data->result = key->name;
	}

	return 0;
}

static char *option_generator(const char *text, int state)
{
	static struct find_context find_data;
	if (state == 0) {
		// start a new search
		find_data.search_text = text;
		find_data.skip_until = NULL;
	} else {
		// continue searching after the last match
		find_data.skip_until = find_data.result;
	}
	find_data.result = NULL;

	opdb_enum(opdb_find_option, &find_data);
	if (find_data.result)
		return strdup(find_data.result);
	return NULL;
}

static int stab_find_symbol(void *user_data, const char *name, address_t value)
{
	(void)value;
	struct find_context *find_data = user_data;

	if (find_data->skip_until) {
		if (strcmp(name, find_data->skip_until) == 0)
			find_data->skip_until = NULL;
	} else if (!find_data->result) {
		size_t len = strlen(find_data->search_text);
		if (strncmp(name, find_data->search_text, len) == 0) {
			// name is stack-allocated and becomes invalid after
			// returning, so we copy it to a static buffer.
			static char buffer[MAX_SYMBOL_LENGTH];
			snprintf(buffer, sizeof(buffer), "%s", name);
			find_data->result = buffer;
		}
	}

	return 0;
}

static char *symbol_generator(const char *text, int state)
{
	static struct find_context find_data;
	if (state == 0) {
		// start a new search
		find_data.search_text = text;
		find_data.skip_until = NULL;
	} else {
		// continue searching after the last match
		find_data.skip_until = find_data.result;
	}
	find_data.result = NULL;

	stab_enum(stab_find_symbol, &find_data);
	if (find_data.result)
		return strdup(find_data.result);
	return NULL;
}

static char *array_generator(const char *text, int state, const char **array)
{
	static const char **array_ptr;
	if (state == 0) {
		array_ptr = array;
	} else {
		++array_ptr;
	}

	while (*array_ptr) {
		if (strncasecmp(*array_ptr, text, strlen(text)) == 0)
			return strdup(*array_ptr);
		array_ptr++;
	}
	return NULL;
}

static rl_compentry_func_t *complete_addrcmd(char **arg, const char *line, int start)
{
	const char *token = get_arg(arg);
	if (token == NULL || token == line + start)
		return symbol_generator;
	return NULL;
}

static char *erase_subcmd_generator(const char *text, int state)
{
	const char *subcmds[] = { "all", "segment", "segrange", NULL };
	return array_generator(text, state, subcmds);
}

static rl_compentry_func_t *complete_erase(char **arg, const char *line, int start)
{
	const char *subcmd = get_arg(arg);
	if (subcmd == NULL || subcmd == line + start)
		return erase_subcmd_generator;
	else
		return complete_addrcmd(arg, line, start);
}

static rl_compentry_func_t *complete_help(char **arg, const char *line, int start)
{
	const char *topic = get_arg(arg);
	if (topic == NULL || topic == line + start)
		return command_generator;
	return NULL;
}

static rl_compentry_func_t *complete_loadraw(char **arg, const char *line, int start)
{
	const char *filename_arg = get_arg(arg);
	if (filename_arg == NULL || filename_arg == line + start)
		return NULL; // default filename completion
	else
		return complete_addrcmd(arg, line, start);
}

static rl_compentry_func_t *complete_opt(char **arg, const char *line, int start)
{
	const char *opt_text = get_arg(arg);
	if (opt_text == NULL || opt_text == line + start)
		return option_generator;
	return NULL;
}

static char *power_subcmd_generator(const char *text, int state)
{
	const char *subcmds[] = { "info", "clear", "all", "session",
				  "export-csv", "profile", NULL };
	return array_generator(text, state, subcmds);
}

static rl_compentry_func_t *complete_power(char **arg, const char *line, int start)
{
	const char *subcmd = get_arg(arg);
	if (subcmd == NULL || subcmd == line + start)
		return power_subcmd_generator;
	return NULL;
}

static char *simio_subcmd_generator(const char *text, int state)
{
	const char *subcmds[] = { "add", "del", "devices", "classes", "help",
				  "config", "info", NULL };
	return array_generator(text, state, subcmds);
}

static rl_compentry_func_t *complete_simio(char **arg, const char *line, int start)
{
	const char *subcmd = get_arg(arg);
	if (subcmd == NULL || subcmd == line + start)
		return simio_subcmd_generator;
	return NULL;
}

static char *sym_subcmd_generator(const char *text, int state)
{
	const char *subcmds[] = { "clear", "set", "del", "import", "import+",
				  "export", "find", "rename", NULL };
	return array_generator(text, state, subcmds);
}

static rl_compentry_func_t *complete_sym(char **arg, const char *line, int start)
{
	const char *subcmd = get_arg(arg);
	if (subcmd == NULL || subcmd == line + start)
		return sym_subcmd_generator;
	else if (strcasecmp(subcmd, "set") == 0 ||
		 strcasecmp(subcmd, "del") == 0 ||
		 strcasecmp(subcmd, "find") == 0)
		return complete_addrcmd(arg, line, start);
	return NULL;
}

struct cmd_completer {
	const char *name;
	rl_compentry_func_t *(*completer)(char **arg, const char *line, int start);
};

static const struct cmd_completer cmd_completers[] = {
	{ "cgraph",     complete_addrcmd },
	{ "dis",        complete_addrcmd },
	{ "erase",	complete_erase },
	{ "fill",       complete_addrcmd },
	{ "help",       complete_help },
	{ "hexout",     complete_addrcmd },
	{ "isearch",    complete_addrcmd },
	{ "load_raw",   complete_loadraw },
	{ "md",         complete_addrcmd },
	{ "mw",         complete_addrcmd },
	{ "opt",        complete_opt },
	{ "power",      complete_power },
	{ "save_raw",   complete_addrcmd },
	{ "setbreak",   complete_addrcmd },
	{ "setwatch",   complete_addrcmd },
	{ "setwatch_r", complete_addrcmd },
	{ "setwatch_w", complete_addrcmd },
	{ "simio",      complete_simio },
	{ "sym",        complete_sym },
	{ "verify_raw", complete_loadraw },
	{ NULL,         NULL }
};

static char **mspdebug_completion(const char *text, int start, int end)
{
	rl_compentry_func_t *generator = NULL;

	// copy the current command line, terminate at cursor position
	char *line = strdup(rl_line_buffer);
	line[end] = '\0';

	char *arg = line;
	const char *cmd_text = get_arg(&arg);
	struct cmddb_record cmd;

	if (cmd_text == NULL || cmd_text == line + start)
		generator = command_generator;
	else if (!cmddb_get(cmd_text, &cmd)) {
		const struct cmd_completer *c = cmd_completers;
		while (c->name) {
			if (!strcmp(cmd.name, c->name)) {
				generator = c->completer(&arg, line, start);
				break;
			}
			c++;
		}
	}

	free(line);
	if (generator)
		return rl_completion_matches(text, generator);
	return NULL;
}

static int readline_init(void)
{
	char *path = expand_tilde(HISTORY_FILENAME);
	if (path) {
		read_history(path);
		free(path);
	}

	rl_attempted_completion_function = mspdebug_completion;

	return 0;
}

static void readline_exit(void)
{
	char *path = expand_tilde(HISTORY_FILENAME);
	if (path) {
		write_history(path);
		free(path);
	}
}

static int readline_read_command(char *out, int max_len)
{
	char *buf = readline("(mspdebug) ");

	if (!buf) {
		printf("\n");
		return 1;
	}

	if (*buf)
		add_history(buf);

	strncpy(out, buf, max_len);
	out[max_len - 1] = 0;
	free(buf);

	return 0;
}

static int readline_prompt_abort(const char *message)
{
	char buf[32];

	for (;;) {
		printf("%s ", message);
		fflush(stdout);

		if (!fgets(buf, sizeof(buf), stdin)) {
			printf("\n");
			return 1;
		}

		if (toupper(buf[0]) == 'Y')
			return 0;
		if (toupper(buf[0]) == 'N')
			return 1;

		printf("Please answer \"y\" or \"n\".\n");
	}

	return 0;
}

const struct input_interface input_readline = {
	.init		= readline_init,
	.exit		= readline_exit,
	.read_command	= readline_read_command,
	.prompt_abort	= readline_prompt_abort
};
