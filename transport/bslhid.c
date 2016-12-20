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

#ifndef __Windows__
#include <usb.h>
#else
#include <lusb0_usb.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "usbutil.h"
#include "output.h"
#include "output_util.h"
#include "bslhid.h"

#define BSLHID_VID		0x2047
#define BSLHID_PID		0x0200

#define BSLHID_CLASS		USB_CLASS_HID

#define BSLHID_XFER_SIZE	64
#define BSLHID_MTU		(BSLHID_XFER_SIZE - 2)
#define BSLHID_HEADER		0x3F
#define BSLHID_TIMEOUT		5000

struct bslhid_transport {
	struct transport		base;

	int				cfg_number;
	int				int_number;

	struct usb_dev_handle		*handle;

	int				in_ep;
	int				out_ep;

	char				bus_name[PATH_MAX + 1];
};

static int find_interface(struct bslhid_transport *tr,
			  const struct usb_device *dev)
{
	int c;

	for (c = 0; c < dev->descriptor.bNumConfigurations; c++) {
		const struct usb_config_descriptor *cfg = &dev->config[c];
		int i;

		for (i = 0; i < cfg->bNumInterfaces; i++) {
			const struct usb_interface *intf = &cfg->interface[i];
			const struct usb_interface_descriptor *desc =
				&intf->altsetting[0];

			if (desc->bInterfaceClass == BSLHID_CLASS) {
				tr->cfg_number = c;
				tr->int_number = i;
				return 0;
			}
		}
	}

	printc_err("bslhid: can't find a matching interface\n");
	return -1;
}

static int find_endpoints(struct bslhid_transport *tr,
			  const struct usb_device *dev)
{
	const struct usb_interface_descriptor *desc =
		&dev->config[tr->cfg_number].interface[tr->int_number].
			altsetting[0];
	int i;

	tr->in_ep = -1;
	tr->out_ep = -1;

	for (i = 0; i < desc->bNumEndpoints; i++) {
		int addr = desc->endpoint[i].bEndpointAddress;

		if (addr & USB_ENDPOINT_DIR_MASK)
			tr->in_ep = addr;
		else
			tr->out_ep = addr;
	}

	if (tr->in_ep < 0 || tr->out_ep < 0) {
		printc_err("bslhid: can't find suitable endpoints\n");
		return -1;
	}

	return 0;
}

static int open_device(struct bslhid_transport *tr, struct usb_device *dev)
{
	if (find_interface(tr, dev) < 0)
		return -1;

	printc_dbg("Opening interface %d (config %d)...\n",
		   tr->int_number, tr->cfg_number);

	if (find_endpoints(tr, dev) < 0)
		return -1;

	printc_dbg("Found endpoints: IN: 0x%02x, OUT: 0x%02x\n",
		   tr->in_ep, tr->out_ep);

	tr->handle = usb_open(dev);
	if (!tr->handle) {
		pr_error("bslhid: can't open device");
		return -1;
	}

#ifdef __Windows__
	if (usb_set_configuration(tr->handle, tr->cfg_number) < 0)
		pr_error("warning: bslhid: can't set configuration");
#endif

#ifdef __linux__
	if (usb_detach_kernel_driver_np(tr->handle, tr->int_number) < 0)
		pr_error("warning: bslhid: can't detach kernel driver");
#endif

	if (usb_claim_interface(tr->handle, tr->int_number) < 0) {
		pr_error("bslhid: can't claim interface");
		usb_close(tr->handle);
		return -1;
	}

	/* Save the bus path for a future suspend/resume */
	strncpy(tr->bus_name, dev->bus->dirname, sizeof(tr->bus_name));
	tr->bus_name[sizeof(tr->bus_name) - 1] = 0;

	return 0;
}

static void bslhid_destroy(transport_t base)
{
	struct bslhid_transport *tr = (struct bslhid_transport *)base;

	if (tr->handle) {
		usb_release_interface(tr->handle, tr->int_number);
		usb_close(tr->handle);
	}

	free(tr);
}

static int bslhid_flush(transport_t base)
{
#ifndef __APPLE__
	struct bslhid_transport *tr = (struct bslhid_transport *)base;
	uint8_t inbuf[BSLHID_XFER_SIZE];

	if (!tr->handle)
		return 0;

	while (usb_bulk_read(tr->handle, tr->in_ep,
			     (char *)inbuf, sizeof(inbuf), 100) > 0);
#endif

	return 0;
}

static int bslhid_send(transport_t base, const uint8_t *data, int len)
{
	struct bslhid_transport *tr = (struct bslhid_transport *)base;
	uint8_t outbuf[BSLHID_XFER_SIZE];

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

	if (usb_bulk_write(tr->handle, tr->out_ep,
			   (char *)outbuf, sizeof(outbuf),
			   BSLHID_TIMEOUT) < 0) {
		printc_err("bslhid: usb_bulk_write: %s\n", usb_strerror());
		return -1;
	}

	return 0;
}

static int bslhid_recv(transport_t base, uint8_t *data, int max_len)
{
	struct bslhid_transport *tr = (struct bslhid_transport *)base;
	uint8_t inbuf[BSLHID_XFER_SIZE];
	int r;
	int len;

	if (!tr->handle) {
		printc_err("bslhid: recv on suspended device\n");
		return -1;
	}

	r = usb_bulk_read(tr->handle, tr->in_ep,
			  (char *)inbuf, sizeof(inbuf), BSLHID_TIMEOUT);
	if (r <= 0) {
		printc_err("bslhid_recv: usb_bulk_read: %s\n", usb_strerror());
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

static int bslhid_suspend(transport_t base)
{
	struct bslhid_transport *tr = (struct bslhid_transport *)base;

	if (tr->handle) {
		usb_release_interface(tr->handle, tr->int_number);
		usb_close(tr->handle);
		tr->handle = NULL;
	}

	return 0;
}

static struct usb_bus *find_by_name(const char *name)
{
	struct usb_bus *b;

	for (b = usb_get_busses(); b; b = b->next)
		if (!strcmp(name, b->dirname))
			return b;

	return NULL;
}

static struct usb_device *find_first_bsl(struct usb_bus *bus)
{
	struct usb_device *d;

	for (d = bus->devices; d; d = d->next)
		if ((d->descriptor.idVendor == BSLHID_VID) &&
		    (d->descriptor.idProduct == BSLHID_PID))
			return d;

	return NULL;
}

static int bslhid_resume(transport_t base)
{
	struct bslhid_transport *tr = (struct bslhid_transport *)base;
	struct usb_bus *bus;
	struct usb_device *dev;

	if (tr->handle)
		return 0;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	bus = find_by_name(tr->bus_name);
	if (!bus) {
		printc_err("bslhid: can't find bus to resume from\n");
		return -1;
	}

	/* We can't portably distinguish physical locations, so this
	 * will have to do.
	 */
	dev = find_first_bsl(bus);
	if (!dev) {
		printc_err("bslhid: can't find a BSL HID on this bus\n");
		return -1;
	}

	if (open_device(tr, dev) < 0) {
		printc_err("bslhid: failed to resume BSL HID device\n");
		return -1;
	}

	return 0;
}

static const struct transport_class bslhid_transport_class = {
	.destroy	= bslhid_destroy,
	.send		= bslhid_send,
	.recv		= bslhid_recv,
	.flush		= bslhid_flush,
	.set_modem	= bslhid_set_modem,
	.suspend	= bslhid_suspend,
	.resume		= bslhid_resume
};

transport_t bslhid_open(const char *dev_path, const char *requested_serial)
{
	struct bslhid_transport *tr = malloc(sizeof(*tr));
	struct usb_device *dev;

	if (!tr) {
		pr_error("bslhid: can't allocate memory");
		return NULL;
	}

	memset(tr, 0, sizeof(*tr));
	tr->base.ops = &bslhid_transport_class;

	usb_init();
	usb_find_busses();
	usb_find_devices();

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
		printc_err("bslhid: failed to open BSL HID device\n");
		free(tr);
		return NULL;
	}

	bslhid_flush(&tr->base);
	return &tr->base;
}
