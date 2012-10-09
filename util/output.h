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

#ifndef OUTPUT_H_
#define OUTPUT_H_

#include "vector.h"

/* Print output. ANSI colour codes may be embedded, and these will be
 * stripped on output if colour output is disabled.
 *
 * Returns the number of characters printed (not including colour
 * codes).
 */
int printc(const char *fmt, ...)
	__attribute__((format (printf, 1, 2)));
int printc_dbg(const char *fmt, ...)
	__attribute__((format (printf, 1, 2)));
int printc_err(const char *fmt, ...)
	__attribute__((format (printf, 1, 2)));
int printc_shell(const char *fmt, ...)
	__attribute__((format (printf, 1, 2)));

void pr_error(const char *prefix);

/* Enable embedded output mode. When enabled, all logical streams
 * are sent to stdout (not stderr), and prefixed with the following
 * sigils:
 *
 *   : normal
 *   ! error
 *   - debug
 *   \ shell
 *
 * Additionally, ANSI codes are used for colourized output on Windows
 * instead of changing the console text attributes.
 */
void output_set_embedded(int enable);

/* Capture output. Capturing is started by calling capture_begin() with
 * a callback function. The callback is invoked for each line of output
 * printed to either stdout or stderr (output still goes to
 * stdout/stderr as well).
 *
 * Capture is ended by calling capture_end().
 */
typedef void (*capture_func_t)(void *user_data, const char *text);

void capture_start(capture_func_t, void *user_data);
void capture_end(void);

#endif
