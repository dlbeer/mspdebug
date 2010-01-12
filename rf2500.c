/* MSPDebug - debugging tool for the eZ430
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
#include <usb.h>

#include "transport.h"
#include "util.h"

/*********************************************************************
 * USB transport
 *
 * These functions handle the details of slicing data over USB
 * transfers. The interface presented is a continuous byte stream with
 * no slicing codes.
 *
 * Writes are unbuffered -- a single write translates to at least
 * one transfer.
 */

#define USB_FET_VENDOR			0x0451
#define USB_FET_PRODUCT			0xf432
#define USB_FET_INTERFACE_CLASS		3

#define USB_FET_IN_EP			0x81
#define USB_FET_OUT_EP			0x01

static int usbtr_int_number;
static struct usb_dev_handle *usbtr_handle;

static int usbtr_open_interface(struct usb_device *dev, int ino)
{
	printf("Trying to open interface %d on %s\n", ino, dev->filename);

	usbtr_int_number = ino;

	usbtr_handle = usb_open(dev);
	if (!usbtr_handle) {
		perror("rf2500: can't open device");
		return -1;
	}

	if (usb_detach_kernel_driver_np(usbtr_handle, usbtr_int_number) < 0)
		perror("rf2500: warning: can't "
			"detach kernel driver");

	if (usb_claim_interface(usbtr_handle, usbtr_int_number) < 0) {
		perror("rf2500: can't claim interface");
		usb_close(usbtr_handle);
		return -1;
	}

	return 0;
}

static int usbtr_open_device(struct usb_device *dev)
{
	struct usb_config_descriptor *c = &dev->config[0];
	int i;

	for (i = 0; i < c->bNumInterfaces; i++) {
		struct usb_interface *intf = &c->interface[i];
		struct usb_interface_descriptor *desc = &intf->altsetting[0];

		if (desc->bInterfaceClass == USB_FET_INTERFACE_CLASS &&
		    !usbtr_open_interface(dev, desc->bInterfaceNumber))
			return 0;
	}

	return -1;
}

static int usbtr_send(const u_int8_t *data, int len)
{
	while (len) {
		u_int8_t pbuf[256];
		int plen = len > 255 ? 255 : len;
		int txlen = plen + 1;

		memcpy(pbuf + 1, data, plen);

		/* This padding is needed to work around an apparent bug in
		 * the RF2500 FET. Without this, the device hangs.
		 */
		if (txlen > 32 && (txlen & 0x3f))
			while (txlen < 255 && (txlen & 0x3f))
				pbuf[txlen++] = 0xff;
		else if (txlen > 16 && (txlen & 0xf))
			while (txlen < 255 && (txlen & 0xf) != 1)
				pbuf[txlen++] = 0xff;
		pbuf[0] = txlen - 1;

#ifdef DEBUG_USBTR
		puts("USB transfer out:");
		hexdump(0, pbuf, txlen);
#endif
		if (usb_bulk_write(usbtr_handle, USB_FET_OUT_EP,
			(const char *)pbuf, txlen, 10000) < 0) {
			perror("rf2500: can't send data");
			return -1;
		}

		data += plen;
		len -= plen;
	}

	return 0;
}

static u_int8_t usbtr_buf[64];
static int usbtr_len;
static int usbtr_offset;

static int usbtr_flush(void)
{
	char buf[64];

	while (usb_bulk_read(usbtr_handle, USB_FET_IN_EP,
			buf, sizeof(buf), 100) >= 0);

	return 0;
}

static int usbtr_recv(u_int8_t *databuf, int max_len)
{
	int rlen;

	if (usbtr_offset >= usbtr_len) {
		if (usb_bulk_read(usbtr_handle, USB_FET_IN_EP,
				(char *)usbtr_buf, sizeof(usbtr_buf),
				10000) < 0) {
			perror("rf2500: can't receive data");
			return -1;
		}

#ifdef DEBUG_USBTR
		puts("USB transfer in:");
		hexdump(0, usbtr_buf, 64);
#endif

		usbtr_len = usbtr_buf[1] + 2;
		if (usbtr_len > sizeof(usbtr_buf))
			usbtr_len = sizeof(usbtr_buf);
		usbtr_offset = 2;
	}

	rlen = usbtr_len - usbtr_offset;
	if (rlen > max_len)
		rlen = max_len;
	memcpy(databuf, usbtr_buf + usbtr_offset, rlen);
	usbtr_offset += rlen;

	return rlen;
}

static void usbtr_close(void)
{
	usb_release_interface(usbtr_handle, usbtr_int_number);
	usb_close(usbtr_handle);
}

static const struct fet_transport usbtr_transport = {
	.flush = usbtr_flush,
	.send = usbtr_send,
	.recv = usbtr_recv,
	.close = usbtr_close
};

const struct fet_transport *rf2500_open(void)
{
	struct usb_bus *bus;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	for (bus = usb_get_busses(); bus; bus = bus->next) {
		struct usb_device *dev;

		for (dev = bus->devices; dev; dev = dev->next) {
			if (dev->descriptor.idVendor == USB_FET_VENDOR &&
			    dev->descriptor.idProduct == USB_FET_PRODUCT &&
			    !usbtr_open_device(dev)) {
				usbtr_flush();
				return &usbtr_transport;
			}
		}
	}

	fprintf(stderr, "rf2500: no devices could be found\n");
	return NULL;
}
