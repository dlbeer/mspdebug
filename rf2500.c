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

#include "rf2500.h"
#include "util.h"

struct rf2500_transport {
	struct transport        base;

	int                     int_number;
	struct usb_dev_handle   *handle;

	u_int8_t                buf[64];
	int                     len;
	int                     offset;
};

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

static int open_interface(struct rf2500_transport *tr,
			  struct usb_device *dev, int ino)
{
	printf("Trying to open interface %d on %s\n", ino, dev->filename);

	tr->int_number = ino;

	tr->handle = usb_open(dev);
	if (!tr->handle) {
		perror("rf2500: can't open device");
		return -1;
	}

#ifndef __APPLE__
	if (usb_detach_kernel_driver_np(tr->handle, tr->int_number) < 0)
		perror("rf2500: warning: can't "
			"detach kernel driver");
#endif

	if (usb_claim_interface(tr->handle, tr->int_number) < 0) {
		perror("rf2500: can't claim interface");
		usb_close(tr->handle);
		return -1;
	}

	return 0;
}

static int open_device(struct rf2500_transport *tr,
		       struct usb_device *dev)
{
	struct usb_config_descriptor *c = &dev->config[0];
	int i;

	for (i = 0; i < c->bNumInterfaces; i++) {
		struct usb_interface *intf = &c->interface[i];
		struct usb_interface_descriptor *desc = &intf->altsetting[0];

		if (desc->bInterfaceClass == USB_FET_INTERFACE_CLASS &&
		    !open_interface(tr, dev, desc->bInterfaceNumber))
			return 0;
	}

	return -1;
}

static int usbtr_send(transport_t tr_base, const u_int8_t *data, int len)
{
	struct rf2500_transport *tr = (struct rf2500_transport *)tr_base;

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
		debug_hexdump("USB transfer out", pbuf, txlen);
#endif
		if (usb_bulk_write(tr->handle, USB_FET_OUT_EP,
			(const char *)pbuf, txlen, 10000) < 0) {
			perror("rf2500: can't send data");
			return -1;
		}

		data += plen;
		len -= plen;
	}

	return 0;
}

static int usbtr_recv(transport_t tr_base, u_int8_t *databuf, int max_len)
{
	struct rf2500_transport *tr = (struct rf2500_transport *)tr_base;
	int rlen;

	if (tr->offset >= tr->len) {
		if (usb_bulk_read(tr->handle, USB_FET_IN_EP,
				(char *)tr->buf, sizeof(tr->buf),
				10000) < 0) {
			perror("rf2500: can't receive data");
			return -1;
		}

#ifdef DEBUG_USBTR
		debug_hexdump("USB transfer in", tr->buf, 64);
#endif

		tr->len = tr->buf[1] + 2;
		if (tr->len > sizeof(tr->buf))
			tr->len = sizeof(tr->buf);
		tr->offset = 2;
	}

	rlen = tr->len - tr->offset;
	if (rlen > max_len)
		rlen = max_len;
	memcpy(databuf, tr->buf + tr->offset, rlen);
	tr->offset += rlen;

	return rlen;
}

static void usbtr_destroy(transport_t tr_base)
{
	struct rf2500_transport *tr = (struct rf2500_transport *)tr_base;

	usb_release_interface(tr->handle, tr->int_number);
	usb_close(tr->handle);
	free(tr);
}

transport_t rf2500_open(void)
{
	struct rf2500_transport *tr = malloc(sizeof(*tr));
	struct usb_bus *bus;

	if (!tr) {
		perror("rf2500: can't allocate memory");
		return NULL;
	}

	tr->base.destroy = usbtr_destroy;
	tr->base.send = usbtr_send;
	tr->base.recv = usbtr_recv;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	for (bus = usb_get_busses(); bus; bus = bus->next) {
		struct usb_device *dev;

		for (dev = bus->devices; dev; dev = dev->next) {
			if (dev->descriptor.idVendor == USB_FET_VENDOR &&
			    dev->descriptor.idProduct == USB_FET_PRODUCT &&
			    !open_device(tr, dev)) {
				char buf[64];

				/* Flush out lingering data */
				while (usb_bulk_read(tr->handle, USB_FET_IN_EP,
						     buf, sizeof(buf),
						     100) >= 0);

				return (transport_t)tr;
			}
		}
	}

	fprintf(stderr, "rf2500: no devices could be found\n");
	free(tr);
	return NULL;
}
