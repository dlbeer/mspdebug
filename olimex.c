/* MSPDebug - debugging tool for the eZ430
 * Copyright (C) 2009-2011 Daniel Beer
 * Copyright (C) 2010 Peter Jansen
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
#include <usb.h>

#include "olimex.h"
#include "util.h"
#include "usbutil.h"
#include "output.h"

struct olimex_transport {
	struct transport        base;

	int                     int_number;
	struct usb_dev_handle   *handle;

	int			in_ep;
	int			out_ep;

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

#define USB_FET_VENDOR			0x15ba

#define V1_PRODUCT			0x0002
#define V1_INTERFACE_CLASS		255
#define V1_IN_EP			0x81
#define V1_OUT_EP			0x01

#define V2_PRODUCT			0x0031
#define V2_INTERFACE_CLASS		10
#define V2_IN_EP			0x82
#define V2_OUT_EP			0x02

#define CP210x_REQTYPE_HOST_TO_DEVICE   0x41

#define CP210X_IFC_ENABLE               0x00
#define CP210X_SET_BAUDDIV              0x01
#define CP210X_SET_MHS                  0x07

#define TIMEOUT		                10000

static int open_interface(struct olimex_transport *tr,
			  struct usb_device *dev, int ino)
{
#if defined(__linux__)
	int drv;
	char drName[256];
#endif

	printc(__FILE__": Trying to open interface %d on %s\n",
	       ino, dev->filename);

	tr->int_number = ino;

	tr->handle = usb_open(dev);
	if (!tr->handle) {
		pr_error(__FILE__": can't open device");
		return -1;
	}

#if defined(__linux__)
	drv = usb_get_driver_np(tr->handle, tr->int_number, drName,
				sizeof(drName));
	printc(__FILE__" : driver %d\n", drv);
	if (drv >= 0) {
		if (usb_detach_kernel_driver_np(tr->handle,
						tr->int_number) < 0)
			pr_error(__FILE__": warning: can't detach "
			       "kernel driver");
	}
#endif

#ifdef WIN32
	if (usb_set_configuration(tr->handle, 1) < 0) {
		pr_error(__FILE__": can't set configuration 1");
		usb_close(tr->handle);
		return -1;
	}
#endif

	if (usb_claim_interface(tr->handle, tr->int_number) < 0) {
		pr_error(__FILE__": can't claim interface");
		usb_close(tr->handle);
		return -1;
	}

	int ret = usb_control_msg(tr->handle, CP210x_REQTYPE_HOST_TO_DEVICE,
				  CP210X_IFC_ENABLE, 0x1, 0, NULL, 0, 300);
#ifdef DEBUG_OLIMEX
	printc(__FILE__": %s : Sending control message ret %d\n",
	       __FUNCTION__, ret);
#endif
	/* Set the baud rate to 500000 bps */
	ret = usb_control_msg(tr->handle, CP210x_REQTYPE_HOST_TO_DEVICE,
			      CP210X_SET_BAUDDIV, 0x7, 0, NULL, 0, 300);
#ifdef DEBUG_OLIMEX
	printc(__FILE__": %s : Sending control message ret %d\n",
	       __FUNCTION__, ret);
#endif
	/* Set the modem control settings.
	 * Set RTS, DTR and WRITE_DTR, WRITE_RTS
	 */
	ret = usb_control_msg(tr->handle, CP210x_REQTYPE_HOST_TO_DEVICE,
			      CP210X_SET_MHS, 0x303, 0, NULL, 0, 300);
#ifdef DEBUG_OLIMEX
	printc(__FILE__": %s : Sending control message ret %d\n",
	       __FUNCTION__, ret);
#endif

	return 0;
}

static int open_device(struct olimex_transport *tr, struct usb_device *dev)
{
	struct usb_config_descriptor *c = &dev->config[0];
	int i;

	for (i = 0; i < c->bNumInterfaces; i++) {
		struct usb_interface *intf = &c->interface[i];
		struct usb_interface_descriptor *desc = &intf->altsetting[0];

		if (desc->bInterfaceClass == V1_INTERFACE_CLASS &&
		    !open_interface(tr, dev, desc->bInterfaceNumber)) {
			printc_dbg("olimex: rev 1 device\n");
			tr->in_ep = V1_IN_EP;
			tr->out_ep = V1_OUT_EP;
			return 0;
		} else if (desc->bInterfaceClass == V2_INTERFACE_CLASS &&
			   !open_interface(tr, dev, desc->bInterfaceNumber)) {
			printc_dbg("olimex: rev 2 device\n");
			tr->in_ep = V2_IN_EP;
			tr->out_ep = V2_OUT_EP;
			return 0;
		}
	}

	return -1;
}

static int usbtr_send(transport_t tr_base, const uint8_t *data, int len)
{
	struct olimex_transport *tr = (struct olimex_transport *)tr_base;

	int sent;

	while (len) {
#ifdef DEBUG_OLIMEX
		debug_hexdump(__FILE__": USB transfer out", data, len);
#endif
		sent = usb_bulk_write(tr->handle, tr->out_ep,
				      (char *)data, len, TIMEOUT);
		if (sent < 0) {
			pr_error(__FILE__": can't send data");
			return -1;
		}

		len -= sent;
	}

	return 0;
}

static int usbtr_recv(transport_t tr_base, uint8_t *databuf, int max_len)
{
	struct olimex_transport *tr = (struct olimex_transport *)tr_base;
	int rlen;

#ifdef DEBUG_OLIMEX
	printc(__FILE__": %s : read max %d\n", __FUNCTION__, max_len);
#endif

	rlen = usb_bulk_read(tr->handle, tr->in_ep, (char *)databuf,
			     max_len, TIMEOUT);

#ifdef DEBUG_OLIMEX
	printc(__FILE__": %s : read %d\n", __FUNCTION__, rlen);
#endif

	if (rlen < 0) {
		pr_error(__FILE__": can't receive data");
		return -1;
	}

#ifdef DEBUG_OLIMEX
	debug_hexdump(__FILE__": USB transfer in", databuf, rlen);
#endif

	return rlen;
}

static void usbtr_destroy(transport_t tr_base)
{
	struct olimex_transport *tr = (struct olimex_transport *)tr_base;

	usb_release_interface(tr->handle, tr->int_number);
	usb_close(tr->handle);
	free(tr);
}

transport_t olimex_open(const char *devpath, const char *requested_serial)
{
	struct olimex_transport *tr = malloc(sizeof(*tr));
	struct usb_device *dev;
	char buf[64];

	if (!tr) {
		pr_error(__FILE__": can't allocate memory");
		return NULL;
	}

	tr->base.destroy = usbtr_destroy;
	tr->base.send = usbtr_send;
	tr->base.recv = usbtr_recv;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	if (devpath) {
		dev = usbutil_find_by_loc(devpath);
	} else {
		dev = usbutil_find_by_id(USB_FET_VENDOR, V1_PRODUCT,
					 requested_serial);
		if (!dev)
			dev = usbutil_find_by_id(USB_FET_VENDOR, V2_PRODUCT,
						 requested_serial);
	}

	if (!dev) {
		free(tr);
		return NULL;
	}

	if (open_device(tr, dev) < 0) {
		printc_err(__FILE__ ": failed to open Olimex device\n");
		return NULL;
	}

	/* Flush out lingering data */
	while (usb_bulk_read(tr->handle, tr->in_ep,
			     buf, sizeof(buf),
			     100) > 0);

	return (transport_t)tr;
}
