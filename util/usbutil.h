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

#ifndef USBUTIL_H_
#define USBUTIL_H_

#include <libusb.h>

/* List all available USB devices. */
void usbutil_list(void);

/* Search for the first device matching the given Vendor:Product */
libusb_device *usbutil_find_by_id(int vendor, int product,
				      const char *requested_serial);

/* Search for a device using a bus:dev location string */
libusb_device *usbutil_find_by_loc(const char *loc);

#endif
