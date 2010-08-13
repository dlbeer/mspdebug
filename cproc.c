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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include "opdb.h"
#include "expr.h"
#include "cproc.h"
#include "vector.h"
#include "stab.h"
#include "util.h"
#include "output.h"

struct cproc {
	struct vector           command_list;
	int                     lists_modified;

	int                     modify_flags;
	int                     in_reader_loop;
};

static struct cproc_command *find_command(cproc_t cp, const char *name)
{
	int i;

	for (i = 0; i < cp->command_list.size; i++) {
		struct cproc_command *cmd =
			VECTOR_PTR(cp->command_list, i, struct cproc_command);

		if (!strcasecmp(cmd->name, name))
			return cmd;
	}

	return NULL;
}

static int namelist_cmp(const void *a, const void *b)
{
	return strcasecmp(*(const char **)a, *(const char **)b);
}

/* NOTE: Both sort_lists and namelist_print assume that the first item in each
 *       vector element is a const char *
 */
static void sort_lists(cproc_t cp)
{
	if (!cp->lists_modified)
		return;

	if (cp->command_list.ptr)
		qsort(cp->command_list.ptr, cp->command_list.size,
		      cp->command_list.elemsize, namelist_cmp);

	cp->lists_modified = 0;
}

static void namelist_print(struct vector *v)
{
	int i;
	int max_len = 0;
	int rows, cols;

	for (i = 0; i < v->size; i++) {
		const char *text = VECTOR_AT(*v, i, const char *);
		int len = strlen(text);

		if (len > max_len)
			max_len = len;
	}

	max_len += 2;
	cols = 72 / max_len;
	rows = (v->size + cols - 1) / cols;

	for (i = 0; i < rows; i++) {
		int j;

		printf("    ");
		for (j = 0; j < cols; j++) {
			int k = j * rows + i;
			const char *text;

			if (k >= v->size)
				break;

			text = VECTOR_AT(*v, k, const char *);
			printf("%s", text);
			for (k = strlen(text); k < max_len; k++)
				printf(" ");
		}

		printf("\n");
	}
}

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
	struct vector *v = (struct vector *)user_data;

	return vector_push(v, &key->name, 1);
}

static int cmd_help(cproc_t cp, char **arg)
{
	const char *topic = get_arg(arg);

	if (topic) {
		const struct cproc_command *cmd = find_command(cp, topic);
		struct opdb_key key;

		if (cmd) {
			printc("\x1b[1mCOMMAND: %s\x1b[0m\n\n%s\n",
			       cmd->name, cmd->help);
			return 0;
		}

		if (!opdb_get(topic, &key, NULL)) {
			printc("\x1b[1mOPTION: %s (%s)\x1b[0m\n\n%s\n",
			       key.name, type_text(key.type), key.help);
			return 0;
		}

		fprintf(stderr, "help: unknown command: %s\n", topic);
		return -1;
	} else {
		struct vector v;

		vector_init(&v, sizeof(const char *));
		sort_lists(cp);

		printf("Available commands:\n");
		namelist_print(&cp->command_list);
		printf("\n");

		if (!opdb_enum(push_option_name, &v)) {
			printf("Available options:\n");
			namelist_print(&v);
			printf("\n");
		} else {
			perror("failed to allocate memory");
		}

		vector_destroy(&v);

		printf("Type \"help <topic>\" for more information.\n");
		printf("Press Ctrl+D to quit.\n");
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
		return expr_eval(stab_default, word, &value->numeric);

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
	printf("%32s = ", key->name);

	switch (key->type) {
	case OPDB_TYPE_BOOLEAN:
		printf("%s", value->boolean ? "true" : "false");
		break;

	case OPDB_TYPE_NUMERIC:
		printf("0x%x (%u)", value->numeric, value->numeric);
		break;

	case OPDB_TYPE_STRING:
		printf("%s", value->string);
		break;
	}

	printf("\n");
	return 0;
}

static int cmd_opt(cproc_t cp, char **arg)
{
	const char *opt_text = get_arg(arg);
	struct opdb_key key;
	union opdb_value value;

	if (opt_text) {
		if (opdb_get(opt_text, &key, &value) < 0) {
			fprintf(stderr, "opt: no such option: %s\n",
				opt_text);
			return -1;
		}
	}

	if (**arg) {
		if (parse_option(key.type, &value, *arg) < 0) {
			fprintf(stderr, "opt: can't parse option: %s\n",
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

static int cmd_read(cproc_t cp, char **arg)
{
	char *filename = get_arg(arg);

	if (!filename) {
		fprintf(stderr, "read: filename must be specified\n");
		return -1;
	}

	return cproc_process_file(cp, filename);
}

static const struct cproc_command built_in_commands[] = {
	{
		.name = "help",
		.func = cmd_help,
		.help =
"help [command]\n"
"    Without arguments, displays a list of commands. With a command\n"
"    name as an argument, displays help for that command.\n"
	},
	{
		.name = "opt",
		.func = cmd_opt,
		.help =
"opt [name] [value]\n"
"    Query or set option variables. With no arguments, displays all\n"
"    available options.\n"
	},
	{
		.name = "read",
		.func = cmd_read,
		.help =
"read <filename>\n"
"    Read commands from a file and evaluate them.\n"
	}
};

cproc_t cproc_new(void)
{
	cproc_t cp = malloc(sizeof(*cp));

	if (!cp)
		return NULL;

	memset(cp, 0, sizeof(*cp));

	vector_init(&cp->command_list, sizeof(struct cproc_command));

	if (vector_push(&cp->command_list, &built_in_commands,
			ARRAY_LEN(built_in_commands)) < 0) {
		vector_destroy(&cp->command_list);
		free(cp);
		return NULL;
	}

	return cp;
}

void cproc_destroy(cproc_t cp)
{
	vector_destroy(&cp->command_list);
	free(cp);
}

int cproc_register_commands(cproc_t cp, const struct cproc_command *cmd,
			    int count)
{
	if (vector_push(&cp->command_list, cmd, count) < 0)
		return -1;

	cp->lists_modified = 1;
	return 0;
}

void cproc_modify(cproc_t cp, int flags)
{
	cp->modify_flags |= flags;
}

void cproc_unmodify(cproc_t cp, int flags)
{
	cp->modify_flags &= ~flags;
}

int cproc_prompt_abort(cproc_t cp, int flags)
{
        char buf[32];

        if (!(cp->in_reader_loop && (cp->modify_flags & flags)))
                return 0;

        for (;;) {
                printf("Symbols have not been saved since modification. "
                       "Continue (y/n)? ");
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

#ifndef USE_READLINE
#define LINE_BUF_SIZE 128

static char *readline(const char *prompt)
{
	char *buf = malloc(LINE_BUF_SIZE);

	if (!buf) {
		perror("readline: can't allocate memory");
		return NULL;
	}

	for (;;) {
		printf("(mspdebug) ");
		fflush(stdout);

		if (fgets(buf, LINE_BUF_SIZE, stdin))
			return buf;

		if (feof(stdin))
			break;

		printf("\n");
	}

	free(buf);
	return NULL;
}

#define add_history(x)
#endif

static int process_command(cproc_t cp, char *arg, int interactive)
{
	const char *cmd_text;
	int len = strlen(arg);

	while (len && isspace(arg[len - 1]))
		len--;
	arg[len] = 0;

	cmd_text = get_arg(&arg);
	if (cmd_text) {
		const struct cproc_command *cmd = find_command(cp, cmd_text);

		if (cmd) {
			int old = cp->in_reader_loop;
			int ret;

			cp->in_reader_loop = interactive;
			ret = cmd->func(cp, &arg);
			cp->in_reader_loop = old;

			return ret;
		}

		fprintf(stderr, "unknown command: %s (try \"help\")\n",
			cmd_text);
		return -1;
	}

	return 0;
}

void cproc_reader_loop(cproc_t cp)
{
	int old = cp->in_reader_loop;

	cp->in_reader_loop = 1;

	printf("\n");
	cmd_help(cp, NULL);
	printf("\n");

	do {
		for (;;) {
			char *buf = readline("(mspdebug) ");

			if (!buf)
				break;

			add_history(buf);
			process_command(cp, buf, 1);
			free(buf);
		}
	} while (cproc_prompt_abort(cp, CPROC_MODIFY_SYMS));

	printf("\n");
	cp->in_reader_loop = old;
}

int cproc_process_command(cproc_t cp, char *cmd)
{
	return process_command(cp, cmd, 0);
}

int cproc_process_file(cproc_t cp, const char *filename)
{
	FILE *in;
	char buf[1024];
	int line_no = 0;

	in = fopen(filename, "r");
	if (!in) {
		fprintf(stderr, "read: can't open %s: %s\n",
			filename, strerror(errno));
		return -1;
	}

	while (fgets(buf, sizeof(buf), in)) {
		char *cmd = buf;

		line_no++;

		while (*cmd && isspace(*cmd))
			cmd++;

		if (*cmd == '#')
			continue;

		if (process_command(cp, cmd, 0) < 0) {
			fprintf(stderr, "read: error processing %s (line %d)\n",
				filename, line_no);
			fclose(in);
			return -1;
		}
	}

	fclose(in);
	return 0;
}
