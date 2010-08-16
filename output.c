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
#include <string.h>
#include <errno.h>
#include "opdb.h"
#include "output.h"

struct outbuf {
	char	buf[1024];
	int	len;
	int	in_code;
};

static struct outbuf stdout_buf;
static struct outbuf stderr_buf;

static int write_text(struct outbuf *out, const char *buf, FILE *fout)
{
	int want_color = opdb_get_boolean("color");
	int len = 0;

	while (*buf) {
		if (*buf == 27)
			out->in_code = 1;

		if (!out->in_code)
			len++;

		if (*buf == '\n') {
			out->buf[out->len] = 0;
			fprintf(fout, "%s\n", out->buf);
			out->len = 0;
		} else if ((want_color || !out->in_code) &&
			   out->len + 1 < sizeof(out->buf)) {
			out->buf[out->len++] = *buf;
		}

		if (isalpha(*buf))
			out->in_code = 0;

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

	return write_text(&stdout_buf, buf, stdout);
}

int printc_err(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	return write_text(&stderr_buf, buf, stderr);
}

void pr_error(const char *prefix)
{
	printc_err("%s: %s\n", prefix, strerror(errno));
}
