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

#ifndef UTIL_H_
#define UTIL_H_

#include <sys/types.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/* Various utility functions for IO */
int open_serial(const char *device, int rate);
int read_with_timeout(int fd, u_int8_t *data, int len);
int write_all(int fd, const u_int8_t *data, int len);

/* Check and catch ^C from the user */
void ctrlc_init(void);
void ctrlc_reset(void);
int ctrlc_check(void);

/* Retrieve the next word from a pointer to the rest of a command
 * argument buffer. Returns NULL if no more words.
 */
char *get_arg(char **text);

/* Display hex output for debug purposes */
void debug_hexdump(const char *label,
		   const u_int8_t *data, int len);

/* Get text length, not including ANSI codes */
int textlen(const char *text);

#endif
