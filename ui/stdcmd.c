/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009, 2010 Daniel Beer
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

#ifndef NO_SHELLCMD
#include <sys/types.h>
#include <sys/wait.h>

#include <libgen.h>
#include <unistd.h>
#include "ctrlc.h"
#endif /* !NO_SHELLCMD */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "cmddb.h"
#include "opdb.h"
#include "vector.h"
#include "stdcmd.h"
#include "output.h"
#include "output_util.h"
#include "reader.h"
#include "expr.h"

static const char *type_text(opdb_type_t type)
{
	switch (type) {
	case OPDB_TYPE_BOOLEAN:
		return "boolean";

	case OPDB_TYPE_NUMERIC:
		return "numeric";

	case OPDB_TYPE_STRING:
		return "text";
	}

	return "unknown";
}

static int push_option_name(void *user_data, const struct opdb_key *key,
			    const union opdb_value *value)
{
	(void)value;

	return vector_push((struct vector *)user_data, &key->name, 1);
}

static int push_command_name(void *user_data, const struct cmddb_record *rec)
{
	return vector_push((struct vector *)user_data, &rec->name, 1);
}

int cmd_help(char **arg)
{
	const char *topic = get_arg(arg);

	if (topic) {
		struct cmddb_record cmd;
		struct opdb_key key;

		if (!cmddb_get(topic, &cmd)) {
			printc("\x1b[1mCOMMAND: %s\x1b[0m\n\n%s\n",
			       cmd.name, cmd.help);
			return 0;
		}

		if (!opdb_get(topic, &key, NULL)) {
			printc("\x1b[1mOPTION: %s (%s)\x1b[0m\n\n%s\n",
			       key.name, type_text(key.type), key.help);
			return 0;
		}

		printc_err("help: unknown command: %s\n", topic);
		return -1;
	} else {
		struct vector v;

		vector_init(&v, sizeof(const char *));

		if (!cmddb_enum(push_command_name, &v)) {
			printc("Available commands:\n");
			namelist_print(&v);
			printc("\n");
		} else {
			pr_error("help: can't allocate memory for command list");
		}

		vector_realloc(&v, 0);

		if (!opdb_enum(push_option_name, &v)) {
			printc("Available options:\n");
			namelist_print(&v);
			printc("\n");
		} else {
			pr_error("help: can't allocate memory for option list");
		}

		vector_destroy(&v);

		printc("Type \"help <topic>\" for more information.\n");
		printc("Use the \"opt\" command (\"help opt\") to set "
			"options.\n");
#if defined(__Windows__) && !defined(USE_READLINE)
		printc("Press Ctrl+Z, Enter to quit.\n");
#else
		printc("Press Ctrl+D to quit.\n");
#endif
	}

	return 0;
}

static int parse_option(opdb_type_t type, union opdb_value *value,
			const char *word)
{
	switch (type) {
	case OPDB_TYPE_BOOLEAN:
		value->numeric = (isdigit(word[0]) && word[0] > '0') ||
			word[0] == 't' || word[0] == 'y' ||
			(word[0] == 'o' && word[1] == 'n');
		break;

	case OPDB_TYPE_NUMERIC:
		return expr_eval(word, &value->numeric);

	case OPDB_TYPE_STRING:
		strncpy(value->string, word, sizeof(value->string));
		value->string[sizeof(value->string) - 1] = 0;
		break;
	}

	return 0;
}

static int display_option(void *user_data, const struct opdb_key *key,
			  const union opdb_value *value)
{
	(void)user_data;

	printc("%32s = ", key->name);

	switch (key->type) {
	case OPDB_TYPE_BOOLEAN:
		printc("%s", value->boolean ? "true" : "false");
		break;

	case OPDB_TYPE_NUMERIC:
		printc("0x%x (%u)", value->numeric, value->numeric);
		break;

	case OPDB_TYPE_STRING:
		printc("%s", value->string);
		break;
	}

	printc("\n");
	return 0;
}

int cmd_opt(char **arg)
{
	const char *opt_text = get_arg(arg);
	struct opdb_key key;
	union opdb_value value;

	if (opt_text) {
		if (opdb_get(opt_text, &key, &value) < 0) {
			printc_err("opt: no such option: %s\n",
				opt_text);
			return -1;
		}
	}

	if (**arg) {
		if (parse_option(key.type, &value, *arg) < 0) {
			printc_err("opt: can't parse option: %s\n",
				*arg);
			return -1;
		}

		opdb_set(key.name, &value);
	} else if (opt_text) {
		display_option(NULL, &key, &value);
	} else {
		opdb_enum(display_option, NULL);
	}

	return 0;
}

int cmd_read(char **arg)
{
	char *filename = get_arg(arg);

	if (!filename) {
		printc_err("read: filename must be specified\n");
		return -1;
	}

	return process_file(filename, 1);
}

int cmd_exit(char **arg)
{
	(void)arg;

	reader_exit();
	return 0;
}

#ifndef NO_SHELLCMD
int cmd_shellcmd(char **arg)
{
	int fd;
	pid_t pid;
	char *sh, *p;
	static char *shell = NULL;
	static char shell_name[16];

	if (shell == NULL) {
		sh = getenv("SHELL");
		if (sh == NULL) {
			sh = "/bin/sh";
		}

		shell = strdup(sh);
		p = strdup(sh);
		if (shell == NULL || p == NULL) {
			pr_error("!: error: strdup");
			return -1;
		}

		memset(shell_name, 0, sizeof(shell_name));
		*shell_name = '-';
		strncpy(shell_name + 1, basename(p), sizeof(shell_name) - 2);
		shell_name[sizeof(shell_name) - 1] = '\0';
		free(p);
	}

	pid = fork();

	if (pid == -1) {
		pr_error("!: error: fork");
		return -1;
	}

	if (pid == 0) {
		ctrlc_exit();

		for (fd = 3; fd < 1024; fd++) {
			(void)close(fd);
		}

		if (arg && *arg[0]) {
			execl(shell, shell_name, "-c", *arg, NULL);
		} else {
			execl(shell, shell_name, NULL);
		}

		printc_err("!: error: execl(\"%s\")", shell);
		pr_error("");
		_exit(1);
	}

	while (waitpid(-1, NULL, 0) != pid) {
		;
	}

	return 0;
}
#endif /* !NO_SHELLCMD */
