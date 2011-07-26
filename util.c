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
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>

#include "util.h"
#include "output.h"

static volatile int ctrlc_flag;

static void sigint_handler(int signum)
{
	ctrlc_flag = 1;
}

void ctrlc_init(void)
{
#if defined(WIN32) || defined(__CYGWIN__)
       signal(SIGINT, sigint_handler);
#else
       const static struct sigaction siga = {
               .sa_handler = sigint_handler,
               .sa_flags = 0
       };

       sigaction(SIGINT, &siga, NULL);
#endif
}

void ctrlc_reset(void)
{
	ctrlc_flag = 0;
}

int ctrlc_check(void)
{
	return ctrlc_flag;
}

char *get_arg(char **text)
{
	char *start;
	char *rewrite;
	char *end;
	int qstate = 0;
	int qval = 0;

	if (!text)
		return NULL;

	start = *text;
	while (*start && isspace(*start))
		start++;

	if (!*start)
		return NULL;

	/* We've found the start of the argument. Parse it. */
	end = start;
	rewrite = start;
	while (*end) {
		switch (qstate) {
		case 0: /* Bare */
			if (isspace(*end))
				goto out;
			else if (*end == '"')
				qstate = 1;
			else
				*(rewrite++) = *end;
			break;

		case 1: /* In quotes */
			if (*end == '"')
				qstate = 0;
			else if (*end == '\\')
				qstate = 2;
			else
				*(rewrite++) = *end;
			break;

		case 2: /* Backslash */
			if (*end == '\\')
				*(rewrite++) = '\\';
			else if (*end == 'n')
				*(rewrite++) = '\n';
			else if (*end == 'r')
				*(rewrite++) = '\r';
			else if (*end == 't')
				*(rewrite++) = '\t';
			else if (*end >= '0' && *end <= '3') {
				qstate = 30;
				qval = *end - '0';
			} else if (*end == 'x') {
				qstate = 40;
				qval = 0;
			} else
				*(rewrite++) = *end;

			if (qstate == 2)
				qstate = 1;
			break;

		case 30: /* Octal */
		case 31:
			if (*end >= '0' && *end <= '7')
				qval = (qval << 3) | (*end - '0');

			if (qstate == 31) {
				*(rewrite++) = qval;
				qstate = 1;
			} else {
				qstate++;
			}
			break;

		case 40: /* Hex */
		case 41:
			if (isdigit(*end))
				qval = (qval << 4) | (*end - '0');
			else if (isupper(*end))
				qval = (qval << 4) | (*end - 'A' + 10);
			else if (islower(*end))
				qval = (qval << 4) | (*end - 'a' + 10);

			if (qstate == 41) {
				*(rewrite++) = qval;
				qstate = 1;
			} else {
				qstate++;
			}
			break;
		}

		end++;
	}
 out:
	/* Leave the text pointer at the end of the next argument */
	while (*end && isspace(*end))
		end++;

	*rewrite = 0;
	*text = end;
	return start;
}

void debug_hexdump(const char *label, const uint8_t *data, int len)
{
	int offset = 0;

	printc("%s [0x%x bytes]\n", label, len);
	while (offset < len) {
		int i;

		printc("    ");
		for (i = 0; i < 16 && offset + i < len; i++)
			printc("%02x ", data[offset + i]);
		printc("\n");

		offset += i;
	}
}

int hexval(int c)
{
	if (isdigit(c))
		return c - '0';
	if (isupper(c))
		return c - 'A' + 10;
	if (islower(c))
		return c - 'a' + 10;

	return 0;
}
