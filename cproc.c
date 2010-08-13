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
#include "cmddb.h"
#include "stdcmd.h"

struct cproc {
	int                     modify_flags;
	int                     in_reader_loop;
};

cproc_t cproc_new(void)
{
	cproc_t cp = malloc(sizeof(*cp));

	if (!cp)
		return NULL;

	memset(cp, 0, sizeof(*cp));
	return cp;
}

void cproc_destroy(cproc_t cp)
{
	free(cp);
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
		struct cmddb_record cmd;

		if (!cmddb_get(cmd_text, &cmd)) {
			int old = cp->in_reader_loop;
			int ret;

			cp->in_reader_loop = interactive;
			ret = cmd.func(cp, &arg);
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
