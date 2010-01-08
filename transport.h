/* MSPDebug - debugging tool for the eZ430
 * Copyright (C) 2009 Daniel Beer
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

#include <sys/types.h>

/* This structure is used to provide an interface to a lower-level
 * transport. The transport mechanism is viewed as a stream by the FET
 * controller, which handles packet encapsulation, checksums and other
 * high-level functions.
 */
struct fet_transport {
	int (*flush)(void);
	int (*send)(const u_int8_t *data, int len);
	int (*recv)(u_int8_t *data, int max_len);
	void (*close)(void);
};

/* This function is for opening an eZ430-F2013 or FET430UIF device via
 * a kernel-supported serial interface. The argument given should be the
 * filename of the relevant tty device.
 */
const struct fet_transport *uif_open(const char *device);

/* Search the USB bus for the first eZ430-RF2500, and initialize it. If
 * successful, 0 is returned and the fet_* functions are ready for use.
 * If an error occurs, -1 is returned.
 */
const struct fet_transport *rf2500_open(void);

#endif
