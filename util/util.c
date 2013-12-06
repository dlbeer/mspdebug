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
#include <assert.h>
#include <time.h>

#ifdef __Windows__
#include <windows.h>
#endif

#include "ctrlc.h"
#include "util.h"
#include "output.h"

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

#ifdef __Windows__
char *strsep(char **strp, const char *delim)
{
	char *start = *strp;
	char *end = start;

	if (!start)
		return NULL;

	while (*end) {
		const char *d = delim;

		while (*d) {
			if (*d == *end) {
				*(end++) = 0;
				*strp = end;
				return start;
			}

			d++;
		}

		end++;
	}

	*strp = NULL;
	return start;
}
#endif

#ifdef __Windows__
const char *last_error(void)
{
	DWORD err = GetLastError();
	static char msg_buf[128];
	int len;

	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, err, 0,
		      msg_buf, sizeof(msg_buf), NULL);

	/* Trim trailing newline characters */
	len = strlen(msg_buf);
	while (len > 0 && isspace(msg_buf[len - 1]))
		len--;
	msg_buf[len] = 0;

	return msg_buf;
}
#else
const char *last_error(void)
{
	return strerror(errno);
}
#endif

/* Expand leading `~/' in path names. Caller must free the returned ptr */
char *expand_tilde(const char *path)
{
	char *home, *expanded;
	size_t len;

	if (!path)
		return NULL;

	if (!*path)
		return strdup("");

	expanded = NULL;

	if (*path == '~' && *(path + 1) == '/') {
		home = getenv("HOME");

		if (home) {
			/* Trailing '\0' will fit in leading '~'s place */
			len = strlen(home) + strlen(path);
			expanded = (char *)malloc(len);

			if (expanded)
				snprintf(expanded, len, "%s%s", home, path + 1);
			else
				printc_err("%s: malloc: %s\n", __FUNCTION__,
					   last_error());

		} else {
			printc_err("%s: getenv: %s\n", __FUNCTION__,
				   last_error());
		}
	} else {
		expanded = strdup(path);

		if (!expanded)
			printc_err("%s: malloc: %s\n", __FUNCTION__,
				   last_error());
	}

	/* Caller must free()! */
	return expanded;
}

#ifdef __Windows__
int delay_s(unsigned int s)
{
	Sleep(s * 1000);

	return 0;
}

int delay_ms(unsigned int s)
{
	Sleep(s);

	return 0;
}
#else
int delay_s(unsigned int s)
{
	return delay_ms(1000 * s);
}

int delay_ms(unsigned int s)
{
	struct timespec rq, rm;
	int ret;

	rm.tv_sec = s / 1000;
	rm.tv_nsec = (s % 1000) * 1000000;

	do {
		if (ctrlc_check()) {
			ret = -1;
			break;
		}
		rq.tv_sec = rm.tv_sec;
		rq.tv_nsec = rm.tv_nsec;
		ret = nanosleep(&rq, &rm);
	} while(ret == -1 && errno == EINTR);

	return ret;
}
#endif

int base64_encode(const uint8_t *src, int len, char *dst, int max_len)
{
	static const char basis[] =
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	    "abcdefghijklmnopqrstuvwxyz0123456789+/";
	int i = 0;
	int k = 0;

	while ((i < len) && (k + 4) < max_len) {
		int a = src[i++];

		dst[k++] = basis[a >> 2];

		if (i < len) {
			int b = src[i++];

			dst[k++] = basis[((a & 3) << 4) | (b & 0xf0) >> 4];
			if (i < len) {
				int c = src[i++];

				dst[k++] = basis[((b & 0xf) << 2) |
						 ((c & 0xc0) >> 6)];
				dst[k++] = basis[c & 0x3f];
			} else {
				dst[k++] = basis[(b & 0xf) << 2];
				dst[k++] = '=';
			}
		} else {
			dst[k++] = basis[(a & 3) << 4];
			dst[k++] = '=';
			dst[k++] = '=';
		}
	}

	dst[k] = 0;
	return i;
}
