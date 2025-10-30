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

#include <libusb.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "usbutil.h"
#include "output.h"
#include "output_util.h"
#include "bslhid.h"

#define BSLHID_VID		0x2047
#define BSLHID_PID		0x0200

#define BSLHID_CLASS		LIBUSB_CLASS_HID

#define BSLHID_XFER_SIZE	64
#define BSLHID_MTU		(BSLHID_XFER_SIZE - 2)
#define BSLHID_HEADER		0x3F
#define BSLHID_TIMEOUT		5000

struct bslhid_transport {
	struct transport		base;

	int				cfg_number;
	int				int_number;

	libusb_device_handle		*handle;

	int				in_ep;
	int				out_ep;
};

static int find_interface_endpoints(struct bslhid_transport *tr, libusb_device *dev)
{
	int c, rv = -1;
	struct libusb_device_descriptor desc;

	if (libusb_get_device_descriptor(dev, &desc))
		return -1;

	for (c = 0; c < desc.bNumConfigurations && rv; c++) {
		struct libusb_config_descriptor *cfg;
		int i;

		if (libusb_get_config_descriptor(dev, c, &cfg))
			continue;

		for (i = 0; i < cfg->bNumInterfaces; i++) {
			const struct libusb_interface_descriptor *desc =
				&cfg->interface[i].altsetting[0];

			if (desc->bInterfaceClass == BSLHID_CLASS) {
				int j;

				tr->in_ep = -1;
				tr->out_ep = -1;

				for (j = 0; j < desc->bNumEndpoints; j++) {
					int addr = desc->endpoint[j].bEndpointAddress;

					if (addr & LIBUSB_ENDPOINT_DIR_MASK)
						tr->in_ep = addr;
					else
						tr->out_ep = addr;
				}

				if (tr->in_ep < 0 || tr->out_ep < 0) {
					printc_err("bslhid: can't find suitable endpoints\n");
				} else {
					tr->cfg_number = c;
					tr->int_number = i;
					rv = 0;
					break;
				}
			}
		}

		libusb_free_config_descriptor(cfg);
	}

	if (rv)
		printc_err("bslhid: can't find a matching interface\n");
	return rv;
}

static int open_device(struct bslhid_transport *tr, libusb_device *dev)
{
	printc_dbg("Opening interface...\n");

	if (find_interface_endpoints(tr, dev) < 0)
		return -1;

	printc_dbg("Interface %d Config %d Endpoints: IN: 0x%02x, OUT: 0x%02x\n",
		   tr->int_number, tr->cfg_number, tr->in_ep, tr->out_ep);

	if (libusb_open(dev, &tr->handle)) {
		pr_error("bslhid: can't open device");
		return -1;
	}

#ifdef __Windows__
	if (libusb_set_configuration(tr->handle, tr->cfg_number) < 0)
		pr_error("warning: bslhid: can't set configuration");
#endif

#ifdef __linux__
	if (libusb_kernel_driver_active(tr->handle, tr->int_number) > 0) {
		if (libusb_detach_kernel_driver(tr->handle,
						tr->int_number))
			pr_error("warning: bslhid: can't detach kernel driver");
	}
#endif

	if (libusb_claim_interface(tr->handle, tr->int_number) < 0) {
		pr_error("bslhid: can't claim interface");
		libusb_close(tr->handle);
		return -1;
	}

	return 0;
}

static void bslhid_destroy(transport_t base)
{
	struct bslhid_transport *tr = (struct bslhid_transport *)base;

	if (tr->handle) {
		libusb_release_interface(tr->handle, tr->int_number);
		libusb_close(tr->handle);
	}

	free(tr);
}

static int bslhid_flush(transport_t base)
{
#ifndef __APPLE__
	struct bslhid_transport *tr = (struct bslhid_transport *)base;
	uint8_t inbuf[BSLHID_XFER_SIZE];
	int rlen;

	if (!tr->handle)
		return 0;

	while (!libusb_bulk_transfer(tr->handle, tr->in_ep,
			     inbuf, sizeof(inbuf), &rlen, 100) && rlen > 0);
#endif

	return 0;
}

static int bslhid_send(transport_t base, const uint8_t *data, int len)
{
	struct bslhid_transport *tr = (struct bslhid_transport *)base;
	uint8_t outbuf[BSLHID_XFER_SIZE];
	int rv;

	if (!tr->handle) {
		printc_err("bslhid: send on suspended device\n");
		return -1;
	}

	memset(outbuf, 0xac, sizeof(outbuf));

	if (len > BSLHID_MTU) {
		printc_err("bslhid: send in excess of MTU: %d\n", len);
		return -1;
	}

	outbuf[0] = BSLHID_HEADER;
	outbuf[1] = len;
	memcpy(outbuf + 2, data, len);

#ifdef DEBUG_BSLHID
	debug_hexdump("bslhid_send", outbuf, sizeof(outbuf));
#endif

	if ((rv = libusb_bulk_transfer(tr->handle, tr->out_ep,
			   outbuf, sizeof(outbuf), NULL,
			   BSLHID_TIMEOUT) < 0)) {
		printc_err("bslhid: usb_bulk_write: %s\n", libusb_strerror(rv));
		return -1;
	}

	return 0;
}

static int bslhid_recv(transport_t base, uint8_t *data, int max_len)
{
	struct bslhid_transport *tr = (struct bslhid_transport *)base;
	uint8_t inbuf[BSLHID_XFER_SIZE];
	int r, rv;
	int len;

	if (!tr->handle) {
		printc_err("bslhid: recv on suspended device\n");
		return -1;
	}

	if ((rv = libusb_bulk_transfer(tr->handle, tr->in_ep,
			                 inbuf, sizeof(inbuf), &r, BSLHID_TIMEOUT))) {
		printc_err("bslhid_recv: usb_bulk_read: %s\n", libusb_strerror(rv));
		return -1;
	}

#ifdef DEBUG_BSLHID
	debug_hexdump("bslhid_recv", inbuf, r);
#endif

	if (r < 2) {
		printc_err("bslhid_recv: short transfer\n");
		return -1;
	}

	if (inbuf[0] != BSLHID_HEADER) {
		printc_err("bslhid_recv: missing transfer header\n");
		return -1;
	}

	len = inbuf[1];
	if ((len > max_len) || (len + 2 > r)) {
		printc_err("bslhid_recv: bad length: %d (%d byte transfer)\n",
			   len, r);
		return -1;
	}

	memcpy(data, inbuf + 2, len);
	return len;
}

static int bslhid_set_modem(transport_t base, transport_modem_t state)
{
	printc_err("bslhid: unsupported operation: set_modem\n");
	return -1;
}

static const struct transport_class bslhid_transport_class = {
	.destroy	= bslhid_destroy,
	.send		= bslhid_send,
	.recv		= bslhid_recv,
	.flush		= bslhid_flush,
	.set_modem	= bslhid_set_modem,
};

transport_t bslhid_open(const char *dev_path, const char *requested_serial)
{
	struct bslhid_transport *tr = malloc(sizeof(*tr));
	libusb_device *dev;

	if (!tr) {
		pr_error("bslhid: can't allocate memory");
		return NULL;
	}

	memset(tr, 0, sizeof(*tr));
	tr->base.ops = &bslhid_transport_class;

	libusb_init(NULL);

	if (dev_path)
		dev = usbutil_find_by_loc(dev_path);
	else
		dev = usbutil_find_by_id(BSLHID_VID, BSLHID_PID,
					 requested_serial);

	if (!dev) {
		free(tr);
		return NULL;
	}

	if (open_device(tr, dev) < 0) {
		libusb_unref_device(dev);
		printc_err("bslhid: failed to open BSL HID device\n");
		free(tr);
		return NULL;
	}

	libusb_unref_device(dev);
	bslhid_flush(&tr->base);
	return &tr->base;
}
