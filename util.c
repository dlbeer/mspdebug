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
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>

#include "util.h"

static volatile int ctrlc_flag;

static void sigint_handler(int signum)
{
	ctrlc_flag = 1;
}

void ctrlc_init(void)
{
	signal(SIGINT, sigint_handler);
}

void ctrlc_reset(void)
{
	ctrlc_flag = 0;
}

int ctrlc_check(void)
{
	return ctrlc_flag;
}

int read_with_timeout(int fd, uint8_t *data, int max_len)
{
	int r;

	do {
		struct timeval tv = {
			.tv_sec = 5,
			.tv_usec = 0
		};

		fd_set set;

		FD_ZERO(&set);
		FD_SET(fd, &set);

		r = select(fd + 1, &set, NULL, NULL, &tv);
		if (r > 0)
			r = read(fd, data, max_len);

		if (!r)
			errno = ETIMEDOUT;
		if (r <= 0 && errno != EINTR)
			return -1;
	} while (r <= 0);

	return r;
}

int write_all(int fd, const uint8_t *data, int len)
{
	while (len) {
		int result = write(fd, data, len);

		if (result < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}

		data += result;
		len -= result;
	}

	return 0;
}

int open_serial(const char *device, int rate)
{
	int fd = open(device, O_RDWR | O_NOCTTY);
	struct termios attr;

	tcgetattr(fd, &attr);
	cfmakeraw(&attr);
	cfsetispeed(&attr, rate);
	cfsetospeed(&attr, rate);

	if (tcsetattr(fd, TCSAFLUSH, &attr) < 0)
		return -1;

	return fd;
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

	printf("%s [0x%x bytes]\n", label, len);
	while (offset < len) {
		int i;

		printf("    ");
		for (i = 0; i < 16 && offset + i < len; i++)
			printf("%02x ", data[offset + i]);
		printf("\n");

		offset += i;
	}
}

int textlen(const char *text)
{
	int count = 0;

	for (;;) {
		if (*text == 27) {
			while (*text && !isalpha(*text))
				text++;
			if (*text)
				text++;
		}

		if (!*text)
			break;

		count++;
		text++;
	}

	return count;
}
