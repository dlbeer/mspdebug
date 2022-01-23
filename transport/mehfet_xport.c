/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2021 sys64738@disroot.org
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

#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#include "mehfet_xport.h"
#include "util.h"
#include "usbutil.h"
#include "output.h"

struct mehfet_transport {
	struct transport        base;
	struct usb_dev_handle   *handle;
	int epin, epout;
	int buf_size;
};


#define TIMEOUT_S               30
#define REQ_TIMEOUT_MS          100


static int open_device(struct mehfet_transport *tr, struct usb_device *dev)
{
#ifdef __linux__
	int driver;
	char drv_name[128];
#endif

	// first, find the right interface (and associated endpoints) of the USB device

	int config = 0, itf = 0;
	bool has = false;

	for (config = 0; config < dev->descriptor.bNumConfigurations; ++config) {
		struct usb_config_descriptor* cd = &dev->config[config];

		for (itf = 0; itf < cd->bNumInterfaces; ++itf) {
			struct usb_interface_descriptor* id = &cd->interface[itf].altsetting[0];

			if (id->bInterfaceClass == USB_CLASS_VENDOR_SPEC
					&& id->bInterfaceSubClass == '4'
					&& id->bInterfaceProtocol == '3') {
				// here I'd like to check for the "MehFET" substring in the
				// interface's iInterface string, but I can't really figure out
				// how to do that, so I'll just assume this is enough checking.

				if (id->bNumEndpoints != 2) continue; // this should be 2
				for (size_t i = 0; i < id->bNumEndpoints; ++i) {
					struct usb_endpoint_descriptor* ed = &id->endpoint[i];

					if ((ed->bmAttributes & USB_ENDPOINT_TYPE_MASK)
							!= USB_ENDPOINT_TYPE_BULK)
						break;

					if (ed->bEndpointAddress & USB_ENDPOINT_DIR_MASK)
						tr->epin = ed->bEndpointAddress; // input endpoint
					else
						tr->epout = ed->bEndpointAddress; // output endpoint
				}

				if (tr->epin != 0 && tr->epout != 0) {
					has = true;
					goto break_outer;
				}
			}
		}
	}
break_outer:

	if (!has) {
		printc_err("mehfet transport: USB device %s has no MehFET interface.\n",
				dev->filename);
		return -1;
	}

	printc_dbg("mehfet transport: trying to open %s\n", dev->filename);
	tr->handle = usb_open(dev);
	if (!tr->handle) {
		printc_err("mehfet transport: can't open device: %s\n",
			   usb_strerror());
		return -1;
	}

#ifdef __linux__
	driver = usb_get_driver_np(tr->handle, itf, drv_name, sizeof(drv_name));
	if (driver >= 0) {
		printc_dbg("Detaching kernel driver \"%s\"\n", drv_name);
		if (usb_detach_kernel_driver_np(tr->handle, itf) < 0)
			printc_err("warning: mehfet transport: can't detach "
				   "kernel driver: %s\n", usb_strerror());
	}
#endif

#ifdef __Windows__
	if (usb_set_configuration(tr->handle, config) < 0) {
		printc_err("mehfet transport: can't set configuration: %s\n",
			   usb_strerror());
		usb_close(tr->handle);
		return -1;
	}
#endif

	if (usb_claim_interface(tr->handle, itf) < 0) {
		printc_err("mehfet transport: can't claim interface: %s\n",
			   usb_strerror());
		usb_close(tr->handle);
		return -1;
	}

	return 0;
}

static void tr_destroy(transport_t tr_base)
{
	struct mehfet_transport *tr = (struct mehfet_transport *)tr_base;

	usb_close(tr->handle);
	free(tr);
}

static int tr_recv(transport_t tr_base, uint8_t *databuf, int max_len)
{
	struct mehfet_transport *tr = (struct mehfet_transport *)tr_base;
	time_t deadline = time(NULL) + TIMEOUT_S;
	char tmpbuf[tr->buf_size];

	if (max_len > tr->buf_size)
		max_len = tr->buf_size;

	while (time(NULL) < deadline) {
		int r = usb_bulk_read(tr->handle, tr->epin,
				      tmpbuf, max_len,
				      TIMEOUT_S * 1000);

		if (r <= 0) {
			printc_err("mehfet transport: usb_bulk_read: %s\n",
				   usb_strerror());
			return -1;
		}

		memcpy(databuf, tmpbuf, r);
#ifdef DEBUG_MEHFET_TRANSPORT
		printc_dbg("mehfet transport: tr_recv: flags = %02x %02x\n",
			   tmpbuf[0], tmpbuf[1]);
		debug_hexdump("mehfet transport: tr_recv", databuf, r);
#endif
		return r;
	}

	printc_err("mehfet transport: timed out while receiving data\n");
	return -1;
}

static int tr_send(transport_t tr_base, const uint8_t *databuf, int len)
{
	struct mehfet_transport *tr = (struct mehfet_transport *)tr_base;

#ifdef DEBUG_MEHFET
	debug_hexdump("mehfet transport: tr_send", databuf, len);
#endif
	while (len) {
		int r = usb_bulk_write(tr->handle, tr->epout,
				       (char *)databuf, len,
				       TIMEOUT_S * 1000);

		if (r <= 0) {
			printc_err("mehfet transport: usb_bulk_write: %s\n",
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
	(void)tr_base;
	return 0;
}

static int tr_set_modem(transport_t tr_base, transport_modem_t state)
{
	(void)tr_base; (void)state;
	return 0;
}

static const struct transport_class mehfet_class = {
	.destroy	= tr_destroy,
	.send		= tr_send,
	.recv		= tr_recv,
	.flush		= tr_flush,
	.set_modem	= tr_set_modem
};

transport_t mehfet_transport_open(const char *devpath,
		      const uint16_t* vendor, const uint16_t* product,
		      const char *requested_serial)
{
	struct mehfet_transport *tr = malloc(sizeof(*tr));
	struct usb_device *dev = NULL;

	if (!tr) {
		pr_error("mehfet transport: can't allocate memory");
		return NULL;
	}

	tr->base.ops = &mehfet_class;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	if (devpath)
		dev = usbutil_find_by_loc(devpath);
	else if (vendor && product)
		dev = usbutil_find_by_id(*vendor, *product, requested_serial);

	if (!dev) {
		printc_err("mehfet: no USB device found.%s\n",
			vendor ? "" : " (Did you forget to specify a VID:PID?)");
		free(tr);
		return NULL;
	}

	tr->buf_size = 64; // initial conservative value, will get updated later

	if (open_device(tr, dev) < 0) {
		printc_err("mehfet: failed to open device\n");
		free(tr);
		return NULL;
	}

	return &tr->base;
}


void mehfet_transport_set_buf_size(transport_t tr_base, int buf_size)
{
	struct mehfet_transport *tr = (struct mehfet_transport *)tr_base;

	tr->buf_size = buf_size;
}
int  mehfet_transport_get_buf_size(transport_t tr_base)
{
	struct mehfet_transport *tr = (struct mehfet_transport *)tr_base;

	return tr->buf_size;
}



int mehfet_send_raw(transport_t xport, uint8_t cmd, int datalen, const void* data) {
	if (datalen < 0) return -1;
	if (data && !datalen) return -1;
	if (!data && datalen) return -1;

	uint8_t buf[1+4+datalen];

	buf[0] = cmd;
	int i, len2;
	for (i = 0, len2 = datalen; i < 4 && len2; ++i, len2 >>= 7) {
		buf[0] |= 0x80; // command has payload data

		if (i == 3) {
			buf[i+1] = len2;
		} else {
			buf[i+1] = len2 & 0x7f;
			if (len2 >> 7) buf[i+1] |= 0x80;
		}
	}

	if (data && datalen) memcpy(&buf[i+1], data, datalen);

	return xport->ops->send(xport, buf, datalen + 1 + i);
}

int mehfet_recv_raw(transport_t xport, uint8_t* stat, int* datalen, void* data) {
	struct mehfet_transport *tr = (struct mehfet_transport *)xport;
	uint8_t rawbuf[tr->buf_size];

	int nmax = 5, ndata = 0, nfetch;
	if (datalen) {
		ndata = *datalen;
		nmax = ndata + 5;
	}
	nfetch = nmax;
	if (nmax > tr->buf_size) {
		nmax = tr->buf_size;
		/*printc_err("mehfet transport: bug: asking for more data (%d) than "
				"in the device buffer size (%d)\n", nmax, tr->buf_size);
		return -1;*/
	}

	int r = xport->ops->recv(xport, rawbuf, nfetch);
	if (r < 0) return r;
	if (r < 1) {
		printc_err("mehfet transport: no status byte received\n");
		return -1;
	}
	assert(r <= nfetch);

	uint8_t statv = rawbuf[0];

	uint32_t reallen = 0;
	uint8_t lastbyte = statv;
	int i;
	for (i = 0; i < 4 && (lastbyte & 0x80) /*&& i + 1 < r*/; ++i) {
		if (r < i + 2) {
			printc_err("mehfet transport: not enough lenght bytes received (%d)\n", r);
			return -1;
		}

		lastbyte = rawbuf[i + 1];

		uint8_t mask = (i == 3) ? 0xff : 0x7f;
		reallen |= (lastbyte & mask) << (i * 7);
	}

	if ((int)reallen > nmax && data) {
		printc_err("mehfet transport: too much data returned (%d vs %d)\n",
				(int)reallen, nmax);
		return -1;
	}

	int nrecvdata = r - 1 - i;
	assert(nrecvdata >= 0);

	if (nrecvdata && data) memcpy(data, &rawbuf[i+1], nrecvdata);
	// now we can use rawbuf for other purposes

	int off = r;
	int ntodo = (int)reallen - nrecvdata;
	while (ntodo > 0) { // more data bytes following in this logical packet
		int thisblock = tr->buf_size;
		if (thisblock > ntodo) thisblock = ntodo;

		r = xport->ops->recv(xport, rawbuf, thisblock);
		if (r < 0) return r;
		assert(r < thisblock);

		memcpy(&((char*)data)[off], rawbuf, thisblock);
		ntodo -= r;
		off += r;
	}

	if (stat) *stat = statv & 0x7f;
	if (datalen) *datalen = (int)reallen;

	return (int)reallen;
}

int mehfet_err_on_stat(const char* pre, uint8_t stat, int datalen, const void* data) {
	if (stat == mehfet_ok) return 0;

	const char* d = (const char*)data;
	switch (stat) {
	case mehfet_badargs:
		printc_err("mehfet: %s: %s\n", pre,
				datalen ? d : "bad argument sent to command");
		break;
	case mehfet_nocaps:
		printc_err("mehfet: %s: %s\n", pre,
				datalen ? d : "device doesn't have the command capability");
		break;
	case mehfet_badstate:
		printc_err("mehfet: %s: %s\n", pre,
				datalen ? d : "device in wrong state to execute command");
		break;
	case mehfet_invalidcmd:
		printc_err("mehfet: %s: %s\n", pre,
				datalen ? d : "invalid command");
		break;
	case mehfet_error:
		printc_err("mehfet: %s: %s\n", pre, datalen ? d : "unspecified error");
		break;
	default:
		printc_err("mehfet: %s: unknown error %hhu\n", pre, stat);
		break;
	}

	return -1;
}
