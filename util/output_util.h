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

#ifndef OUTPUT_UTIL_H_
#define OUTPUT_UTIL_H_

#include "output.h"
#include "util.h"
#include "powerbuf.h"

/* Print colorized disassembly on command processor standard output */
void disassemble(address_t addr, const uint8_t *buf, int len,
		 powerbuf_t power);

/* Print colorized hexdump on standard output */
void hexdump(address_t addr, const uint8_t *buf, int len);

/* Colorized register dump */
void show_regs(const address_t *regs);

/* Given an address, format it either as sym+0x0offset or just 0x0offset.
 *
 * Returns non-zero if the result is of the form sym+0x0offset.
 */
typedef enum {
	PRINT_ADDRESS_EXACT	= 0x01
} print_address_flags_t;

int print_address(address_t addr, char *buf, int max_len,
		  print_address_flags_t f);

/* Name lists. This function is used for printing multi-column sorted
 * lists of constant strings. Expected is a vector of const char *.
 */
void namelist_print(struct vector *v);

#endif
