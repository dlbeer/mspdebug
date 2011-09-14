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

#ifndef TRANSPORT_H_
#define TRANSPORT_H_

#include <stdint.h>

/* This structure is used to provide an interface to a lower-level
 * transport. The transport mechanism is viewed as a stream by the FET
 * controller, which handles packet encapsulation, checksums and other
 * high-level functions.
 */
struct transport;
typedef struct transport *transport_t;

struct transport {
	void (*destroy)(transport_t tr);
	int (*send)(transport_t tr, const uint8_t *data, int len);
	int (*recv)(transport_t tr, uint8_t *data, int max_len);
};

#endif
