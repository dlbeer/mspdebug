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

#ifndef CPROC_UTIL_H_
#define CPROC_UTIL_H_

#include <sys/types.h>
#include "cproc.h"

/* Print colorized disassembly on command processor standard output */
void cproc_disassemble(cproc_t cp, u_int16_t addr,
		       const u_int8_t *buf, int len);

/* Print colorized hexdump on standard output */
void cproc_hexdump(cproc_t cp, u_int16_t addr,
		   const u_int8_t *buf, int len);

/* Colorized register dump */
void cproc_regs(cproc_t cp, const u_int16_t *regs);

#endif
