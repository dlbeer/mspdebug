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

#ifdef __Windows__
#include <windows.h>
#endif

#include "opdb.h"
#include "output.h"
#include "util.h"

static capture_func_t capture_func;
static void *capture_data;
static int is_embedded_mode;

#define LINEBUF_SIZE	4096

struct linebuf {
	/* Line buffer */
	char		buf[LINEBUF_SIZE];
	int		len;

	/* Does the buffer contain a trailing incomplete ANSI code? */
	int		ansi_mode;
};

/* Return the lower three bits of n, reversed */
static int rev_bits(int n)
{
	const int a = (n & 1) << 2;
	const int b = (n & 2);
	const int c = (n & 4) >> 2;

	return a | b | c;
}

/* Take a Windows color code and an ANSI colour-change code component
 * and return the resulting Windows color code.
 */
static int ansi_apply(int old_state, int code)
{
	/* 0: reset */
	if (code == 0)
		return 7;

	/* 1: bold */
	if (code == 1)
		return old_state | 0x8;

	/* 30-37: foreground colour */
	if (code >= 30 && code <= 37)
		return (old_state & 0xf8) | rev_bits(code - 30);

	/* 40-47: background colour */
	if (code >= 40 && code <= 47)
		return (old_state & 0x0f) | (rev_bits(code - 40) << 4);

	return old_state;
}

/* Parse an ANSI code and compute the next Windows console colour code.
 * Returns the number of bytes consumed.
 */
static int parse_ansi(const char *text, int *ansi_state)
{
	int next_state = *ansi_state;
	int code = 0;
	int len = 0;

	/* Parse the ANSI code and see how long it is */
	while (text[len]) {
		char c = text[len++];

		if (isdigit(c)) {
			code = code * 10 + c - '0';
		} else {
			next_state = ansi_apply(next_state, code);
			code = 0;
		}

		if (isalpha(c))
			break;
	}

	*ansi_state = next_state;
	return len;
}

/* Parse printable characters, up to either the end of the line or the
 * next ANSI code. Returns the number of bytes consumed.
 */
static int parse_text(const char *text)
{
	int len = 0;

	while (text[len] && text[len] != 0x1b)
		len++;

	return len;
}

/* Print an ANSI code, or change the console text colour. */
static void emit_ansi(const char *code, int len, int ansi_state, FILE *out)
{
#ifdef __Windows__
	if (is_embedded_mode) {
		fwrite(code, 1, len, out);
	} else {
		fflush(out);
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE),
					ansi_state);
	}
#else
	fwrite(code, 1, len, out);
#endif
}

/* Process and print a single line of text. The given line of text must
 * be nul-terminated with no line-ending characters. Embedded ANSI
 * sequences are handled appropriately.
 */
static void handle_line(const char *text, FILE *out, char sigil)
{
	const int want_color = opdb_get_boolean("color");
	char cap_buf[LINEBUF_SIZE];
	int cap_len = 0;
	int ansi_state = 7;

	if (is_embedded_mode) {
		out = stdout;
		fputc(sigil, out);
	}

	while (*text) {
		int r;

		if (*text == 0x1b) {
			r = parse_ansi(text, &ansi_state);
			if (want_color)
				emit_ansi(text, r, ansi_state, out);
		} else {
			r = parse_text(text);

			memcpy(cap_buf + cap_len, text, r);
			cap_len += r;
			fwrite(text, 1, r, out);
		}

		text += r;
	}

	/* Reset colours if necessary */
	if (want_color && (ansi_state != 7))
		emit_ansi("\x1b[0m", 4, 7, out);

	fputc('\n', out);
	fflush(out);

	/* Invoke output capture callback */
	cap_buf[cap_len] = 0;
	if (capture_func)
		capture_func(capture_data, cap_buf);
}

/* Push a chunk of text, possibly with embedded ANSI sequences, into a
 * buffer. The text is reassembled into lines, and each line is
 * processed/printed once completely assembled.
 *
 * The number of printable (non-ANSI, non-newline) characters in the
 * chunk of text is returned. The buffer keeps track of whether or not
 * we're currently within an ANSI code, so pushing code fragments works
 * correctly.
 */
static int write_text(struct linebuf *ob, const char *text,
		      FILE *out, char sigil)
{
	int count = 0;

	/* Separate the text into lines and count the number of
	 * printing characters.
	 */
	while (*text) {
		if (*text == '\n') {
			ob->buf[ob->len] = 0;
			ob->len = 0;
			ob->ansi_mode = 0;
			handle_line(ob->buf, out, sigil);
		} else {
			if (*text == 0x1b)
				ob->ansi_mode = 1;

			if (ob->len + 1 < sizeof(ob->buf))
				ob->buf[ob->len++] = *text;
			if (!ob->ansi_mode)
				count++;

			if (isalpha(*text))
				ob->ansi_mode = 0;
		}

		text++;
	}

	return count;
}

static struct linebuf lb_normal;
static struct linebuf lb_debug;
static struct linebuf lb_error;
static struct linebuf lb_shell;

int printc(const char *fmt, ...)
{
	char buf[4096];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	return write_text(&lb_normal, buf, stdout, ':');
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

	return write_text(&lb_debug, buf, stdout, '-');
}

int printc_err(const char *fmt, ...)
{
	char buf[4096];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	return write_text(&lb_error, buf, stderr, '!');
}

int printc_shell(const char *fmt, ...)
{
	char buf[4096];
	va_list ap;

	if (!is_embedded_mode)
		return 0;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	return write_text(&lb_shell, buf, stdout, '\\');
}

void output_set_embedded(int enable)
{
	is_embedded_mode = enable;
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
