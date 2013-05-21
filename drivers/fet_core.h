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

#ifndef FET_CORE_H_
#define FET_CORE_H_

#include "device.h"
#include "transport.h"

/* Don't attempt to close JTAG on exit */
#define FET_SKIP_CLOSE			0x04

/* The new identify method should always be used */
#define FET_IDENTIFY_NEW		0x08

/* A reset on startup should always be performed */
#define FET_FORCE_RESET			0x10

/* To create a FET-like driver, you need to call this function to return
 * a device object. You need to provide:
 *
 *    - device arguments
 *    - a transport (serial port)
 *    - protocol flags for the FET protocol
 *    - flags which might affect FET high-level behaviour
 *    - a device class (vtable)
 */
device_t fet_open(const struct device_args *args,
		  int proto_flags, transport_t transport,
		  int fet_flags,
		  const struct device_class *type);

/* These methods implement the device interface for FET-like drivers. */
int fet_erase(device_t dev_base, device_erase_type_t type,
	      address_t addr);

device_status_t fet_poll(device_t dev_base);

int fet_ctl(device_t dev_base, device_ctl_t action);

void fet_destroy(device_t dev_base);

int fet_readmem(device_t dev_base, address_t addr, uint8_t *buffer,
		address_t count);

int fet_writemem(device_t dev_base, address_t addr,
		 const uint8_t *buffer, address_t count);

int fet_getregs(device_t dev_base, address_t *regs);

int fet_setregs(device_t dev_base, const address_t *regs);

#endif
