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

#ifndef UIF_H_
#define UIF_H_

#include "transport.h"

typedef enum {
	UIF_TYPE_FET,
	UIF_TYPE_OLIMEX,
	UIF_TYPE_OLIMEX_ISO
} uif_type_t;

/* This function is for opening an eZ430-F2013 or FET430UIF device via
 * a kernel-supported serial interface. The argument given should be the
 * filename of the relevant tty device.
 */
transport_t uif_open(const char *device, uif_type_t type);

#endif
