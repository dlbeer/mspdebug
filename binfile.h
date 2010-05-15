/* MSPDebug - debugging tool for the eZ430
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

#ifndef BINFILE_H_
#define BINFILE_H_

#include <stdio.h>
#include <stdint.h>
#include "stab.h"

/* Callback for binary image data */
typedef int (*binfile_imgcb_t)(void *user_data,
			       uint16_t addr, const uint8_t *data, int len);

#define BINFILE_HAS_SYMS        0x01
#define BINFILE_HAS_TEXT        0x02

/* Examine the given file and figure out what it contains. If the file
 * type is unknown, 0 is returned. If an IO error occurs, -1 is
 * returned. Otherwise, the return value is a positive integer
 * bitmask.
 */
int binfile_info(FILE *in);

/* If possible, extract the text from this file, feeding it in chunks
 * of an indeterminate size to the callback given.
 *
 * Returns 0 if successful, -1 if an error occurs.
 */
int binfile_extract(FILE *in, binfile_imgcb_t cb, void *user_data);

/* Attempt to load symbols from the file and store them in the given
 * symbol table. Returns 0 on success or -1 if an error occurs.
 */
int binfile_syms(FILE *in, stab_t stab);

#endif
