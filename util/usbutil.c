/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2013 Daniel Beer
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
#include <stdlib.h>
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
		{0x0451, 0xf430, "FET430UIF"},
		{0x2047, 0x0010, "FET430UIF (V3 firmware)"},
		{0x15ba, 0x0002, "Olimex MSP430-JTAG-TINY (v1)"},
		{0x15ba, 0x0008, "Olimex MSP430-JTAG-ISO"},
		{0x15ba, 0x0031, "Olimex MSP430-JTAG-TINY (v2)"},
		{0x15ba, 0x0100, "Olimex MSP430-JTAG-ISO-MK2 (v2)"},
		{0x2047, 0x0200, "USB bootstrap loader"}
	};
	int i;

	for (i = 0; i < ARRAY_LEN(info); i++)
		if (dev->descriptor.idProduct == info[i].product &&
		    dev->descriptor.idVendor == info[i].vendor)
			return info[i].help;

	return "";
}

static int read_serial(struct usb_device *dev, char *buf, int max_len)
{
	struct usb_dev_handle *dh = usb_open(dev);

	if (!dh)
		return -1;

	if (usb_get_string_simple(dh, dev->descriptor.iSerialNumber,
				  buf, max_len) < 0) {
		usb_close(dh);
		return -1;
	}

	usb_close(dh);
	return 0;
}

void usbutil_list(void)
{
	const struct usb_bus *bus;

	for (bus = usb_get_busses(); bus; bus = bus->next) {
		struct usb_device *dev;
		int busnum = atoi(bus->dirname);

		printc("Devices on bus %03d:\n", busnum);

		for (dev = bus->devices; dev; dev = dev->next) {
			int devnum = atoi(dev->filename);
			char serial[128];

			printc("    %03d:%03d %04x:%04x %s",
			       busnum, devnum,
			       dev->descriptor.idVendor,
			       dev->descriptor.idProduct,
			       device_help(dev));

			if (!read_serial(dev, serial, sizeof(serial)))
				printc(" [serial: %s]\n", serial);
			else
				printc("\n");
		}
	}
}

struct usb_device *usbutil_find_by_id(int vendor, int product,
				      const char *requested_serial)
{
	struct usb_bus *bus;

	for (bus = usb_get_busses(); bus; bus = bus->next) {
		struct usb_device *dev;

		for (dev = bus->devices; dev; dev = dev->next) {
			if (dev->descriptor.idVendor == vendor &&
			    dev->descriptor.idProduct == product) {
				char buf[128];

				if (!requested_serial ||
				    (!read_serial(dev, buf, sizeof(buf)) &&
				     !strcasecmp(requested_serial, buf)))
					return dev;
			}
		}
	}

	if(requested_serial)
		printc_err("usbutil: unable to find device matching "
			"%04x:%04x with serial %s\n", vendor, product,
			requested_serial);
	else
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
