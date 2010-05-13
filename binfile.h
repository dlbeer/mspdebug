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
typedef int (*imgfunc_t)(void *user_data,
			 uint16_t addr, const uint8_t *data, int len);

/* Intel HEX file support */
int ihex_check(FILE *in);
int ihex_extract(FILE *in, imgfunc_t cb, void *user_data);

/* ELF32 file support */
int elf32_check(FILE *in);
int elf32_extract(FILE *in, imgfunc_t cb, void *user_data);
int elf32_syms(FILE *in, stab_t stab);

/* *.map file support */
int symmap_check(FILE *in);
int symmap_syms(FILE *in, stab_t stab);

#endif
