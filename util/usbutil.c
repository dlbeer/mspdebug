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

static const char *device_help(const struct libusb_device_descriptor *desc)
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
		if (desc->idProduct == info[i].product &&
		    desc->idVendor == info[i].vendor)
			return info[i].help;

	return "";
}

static int read_serial(struct libusb_device *dev, const struct libusb_device_descriptor *desc, unsigned char *buf, int max_len)
{
	libusb_device_handle *dh;
	int rv;

	if (libusb_open(dev, &dh))
		return -1;

	rv = libusb_get_string_descriptor_ascii(dh, desc->iSerialNumber, buf, max_len);
	libusb_close(dh);
	return rv;
}

void usbutil_list(void)
{
	libusb_device **list;
	ssize_t cnt = libusb_get_device_list(NULL, &list);
	ssize_t i = 0;

	for (i = 0; i < cnt; i++) {
		libusb_device *dev = list[i];
		struct libusb_device_descriptor desc;

		printc("%03d:%03d",
			libusb_get_bus_number(dev), libusb_get_device_address(dev));

		if (!libusb_get_device_descriptor(dev, &desc)) {
			unsigned char serial[128];

			printc(" %04x:%04x %s",
		       desc.idVendor,
		       desc.idProduct,
		       device_help(&desc));

			if (!read_serial(dev, &desc, serial, sizeof(serial)))
				printc(" [serial: %s]\n", serial);
		}

		printc("\n");
	}

	libusb_free_device_list(list, 1);
}

libusb_device *usbutil_find_by_id(int vendor, int product,
				      const char *requested_serial)
{
	libusb_device **list;
	libusb_device *found = NULL;
	ssize_t cnt = libusb_get_device_list(NULL, &list);
	ssize_t i = 0;

	for (i = 0; i < cnt; i++) {
		libusb_device *dev = list[i];
		struct libusb_device_descriptor desc;

		if (!libusb_get_device_descriptor(dev, &desc)) {
			if (desc.idVendor == vendor && desc.idProduct == product) {
				unsigned char buf[128];

				if (!requested_serial ||
				    (!read_serial(dev, &desc, buf, sizeof(buf)) &&
				     !strcasecmp(requested_serial, (char *) buf))) {
					found = dev;
					libusb_ref_device(found);
					break;
				}
			}
		}
	}

	libusb_free_device_list(list, 1);

	if (!found) {
		if (requested_serial)
			printc_err("usbutil: unable to find device matching "
				"%04x:%04x with serial %s\n", vendor, product,
				requested_serial);
		else
			printc_err("usbutil: unable to find a device matching "
				"%04x:%04x\n", vendor, product);
	}

	return found;
}

libusb_device *usbutil_find_by_loc(const char *loc)
{
	char buf[64];
	char *bus_text;
	char *dev_text;
	int target_bus;
	int target_dev;
	libusb_device **list;
	libusb_device *found = NULL;
	ssize_t cnt, i = 0;

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

	cnt = libusb_get_device_list(NULL, &list);

	for (i = 0; i < cnt; i++) {
		libusb_device *dev = list[i];

		if (target_bus == libusb_get_bus_number(dev) &&
			target_dev == libusb_get_device_address(dev)) {
			found = dev;
			libusb_ref_device(found);
			break;
		}
	}

	libusb_free_device_list(list, 1);

	if (!found)
		printc_err("usbutil: unable to find %03d:%03d\n",
			target_bus, target_dev);

	return found;
}
