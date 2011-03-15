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
#include <errno.h>
#include "opdb.h"
#include "output.h"

struct outbuf {
	char	buf[4096];
	int	len;
	int	in_code;
};

static struct outbuf stdout_buf;
static struct outbuf stderr_buf;

static capture_func_t capture_func;
static void *capture_data;

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
			if (capture_func)
				capture_func(capture_data, out->buf);
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
	char buf[4096];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	return write_text(&stdout_buf, buf, stdout);
}

int printc_dbg(const char *fmt, ...)
{
	char buf[4096];
	va_list ap;

	if (opdb_get_boolean("quiet"))
		return 0;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	return write_text(&stdout_buf, buf, stdout);
}

int printc_err(const char *fmt, ...)
{
	char buf[4096];
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

void capture_start(capture_func_t func, void *data)
{
	capture_func = func;
	capture_data = data;
}

void capture_end(void)
{
	capture_func = NULL;
}

/************************************************************************
 * Name lists
 */

static int namelist_cmp(const void *a, const void *b)
{
	return strcasecmp(*(const char **)a, *(const char **)b);
}

void namelist_print(struct vector *v)
{
	int i;
	int max_len = 0;
	int rows, cols;

	qsort(v->ptr, v->size, v->elemsize, namelist_cmp);

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

		printc("    ");
		for (j = 0; j < cols; j++) {
			int k = j * rows + i;
			const char *text;

			if (k >= v->size)
				break;

			text = VECTOR_AT(*v, k, const char *);
			printc("%s", text);
			for (k = strlen(text); k < max_len; k++)
				printc(" ");
		}

		printc("\n");
	}
}
