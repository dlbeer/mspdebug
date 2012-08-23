/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2011 Daniel Beer
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

#ifndef DYNLOAD_H_
#define DYNLOAD_H_

/* Portable dynamic loader interface. */

#ifdef __Windows__
#include <windows.h>

typedef HMODULE dynload_handle_t;
#else /* __Windows__ */
typedef void *dynload_handle_t;
#endif /* __Windows__ */

dynload_handle_t dynload_open(const char *filename);
void dynload_close(dynload_handle_t hnd);
void *dynload_sym(dynload_handle_t hnd, const char *name);
const char *dynload_error(void);

#endif
