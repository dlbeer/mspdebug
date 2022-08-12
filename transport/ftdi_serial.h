/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2012 Daniel Beer
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

#ifndef FTDI_H_
#define FTDI_H_

#include "transport.h"

/* Search the USB bus for the first Olimex ISO device and initialize it.
 * If successful, return a transport object. Otherwise, return NULL.
 *
 * A particular USB device or serial number may be specified.
 */
transport_t ftdi_open(const char *usb_device,
		      const char *requested_serial,
		      uint16_t vendor, uint16_t product,
		      int baud_rate);

#endif
