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

#include <stdio.h>
#include <string.h>
#include "usbutil.h"
#include "util.h"
#include "output.h"

static const char *device_help(const struct usb_device *dev)
{
	static const struct {
		int vendor;
		int product;
		const char *help;
	} info[] = {
		{0x0451, 0xf432, "eZ430-RF2500"},
		{0x15ba, 0x0002, "Olimex MSP-JTAG-TINY"}
	};
	int i;

	for (i = 0; i < ARRAY_LEN(info); i++)
		if (dev->descriptor.idProduct == info[i].product &&
		    dev->descriptor.idVendor == info[i].vendor)
			return info[i].help;

	return "";
}

void usbutil_list(void)
{
	const struct usb_bus *bus;

	for (bus = usb_get_busses(); bus; bus = bus->next) {
		const struct usb_device *dev;
		int busnum = atoi(bus->dirname);

		printc("Devices on bus %03d:\n", busnum);

		for (dev = bus->devices; dev; dev = dev->next) {
			int devnum = atoi(dev->filename);

			printc("    %03d:%03d %04x:%04x %s\n",
			       busnum, devnum,
			       dev->descriptor.idVendor,
			       dev->descriptor.idProduct,
			       device_help(dev));
		}
	}
}

struct usb_device *usbutil_find_by_id(int vendor, int product)
{
	struct usb_bus *bus;

	for (bus = usb_get_busses(); bus; bus = bus->next) {
		struct usb_device *dev;

		for (dev = bus->devices; dev; dev = dev->next)
			if (dev->descriptor.idVendor == vendor &&
			    dev->descriptor.idProduct == product)
				return dev;
	}

	printc_err("usbutil: unable to find a device matching "
		"%04x:%04x\n", vendor, product);

	return NULL;
}

struct usb_device *usbutil_find_by_loc(const char *loc)
{
	char buf[64];
	char *bus_text;
	char *dev_text;
	int target_bus;
	int target_dev;
	struct usb_bus *bus;

	strncpy(buf, loc, sizeof(buf));
	buf[sizeof(buf) - 1] = 0;

	bus_text = strtok(buf, ":\t\r\n");
	dev_text = strtok(NULL, ":\t\r\n");

	if (!(bus_text && dev_text)) {
		printc_err("usbutil: location must be specified as "
			"<bus>:<device>\n");
		return NULL;
	}

	target_bus = atoi(bus_text);
	target_dev = atoi(dev_text);

	for (bus = usb_get_busses(); bus; bus = bus->next) {
		struct usb_device *dev;
		int busnum = atoi(bus->dirname);

		if (busnum != target_bus)
			continue;

		for (dev = bus->devices; dev; dev = dev->next) {
			int devnum = atoi(dev->filename);

			if (devnum == target_dev)
				return dev;
		}
	}

	printc_err("usbutil: unable to find %03d:%03d\n",
		target_bus, target_dev);
	return NULL;
}
