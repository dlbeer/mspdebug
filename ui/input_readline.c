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
#include "util.h"

#define HISTORY_FILENAME "~/.mspdebug_history"

static int readline_init(void)
{
	char *path = expand_tilde(HISTORY_FILENAME);
	if (path) {
		read_history(path);
		free(path);
	}
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
