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
#include <hidapi.h>

#include "rf2500.h"
#include "util.h"
#include "usbutil.h"
#include "output.h"

#define USB_FET_VENDOR			0x0451
#define USB_FET_PRODUCT			0xf432
#define USB_FET_IN_EP			0x81
#define USB_FET_OUT_EP			0x01

struct rf2500_transport {
	struct transport        base;

	hid_device              *handle;

	uint8_t                 buf[64];
	int                     len;
	int                     offset;
};

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
		debug_hexdump("HIDUSB transfer out", pbuf, txlen);
#endif
		if (hid_write(tr->handle,
					  (const unsigned char *)pbuf, txlen) < 0) {
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
		if (hid_read_timeout(tr->handle, (unsigned char *)tr->buf,
				sizeof(tr->buf), 10000) < 0) {
			pr_error("rf2500: can't receive data");
			return -1;
		}

#ifdef DEBUG_USBTR
		debug_hexdump("HIDUSB transfer in", tr->buf, 64);
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

	hid_close(tr->handle);
	free(tr);
	hid_exit();
}

static int usbtr_flush(transport_t tr_base)
{
	struct rf2500_transport *tr = (struct rf2500_transport *)tr_base;

	unsigned char buf[64];

	/* Flush out lingering data.
	 *
	 * The timeout apparently doesn't work on OS/X, and this loop
	 * just hangs once the endpoint buffer empties.
	 */
	while (hid_read_timeout(tr->handle, buf, sizeof(buf), 100) > 0);

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

static const wchar_t * get_wc(const char *c)
{
    const size_t csize = strlen(c)+1;
    wchar_t* wc = malloc(sizeof(wchar_t)*csize);
    mbstowcs (wc, c, csize);

    return wc;
}

transport_t rf2500_open(const char *devpath, const char *requested_serial)
{
	struct rf2500_transport *tr = malloc(sizeof(*tr));
	hid_device *handle;

	if (!tr) {
		pr_error("rf2500: can't allocate memory");
		return NULL;
	}

	memset(tr, 0, sizeof(*tr));
	tr->base.ops = &rf2500_transport;

	hid_init();

	if (devpath) {
		handle = hid_open_path(devpath);
	}
	else {
		const wchar_t * wc_serial;
		if ( requested_serial ) {
			wc_serial = get_wc(requested_serial);
		} else {
			wc_serial = NULL;
		}
		handle = hid_open(USB_FET_VENDOR, USB_FET_PRODUCT, wc_serial);
		if ( wc_serial ) {
			free((wchar_t *)wc_serial);
		}
	}

	if (!handle) {
		printc_err("rf2500: failed to open RF2500 device\n");
		free(tr);
		hid_exit();
		return NULL;
	}

	tr->handle = handle;

	usbtr_flush(&tr->base);

	return (transport_t)tr;
}
