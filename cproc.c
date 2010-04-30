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

#include "cproc.h"
#include "vector.h"
#include "stab.h"
#include "util.h"

struct cproc {
	struct vector           command_list;
	struct vector           option_list;
	int                     lists_modified;

	int                     modify_flags;
	int                     in_reader_loop;

	device_t                device;
};

static struct cproc_option *find_option(cproc_t cp, const char *name)
{
	int i;

	for (i = 0; i < cp->option_list.size; i++) {
		struct cproc_option *opt =
			VECTOR_PTR(cp->option_list, i, struct cproc_option);

		if (!strcasecmp(opt->name, name))
			return opt;
	}

	return NULL;
}

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

	if (cp->option_list.ptr)
		qsort(cp->option_list.ptr, cp->option_list.size,
		      cp->option_list.elemsize, namelist_cmp);

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

static const char *type_text(cproc_option_type_t type)
{
	switch (type) {
	case CPROC_OPTION_BOOL:
		return "boolean";

	case CPROC_OPTION_NUMERIC:
		return "numeric";

	case CPROC_OPTION_STRING:
		return "text";
	}

	return "unknown";
}

static int cmd_help(cproc_t cp, char **arg)
{
	const char *topic = get_arg(arg);

	if (topic) {
		const struct cproc_command *cmd = find_command(cp, topic);
		const struct cproc_option *opt = find_option(cp, topic);

		if (cmd) {
			cproc_printf(cp, "\x1b[1mCOMMAND: %s\x1b[0m\n",
				     cmd->name);
			fputs(cmd->help, stdout);
			if (opt)
				printf("\n");
		}

		if (opt) {
			cproc_printf(cp, "\x1b[1mOPTION: %s (%s)\x1b[0m\n",
				     opt->name, type_text(opt->type));
			fputs(opt->help, stdout);
		}

		if (!(cmd || opt)) {
			fprintf(stderr, "help: unknown command: %s\n", topic);
			return -1;
		}
	} else {
		sort_lists(cp);

		printf("Available commands:\n");
		namelist_print(&cp->command_list);
		printf("\n");

		printf("Available options:\n");
		namelist_print(&cp->option_list);
		printf("\n");

		printf("Type \"help <topic>\" for more information.\n");
		printf("Press Ctrl+D to quit.\n");
	}

	return 0;
}

static int parse_option(struct cproc_option *o, const char *word)
{
	switch (o->type) {
	case CPROC_OPTION_BOOL:
		o->data.numeric = (isdigit(word[0]) && word[0] > '0') ||
			word[0] == 't' || word[0] == 'y' ||
			(word[0] == 'o' && word[1] == 'n');
		break;

	case CPROC_OPTION_NUMERIC:
		return stab_exp(word, &o->data.numeric);

	case CPROC_OPTION_STRING:
		strncpy(o->data.text, word, sizeof(o->data.text));
		o->data.text[sizeof(o->data.text) - 1] = 0;
		break;
	}

	return 0;
}

static void display_option(const struct cproc_option *o)
{
	printf("%32s = ", o->name);

	switch (o->type) {
	case CPROC_OPTION_BOOL:
		printf("%s", o->data.numeric ? "true" : "false");
		break;

	case CPROC_OPTION_NUMERIC:
		printf("0x%x (%d)", o->data.numeric,
		       o->data.numeric);
		break;

	case CPROC_OPTION_STRING:
		printf("%s", o->data.text);
		break;
	}

	printf("\n");
}

static int cmd_opt(cproc_t cp, char **arg)
{
	const char *opt_text = get_arg(arg);
	struct cproc_option *opt = NULL;

	sort_lists(cp);

	if (opt_text) {
		opt = find_option(cp, opt_text);
		if (!opt) {
			fprintf(stderr, "opt: no such option: %s\n",
				opt_text);
			return -1;
		}
	}

	if (**arg) {
		if (parse_option(opt, *arg) < 0) {
			fprintf(stderr, "opt: can't parse option: %s\n",
				*arg);
			return -1;
		}
	} else if (opt_text) {
		display_option(opt);
	} else {
		int i;

		for (i = 0; i < cp->option_list.size; i++)
			display_option(VECTOR_PTR(cp->option_list, i,
						  struct cproc_option));
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

static const struct cproc_option built_in_options[] = {
	{
		.name = "color",
		.type = CPROC_OPTION_BOOL,
		.help = "Colorize debugging output.\n"
	}
};

cproc_t cproc_new(device_t dev)
{
	cproc_t cp = malloc(sizeof(*cp));

	if (!cp)
		return NULL;

	memset(cp, 0, sizeof(*cp));

	cp->device = dev;

	vector_init(&cp->command_list, sizeof(struct cproc_command));
	vector_init(&cp->option_list, sizeof(struct cproc_option));

	if (vector_push(&cp->command_list, &built_in_commands,
			ARRAY_LEN(built_in_commands)) < 0 ||
	    vector_push(&cp->option_list, &built_in_options,
			ARRAY_LEN(built_in_options)) < 0) {
		vector_destroy(&cp->command_list);
		vector_destroy(&cp->option_list);
		free(cp);
		return NULL;
	}

	return cp;
}

void cproc_destroy(cproc_t cp)
{
	cp->device->destroy(cp->device);
	vector_destroy(&cp->command_list);
	vector_destroy(&cp->option_list);
	free(cp);
}

device_t cproc_device(cproc_t cp)
{
	return cp->device;
}

int cproc_register_commands(cproc_t cp, const struct cproc_command *cmd,
			    int count)
{
	if (vector_push(&cp->command_list, cmd, count) < 0)
		return -1;

	cp->lists_modified = 1;
	return 0;
}

int cproc_register_options(cproc_t cp, const struct cproc_option *opt,
			   int count)
{
	if (vector_push(&cp->option_list, opt, count) < 0)
		return -1;

	cp->lists_modified = 1;
	return 0;
}

void cproc_printf(cproc_t cp, const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	int want_color = 0;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	cproc_get_int(cp, "color", &want_color);
	if (!want_color) {
		char *src = buf;
		char *dst = buf;

		for (;;) {
			if (*src == 27) {
				while (*src && !isalpha(*src))
					src++;
				if (*src)
					src++;
			}

			if (!*src)
				break;

			*(dst++) = *(src++);
		}

		*dst = 0;
	}

	puts(buf);
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

int cproc_get_int(cproc_t cp, const char *name, int *value)
{
	struct cproc_option *opt = find_option(cp, name);

	if (!opt)
		return -1;

	if (opt->type == CPROC_OPTION_NUMERIC ||
	    opt->type == CPROC_OPTION_BOOL) {
		*value = opt->data.numeric;
		return 0;
	}

	return -1;
}

int cproc_get_string(cproc_t cp, const char *name, char *value, int max_len)
{
	struct cproc_option *opt = find_option(cp, name);

	if (!opt)
		return -1;

	if (opt->type == CPROC_OPTION_STRING) {
		strncpy(value, opt->data.text, max_len);
		value[max_len - 1] = 0;
		return 0;
	}

	return -1;
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
