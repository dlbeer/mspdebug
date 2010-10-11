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

#ifndef FET_H_
#define FET_H_

#include "device.h"
#include "transport.h"

/* MSP430 FET protocol implementation. */
#define FET_PROTO_SPYBIWIRE	0x01
#define FET_PROTO_RF2500	0x02
#define FET_PROTO_OLIMEX        0x04

device_t fet_open(transport_t transport, int proto_flags, int vcc_mv,
		  const char *force_id);

#endif
