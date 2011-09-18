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

#include <stdint.h>
#include <ctype.h>

#ifdef WIN32
#include <windows.h>
#endif

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

#define LE_BYTE(b, x) ((int)((uint8_t *)(b))[x])
#define LE_WORD(b, x) ((LE_BYTE(b, x + 1) << 8) | LE_BYTE(b, x))
#define LE_LONG(b, x) ((LE_WORD(b, x + 2) << 16) | LE_WORD(b, x))

/* This type fits an MSP430X register value */
typedef uint32_t address_t;

/* Retrive a string describing the last system error */
const char *last_error(void);

/* Check and catch ^C from the user */
void ctrlc_init(void);
void ctrlc_exit(void);
void ctrlc_reset(void);
int ctrlc_check(void);

/* Retrieve the next word from a pointer to the rest of a command
 * argument buffer. Returns NULL if no more words.
 */
char *get_arg(char **text);

/* Display hex output for debug purposes */
void debug_hexdump(const char *label,
		   const uint8_t *data, int len);

static inline int ishex(int c)
{
	return isdigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

int hexval(int c);

#ifdef WIN32
char *strsep(char **strp, const char *delim);

HANDLE ctrlc_win32_event(void);
#endif

/* Expand `~' in path names. Caller must free the returned ptr */
char *expand_tilde(const char *path);

#endif
