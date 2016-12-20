/* MSPDebug - debugging tool for the eZ430
 * Copyright (C) 2009-2012 Daniel Beer
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

#ifndef __Windows__
#include <usb.h>
#else
#include <lusb0_usb.h>
#endif

#include <time.h>

#include "cp210x.h"
#include "util.h"
#include "usbutil.h"
#include "output.h"

struct cp210x_transport {
	struct transport        base;

	struct usb_dev_handle   *handle;
	int                     int_number;
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

#define CP210X_CLOCK			3500000

#define V1_INTERFACE_CLASS		255
#define V1_IN_EP			0x81
#define V1_OUT_EP			0x01

#define CP210x_REQTYPE_HOST_TO_DEVICE   0x41

#define CP210X_IFC_ENABLE               0x00
#define CP210X_SET_BAUDDIV              0x01
#define CP210X_SET_MHS                  0x07

/* CP210X_(SET_MHS|GET_MDMSTS) */
#define CP210X_DTR			0x0001
#define CP210X_RTS			0x0002
#define CP210X_CTS			0x0010
#define CP210X_DSR			0x0020
#define CP210X_RING			0x0040
#define CP210X_DCD			0x0080
#define CP210X_WRITE_DTR		0x0100
#define CP210X_WRITE_RTS		0x0200

#define TIMEOUT_S	                30

static int configure_port(struct cp210x_transport *tr, int baud_rate)
{
	int ret;

	ret = usb_control_msg(tr->handle, CP210x_REQTYPE_HOST_TO_DEVICE,
			      CP210X_IFC_ENABLE, 0x1, 0, NULL, 0, 300);
#ifdef DEBUG_CP210X
	printc("%s: %s: Sending control message "
		"CP210x_REQTYPE_HOST_TO_DEVICE, ret = %d\n",
	       __FILE__, __FUNCTION__, ret);
#endif
	if (ret < 0) {
		pr_error(__FILE__": can't enable CP210x UART");
		return -1;
	}

	/* Set the baud rate to 500000 bps */
	ret = usb_control_msg(tr->handle, CP210x_REQTYPE_HOST_TO_DEVICE,
			      CP210X_SET_BAUDDIV, CP210X_CLOCK / baud_rate,
			      0, NULL, 0, 300);
#ifdef DEBUG_CP210X
	printc("%s: %s: Sending control message "
		"CP210X_SET_BAUDDIV, ret = %d\n",
	       __FILE__, __FUNCTION__, ret);
#endif
	if (ret < 0) {
		pr_error(__FILE__": can't set baud rate");
		return -1;
	}

	/* Set the modem control settings.
	 * Clear RTS, DTR and WRITE_DTR, WRITE_RTS
	 */
	ret = usb_control_msg(tr->handle, CP210x_REQTYPE_HOST_TO_DEVICE,
			      CP210X_SET_MHS, 0x303, 0, NULL, 0, 300);
#ifdef DEBUG_CP210X
	printc("%s: %s: Sending control message "
		"CP210X_SET_MHS, ret %d\n",
	       __FILE__, __FUNCTION__, ret);
#endif
	if (ret < 0) {
		pr_error(__FILE__": can't set modem control");
		return -1;
	}

	return 0;
}

static int open_interface(struct cp210x_transport *tr,
			  struct usb_device *dev, int ino,
			  int baud_rate)
{
#if defined(__linux__)
	int drv;
	char drName[256];
#endif

	printc_dbg(__FILE__": Trying to open interface %d on %s\n",
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

#ifdef __Windows__
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

	if (configure_port(tr, baud_rate) < 0) {
		printc_err("Failed to configure for V1 device\n");
		usb_close(tr->handle);
		return -1;
	}

	return 0;
}

static int open_device(struct cp210x_transport *tr, struct usb_device *dev,
		       int baud_rate)
{
	struct usb_config_descriptor *c = &dev->config[0];
	int i;

	for (i = 0; i < c->bNumInterfaces; i++) {
		struct usb_interface *intf = &c->interface[i];
		struct usb_interface_descriptor *desc = &intf->altsetting[0];

		if (desc->bInterfaceClass == V1_INTERFACE_CLASS &&
		    !open_interface(tr, dev, desc->bInterfaceNumber,
				    baud_rate))
			return 0;
	}

	return -1;
}

static int usbtr_send(transport_t tr_base, const uint8_t *data, int len)
{
	struct cp210x_transport *tr = (struct cp210x_transport *)tr_base;
	int sent;

#ifdef DEBUG_CP210X
	debug_hexdump(__FILE__ ": USB transfer out", data, len);
#endif
	while (len) {
		sent = usb_bulk_write(tr->handle, V1_OUT_EP,
				      (char *)data, len, TIMEOUT_S * 1000);
		if (sent <= 0) {
			pr_error(__FILE__": can't send data");
			return -1;
		}

		data += sent;
		len -= sent;
	}

	return 0;
}

static int usbtr_recv(transport_t tr_base, uint8_t *databuf, int max_len)
{
	struct cp210x_transport *tr = (struct cp210x_transport *)tr_base;
	int rlen;
	time_t deadline = time(NULL) + TIMEOUT_S;

#ifdef DEBUG_CP210X
	printc(__FILE__": %s : read max %d\n", __FUNCTION__, max_len);
#endif

	while (time(NULL) < deadline) {
		rlen = usb_bulk_read(tr->handle, V1_IN_EP, (char *)databuf,
				     max_len, TIMEOUT_S * 1000);

#ifdef DEBUG_CP210X
		printc(__FILE__": %s : read %d\n", __FUNCTION__, rlen);
#endif

		if (rlen < 0) {
			pr_error(__FILE__": can't receive data");
			return -1;
		}

		if (rlen > 0) {
#ifdef DEBUG_CP210X
			debug_hexdump(__FILE__": USB transfer in", databuf, rlen);
#endif
			return rlen;
		}
	}

	pr_error(__FILE__": read operation timed out");
	return -1;
}

static void usbtr_destroy(transport_t tr_base)
{
	struct cp210x_transport *tr = (struct cp210x_transport *)tr_base;

	usb_release_interface(tr->handle, tr->int_number);
	usb_close(tr->handle);
	free(tr);
}

static int usbtr_flush(transport_t tr_base)
{
	struct cp210x_transport *tr = (struct cp210x_transport *)tr_base;
	char buf[64];

	/* Flush out lingering data */
	while (usb_bulk_read(tr->handle, V1_IN_EP,
			     buf, sizeof(buf),
			     100) > 0);

	return 0;
}

static int usbtr_set_modem(transport_t tr_base, transport_modem_t state)
{
	struct cp210x_transport *tr = (struct cp210x_transport *)tr_base;
	int value = CP210X_WRITE_DTR | CP210X_WRITE_RTS;

	/* DTR and RTS bits are active-low for this device */
	if (!(state & TRANSPORT_MODEM_DTR))
		value |= CP210X_DTR;
	if (!(state & TRANSPORT_MODEM_RTS))
		value |= CP210X_RTS;

	if (usb_control_msg(tr->handle, CP210x_REQTYPE_HOST_TO_DEVICE,
			    CP210X_SET_MHS, value, 0, NULL, 0, 300) < 0) {
		pr_error("cp210x: failed to set modem control lines\n");
		return -1;
	}

	return 0;
}

static const struct transport_class cp210x_class = {
	.destroy	= usbtr_destroy,
	.send		= usbtr_send,
	.recv		= usbtr_recv,
	.flush		= usbtr_flush,
	.set_modem	= usbtr_set_modem
};

transport_t cp210x_open(const char *devpath, const char *requested_serial,
			int baud_rate, uint16_t product, uint16_t vendor)
{
	struct cp210x_transport *tr = malloc(sizeof(*tr));
	struct usb_device *dev;

	if (!tr) {
		pr_error(__FILE__": can't allocate memory");
		return NULL;
	}

	tr->base.ops = &cp210x_class;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	if (devpath)
		dev = usbutil_find_by_loc(devpath);
	else
		dev = usbutil_find_by_id(product, vendor, requested_serial);

	if (!dev) {
		free(tr);
		return NULL;
	}

	if (open_device(tr, dev, baud_rate) < 0) {
		printc_err(__FILE__ ": failed to open CP210X device\n");
		free(tr);
		return NULL;
	}

	usbtr_flush(&tr->base);

	return (transport_t)tr;
}
