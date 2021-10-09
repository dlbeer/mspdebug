/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2021 sys64738@disroot.org
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

#ifndef MEHFET_TRANSPORT_H_
#define MEHFET_TRANSPORT_H_

#include "transport.h"

enum mehfet_status {
	mehfet_ok = 0x00,

	mehfet_badargs    = 0x7b,
	mehfet_nocaps     = 0x7c,
	mehfet_badstate   = 0x7d,
	mehfet_invalidcmd = 0x7e,
	mehfet_error      = 0x7f
};


/* Search the USB bus for the first MehFET device and initialize it.
 * If successful, return a transport object. Otherwise, return NULL.
 *
 * A particular USB device or serial number may be specified.
 */
transport_t mehfet_transport_open(const char *usb_device,
		      const uint16_t* vendor, const uint16_t* product,
		      const char *requested_serial);

int  mehfet_transport_get_buf_size(transport_t xport);
void mehfet_transport_set_buf_size(transport_t xport, int buf_size);

int mehfet_send_raw(transport_t xport, uint8_t cmd, int datalen, const void* data);
int mehfet_recv_raw(transport_t xport, uint8_t* stat, int* datalen, void* data);

int mehfet_err_on_stat(const char* pre, uint8_t stat, int datalen, const void* data);

#endif
