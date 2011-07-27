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

#include "opdb.h"
#include "output.h"
#include "util.h"

struct outbuf {
	char	buf[4096];
	int	len;
	int	in_code;

	int	ansi_cur;
	int	ansi_num;
	int	ansi_next;
};

static struct outbuf stdout_buf;
static struct outbuf stderr_buf;

static capture_func_t capture_func;
static void *capture_data;

static void process_ansi_part(struct outbuf *out)
{
	if (!out->ansi_num) {
		out->ansi_next = 7;
	} else if (out->ansi_num == 1) {
		out->ansi_next |= 0x8;
	} else if (out->ansi_num >= 30) {
		const int attr = out->ansi_num % 10;
		const int col = ((attr & 1) << 2) |
		      (attr & 2) |
		      ((attr & 4) >> 2);

		if (out->ansi_num >= 40)
			out->ansi_next = (out->ansi_next & 0x0f) |
					(col << 4);
		else
			out->ansi_next = (out->ansi_next & 0xf0) | col;
	}

	out->ansi_num = 0;
}

static int write_text(struct outbuf *out, const char *buf, FILE *fout)
{
	int want_color = opdb_get_boolean("color");
	int len = 0;

	if (!out->ansi_cur)
		out->ansi_cur = 7;

	while (*buf) {
		if (*buf == 27) {
			out->in_code = 1;
			out->ansi_num = 0;
			out->ansi_next = out->ansi_cur;
		}

		if (!out->in_code)
			len++;

		if (*buf == '\n') {
			fputc('\n', fout);
			out->buf[out->len] = 0;
			if (capture_func)
				capture_func(capture_data, out->buf);
			out->len = 0;
		} else if (out->in_code) {
			if (isdigit(*buf)) {
				out->ansi_num =
					out->ansi_num * 10 + *buf - '0';
			} else if (*buf == ';') {
				process_ansi_part(out);
			} else if (isalpha(*buf)) {
				process_ansi_part(out);
				out->in_code = 0;
				if (*buf == 'm')
					out->ansi_cur = out->ansi_next;
#ifdef WIN32
				if (want_color && *buf == 'm') {
					fflush(fout);
					SetConsoleTextAttribute(GetStdHandle
					  (STD_OUTPUT_HANDLE), out->ansi_cur);
				}
#endif
			}

#ifndef WIN32
			if (want_color)
				fputc(*buf, fout);
#endif
		} else {
			if (out->len + 1 < sizeof(out->buf))
				out->buf[out->len++] = *buf;

			fputc(*buf, fout);
		}

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
	printc_err("%s: %s\n", prefix, last_error());
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
