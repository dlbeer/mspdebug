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
#include <stdarg.h>
#include "opdb.h"
#include "output.h"

static char out_buf[1024];
static int out_len;
static int in_code;

static int write_text(const char *buf)
{
	int want_color = opdb_get_boolean("color");
	int len = 0;

	while (*buf) {
		if (*buf == 27)
			in_code = 1;

		if (!in_code)
			len++;

		if (*buf == '\n') {
			out_buf[out_len] = 0;
			puts(out_buf);
			out_len = 0;
		} else if ((want_color || !in_code) &&
			   out_len + 1 < sizeof(out_buf)) {
			out_buf[out_len++] = *buf;
		}

		if (isalpha(*buf))
			in_code = 0;

		buf++;
	}

	return len;
}

int printc(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	return write_text(buf);
}
