/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2012 Daniel Beer
 * Copyright (C) 2010 Peter Jansen
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

#ifndef CDC_ACM_H_
#define CDC_ACM_H_

#include "transport.h"

/* Search the USB bus for the first CDC-ACM device, and initialize it.
 * If successful, return a valid transport object.
 *
 * A particular USB device may be specified in bus:dev form.
 */
transport_t cdc_acm_open(const char *usb_device, const char *requested_serial,
			 int baud_rate, uint16_t product, uint16_t vendor);

#endif
