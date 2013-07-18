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

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

#define LE_BYTE(b, x) ((int)((uint8_t *)(b))[x])
#define LE_WORD(b, x) ((LE_BYTE(b, x + 1) << 8) | LE_BYTE(b, x))
#define LE_LONG(b, x) ((LE_WORD(b, x + 2) << 16) | LE_WORD(b, x))

/* This type fits an MSP430X register value */
typedef uint32_t address_t;

#define ADDRESS_NONE ((address_t)0xffffffff)

/* Retrive a string describing the last system error */
const char *last_error(void);

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

#ifdef __Windows__
char *strsep(char **strp, const char *delim);
#endif

/* Expand `~' in path names. Caller must free the returned ptr */
char *expand_tilde(const char *path);

/* Sleep for a number of seconds (_s) or milliseconds (_ms) */
int delay_s(unsigned int s);
int delay_ms(unsigned int s);

/* Base64 encode a block without breaking into lines. Returns the number
 * of source bytes encoded. The output is nul-terminated.
 */
static inline int base64_encoded_size(int decoded_size)
{
	return ((decoded_size + 2) / 3) * 4;
}

int base64_encode(const uint8_t *src, int len, char *dst, int max_len);

/* printf format for long long args */
#ifdef __MINGW32__
#define LLFMT "I64d"
#else
#define LLFMT "lld"
#endif

#endif
