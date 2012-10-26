/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2012 Daniel Beer
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

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ftdi.h"
#include "util.h"
#include "usbutil.h"
#include "output.h"

struct ftdi_transport {
	struct transport        base;
	struct usb_dev_handle   *handle;
};

#define USB_INTERFACE           0
#define USB_CONFIG              1

#define EP_IN                   0x81
#define EP_OUT                  0x02

#define TIMEOUT_S               30
#define REQ_TIMEOUT_MS          100

#define REQTYPE_HOST_TO_DEVICE  0x40

#define FTDI_SIO_RESET			0 /* Reset the port */
#define FTDI_SIO_MODEM_CTRL		1 /* Set the modem control register */
#define FTDI_SIO_SET_FLOW_CTRL		2 /* Set flow control register */
#define FTDI_SIO_SET_BAUD_RATE		3 /* Set baud rate */
#define FTDI_SIO_SET_DATA		4 /* Set the data characteristics of
					     the port */
#define FTDI_SIO_GET_MODEM_STATUS	5 /* Retrieve current value of modem
					     status register */
#define FTDI_SIO_SET_EVENT_CHAR		6 /* Set the event character */
#define FTDI_SIO_SET_ERROR_CHAR		7 /* Set the error character */
#define FTDI_SIO_SET_LATENCY_TIMER	9 /* Set the latency timer */
#define FTDI_SIO_GET_LATENCY_TIMER	10 /* Get the latency timer */

#define FTDI_SIO_RESET_SIO              0
#define FTDI_SIO_RESET_PURGE_RX         1
#define FTDI_SIO_RESET_PURGE_TX         2

#define FTDI_PACKET_SIZE                64

#define FTDI_CLOCK			3000000

#define FTDI_DTR			0x0001
#define FTDI_RTS			0x0002
#define FTDI_WRITE_DTR			0x0100
#define FTDI_WRITE_RTS			0x0200

static int do_cfg(struct usb_dev_handle *handle, const char *what,
		  int request, int value)
{
	if (usb_control_msg(handle, REQTYPE_HOST_TO_DEVICE,
			    request, value, 0,
			    NULL, 0, REQ_TIMEOUT_MS)) {
		printc_err("ftdi: %s failed: %s\n", what, usb_strerror());
		return -1;
	}

	return 0;
}

int configure_ftdi(struct usb_dev_handle *h, int baud_rate)
{
	if (do_cfg(h, "reset FTDI",
		   FTDI_SIO_RESET, FTDI_SIO_RESET_SIO) < 0 ||
	    do_cfg(h, "set data characteristics",
		   FTDI_SIO_SET_DATA, 8) < 0 ||
	    do_cfg(h, "disable flow control",
		   FTDI_SIO_SET_FLOW_CTRL, 0) < 0 ||
	    do_cfg(h, "set modem control lines",
		   FTDI_SIO_MODEM_CTRL, 0x303) < 0 ||
	    do_cfg(h, "set baud rate",
		   FTDI_SIO_SET_BAUD_RATE, FTDI_CLOCK / baud_rate) < 0 ||
	    do_cfg(h, "set latency timer",
		   FTDI_SIO_SET_LATENCY_TIMER, 50) < 0 ||
	    do_cfg(h, "purge TX",
		   FTDI_SIO_RESET, FTDI_SIO_RESET_PURGE_TX) < 0 ||
	    do_cfg(h, "purge RX",
		   FTDI_SIO_RESET, FTDI_SIO_RESET_PURGE_RX) < 0)
		return -1;

	return 0;
}

static int open_device(struct ftdi_transport *tr, struct usb_device *dev,
		       int baud_rate)
{
#ifdef __linux__
	int driver;
	char drv_name[128];
#endif

	printc_dbg("ftdi: trying to open %s\n", dev->filename);
	tr->handle = usb_open(dev);
	if (!tr->handle) {
		printc_err("ftdi: can't open device: %s\n",
			   usb_strerror());
		return -1;
	}

#ifdef __linux__
	driver = usb_get_driver_np(tr->handle, USB_INTERFACE,
				   drv_name, sizeof(drv_name));
	if (driver >= 0) {
		printc_dbg("Detaching kernel driver \"%s\"\n", drv_name);
		if (usb_detach_kernel_driver_np(tr->handle, USB_INTERFACE) < 0)
			printc_err("warning: ftdi: can't detach "
				   "kernel driver: %s\n", usb_strerror());
	}
#endif

#ifdef __Windows__
	if (usb_set_configuration(tr->handle, USB_CONFIG) < 0) {
		printc_err("ftdi: can't set configuration: %s\n",
			   usb_strerror());
		usb_close(tr->handle);
		return -1;
	}
#endif

	if (usb_claim_interface(tr->handle, USB_INTERFACE) < 0) {
		printc_err("ftdi: can't claim interface: %s\n",
			   usb_strerror());
		usb_close(tr->handle);
		return -1;
	}

	if (configure_ftdi(tr->handle, baud_rate) < 0) {
		printc_err("ftdi: failed to configure device: %s\n",
			   usb_strerror());
		usb_close(tr->handle);
		return -1;
	}

	return 0;
}

static void tr_destroy(transport_t tr_base)
{
	struct ftdi_transport *tr = (struct ftdi_transport *)tr_base;

	usb_close(tr->handle);
	free(tr);
}

static int tr_recv(transport_t tr_base, uint8_t *databuf, int max_len)
{
	struct ftdi_transport *tr = (struct ftdi_transport *)tr_base;
	time_t deadline = time(NULL) + TIMEOUT_S;
	char tmpbuf[FTDI_PACKET_SIZE];

	if (max_len > FTDI_PACKET_SIZE - 2)
		max_len = FTDI_PACKET_SIZE - 2;

	while(time(NULL) < deadline) {
		int r = usb_bulk_read(tr->handle, EP_IN,
				      tmpbuf, max_len + 2,
				      TIMEOUT_S * 1000);

		if (r <= 0) {
			printc_err("ftdi: usb_bulk_read: %s\n",
				   usb_strerror());
			return -1;
		}

		if (r > 2) {
			memcpy(databuf, tmpbuf + 2, r - 2);
#ifdef DEBUG_OLIMEX_ISO
			printc_dbg("ftdi: tr_recv: flags = %02x %02x\n",
				   tmpbuf[0], tmpbuf[1]);
			debug_hexdump("ftdi: tr_recv", databuf, r - 2);
#endif
			return r - 2;
		}
	}

	printc_err("ftdi: timed out while receiving data\n");
	return -1;
}

static int tr_send(transport_t tr_base, const uint8_t *databuf, int len)
{
	struct ftdi_transport *tr = (struct ftdi_transport *)tr_base;

#ifdef DEBUG_OLIMEX_ISO
	debug_hexdump("ftdi: tr_send", databuf, len);
#endif
	while (len) {
		int r = usb_bulk_write(tr->handle, EP_OUT,
				       (char *)databuf, len,
				       TIMEOUT_S * 1000);

		if (r <= 0) {
			printc_err("ftdi: usb_bulk_write: %s\n",
				   usb_strerror());
			return -1;
		}

		databuf += r;
		len -= r;
	}

	return 0;
}

static int tr_flush(transport_t tr_base)
{
	struct ftdi_transport *tr = malloc(sizeof(*tr));

	return do_cfg(tr->handle, "purge RX",
		      FTDI_SIO_RESET, FTDI_SIO_RESET_PURGE_RX);
}

static int tr_set_modem(transport_t tr_base, transport_modem_t state)
{
	struct ftdi_transport *tr = malloc(sizeof(*tr));
	int value = FTDI_WRITE_DTR | FTDI_WRITE_RTS;

	/* DTR and RTS bits are active-low for this device */
	if (!(state & TRANSPORT_MODEM_DTR))
		value |= FTDI_DTR;
	if (!(state & TRANSPORT_MODEM_RTS))
		value |= FTDI_RTS;

	return do_cfg(tr->handle, "set modem control lines",
		      FTDI_SIO_MODEM_CTRL, value);
}

static const struct transport_class ftdi_class = {
	.destroy	= tr_destroy,
	.send		= tr_send,
	.recv		= tr_recv,
	.flush		= tr_flush,
	.set_modem	= tr_set_modem
};

transport_t ftdi_open(const char *devpath,
		      const char *requested_serial,
		      uint16_t vendor, uint16_t product,
		      int baud_rate)
{
	struct ftdi_transport *tr = malloc(sizeof(*tr));
	struct usb_device *dev;

	if (!tr) {
		pr_error("ftdi: can't allocate memory");
		return NULL;
	}

	tr->base.ops = &ftdi_class;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	if (devpath)
		dev = usbutil_find_by_loc(devpath);
	else
		dev = usbutil_find_by_id(vendor, product, requested_serial);

	if (!dev) {
		free(tr);
		return NULL;
	}

	if (open_device(tr, dev, baud_rate) < 0) {
		printc_err("ftdi: failed to open device\n");
		free(tr);
		return NULL;
	}

	return &tr->base;
}
