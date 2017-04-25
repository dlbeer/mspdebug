/* MSPDebug - debugging tool for the eZ430
 * Copyright (C) 2009-2012 Daniel Beer
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
#ifndef __Windows__
#include <usb.h>
#else
#include <lusb0_usb.h>
#endif

#include "rf2500.h"
#include "util.h"
#include "usbutil.h"
#include "output.h"

struct rf2500_transport {
	struct transport        base;

	int                     int_number;
	struct usb_dev_handle   *handle;

	uint8_t                 buf[64];
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
	printc_dbg("Trying to open interface %d on %s\n", ino, dev->filename);

	tr->int_number = ino;

	tr->handle = usb_open(dev);
	if (!tr->handle) {
		pr_error("rf2500: can't open device");
		return -1;
	}

#if defined(__linux__)
	if (usb_detach_kernel_driver_np(tr->handle, tr->int_number) < 0)
		pr_error("rf2500: warning: can't "
			"detach kernel driver");
#endif

#ifdef __Windows__
	if (usb_set_configuration(tr->handle, 1) < 0) {
		pr_error("rf2500: can't set configuration 1");
		usb_close(tr->handle);
		return -1;
	}
#endif

	if (usb_claim_interface(tr->handle, tr->int_number) < 0) {
		pr_error("rf2500: can't claim interface");
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

static int usbtr_send(transport_t tr_base, const uint8_t *data, int len)
{
	struct rf2500_transport *tr = (struct rf2500_transport *)tr_base;

	while (len) {
		uint8_t pbuf[256];
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
			(char *)pbuf, txlen, 10000) < 0) {
			pr_error("rf2500: can't send data");
			return -1;
		}

		data += plen;
		len -= plen;
	}

	return 0;
}

static int usbtr_recv(transport_t tr_base, uint8_t *databuf, int max_len)
{
	struct rf2500_transport *tr = (struct rf2500_transport *)tr_base;
	int rlen;

	if (tr->offset >= tr->len) {
		if (usb_bulk_read(tr->handle, USB_FET_IN_EP,
				(char *)tr->buf, sizeof(tr->buf),
				10000) < 0) {
			pr_error("rf2500: can't receive data");
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

static int usbtr_flush(transport_t tr_base)
{
	struct rf2500_transport *tr = (struct rf2500_transport *)tr_base;

#if !defined(__APPLE__) && !defined(__sun__)
	char buf[64];

	/* Flush out lingering data.
	 *
	 * The timeout apparently doesn't work on OS/X, and this loop
	 * just hangs once the endpoint buffer empties.
	 */
	while (usb_bulk_read(tr->handle, USB_FET_IN_EP,
			     buf, sizeof(buf),
			     100) > 0);
#endif

	tr->len = 0;
	tr->offset = 0;
	return 0;
}

static int usbtr_set_modem(transport_t tr_base, transport_modem_t state)
{
	printc_err("rf2500: unsupported operation: set_modem\n");
	return -1;
}

static const struct transport_class rf2500_transport = {
	.destroy	= usbtr_destroy,
	.send		= usbtr_send,
	.recv		= usbtr_recv,
	.flush		= usbtr_flush,
	.set_modem	= usbtr_set_modem
};

transport_t rf2500_open(const char *devpath, const char *requested_serial)
{
	struct rf2500_transport *tr = malloc(sizeof(*tr));
	struct usb_device *dev;

	if (!tr) {
		pr_error("rf2500: can't allocate memory");
		return NULL;
	}

	memset(tr, 0, sizeof(*tr));
	tr->base.ops = &rf2500_transport;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	if (devpath)
		dev = usbutil_find_by_loc(devpath);
	else
		dev = usbutil_find_by_id(USB_FET_VENDOR, USB_FET_PRODUCT,
					 requested_serial);

	if (!dev) {
		free(tr);
		return NULL;
	}

	if (open_device(tr, dev) < 0) {
		printc_err("rf2500: failed to open RF2500 device\n");
		free(tr);
		return NULL;
	}

	usbtr_flush(&tr->base);

	return (transport_t)tr;
}
