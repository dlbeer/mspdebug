/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2012 Daniel Beer
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

/* This structure is used to provide a consistent interface to a
 * lower-level serial-port type device.
 */
struct transport;
typedef struct transport *transport_t;

typedef enum {
	TRANSPORT_MODEM_DTR = 0x01,
	TRANSPORT_MODEM_RTS = 0x02
} transport_modem_t;

struct transport_class {
	/* Close the port and free resources */
	void (*destroy)(transport_t tr);

	/* Send a block of data. Returns 0 on success, or -1 if an error
	 * occurs.
	 */
	int (*send)(transport_t tr, const uint8_t *data, int len);

	/* Receive a block of data, up to the maximum size. Returns the
	 * number of bytes received on success (which must be non-zero),
	 * or -1 if an error occurs. Read timeouts are treated as
	 * errors.
	 */
	int (*recv)(transport_t tr, uint8_t *data, int max_len);

	/* Flush any lingering data in either direction. */
	int (*flush)(transport_t tr);

	/* Set modem control lines. Returns 0 on success or -1 if an
	 * error occurs.
	 */
	int (*set_modem)(transport_t tr, transport_modem_t state);

	/* This pair of optional methods allows a transport to survive a
	 * USB device reset. Before an impending reset, suspend() should
	 * be called to release references to the bus. After the reset
	 * is completed, resume() should be called to reattach.
	 *
	 * It is an error to invoke IO methods on a suspended device.
	 */
	int (*suspend)(transport_t tr);
	int (*resume)(transport_t tr);
};

struct transport {
	const struct transport_class	*ops;
};

#endif
