/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2013 Daniel Beer
 * Copyright (C) 2016 Hiroki Mori
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

#include <usb.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <CoreFoundation/CoreFoundation.h>
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

struct bslosx_transport {
	struct transport		base;

	int				cfg_number;
	int				int_number;

	IOHIDDeviceRef			refDevice;

	char				bus_name[PATH_MAX + 1];
};

static void bslosx_destroy(transport_t base)
{
	struct bslosx_transport *tr = (struct bslosx_transport *)base;

	if (tr->refDevice) {
		IOHIDDeviceClose(tr->refDevice, kIOHIDOptionsTypeNone);
	}

	free(tr);
}

static int bslosx_flush(transport_t base)
{
	return 0;
}

static int bslosx_send(transport_t base, const uint8_t *data, int len)
{
	struct bslosx_transport *tr = (struct bslosx_transport *)base;
	uint8_t outbuf[BSLHID_XFER_SIZE];

	if (!tr->refDevice) {
		printc_err("bslosx: send on suspended device\n");
		return -1;
	}

	memset(outbuf, 0xac, sizeof(outbuf));

	if (len > BSLHID_MTU) {
		printc_err("bslosx: send in excess of MTU: %d\n", len);
		return -1;
	}

	outbuf[0] = BSLHID_HEADER;
	outbuf[1] = len;
	memcpy(outbuf + 2, data, len);

#ifdef DEBUG_BSLHID
	debug_hexdump("bslosx_send", outbuf, sizeof(outbuf));
#endif

	IOReturn ret = IOHIDDeviceSetReport(tr->refDevice, kIOHIDReportTypeOutput, 0, outbuf, BSLHID_XFER_SIZE);

	return 0;
}

static int g_readBytes;

static void reportCallback(void *inContext, IOReturn inResult, void *inSender,
	IOHIDReportType inType, uint32_t inReportID,
	uint8_t *inReport, CFIndex InReportLength)
{
	g_readBytes = InReportLength;
}

static int bslosx_recv(transport_t base, uint8_t *data, int max_len)
{
	struct bslosx_transport *tr = (struct bslosx_transport *)base;
	uint8_t inbuf[BSLHID_XFER_SIZE];
	int r;
	int len;

	if (!tr->refDevice) {
		printc_err("bslosx: recv on suspended device\n");
		return -1;
	}

	IOHIDDeviceRegisterInputReportCallback(tr->refDevice,
		&inbuf,
		BSLHID_XFER_SIZE,
		reportCallback,
		NULL);
	g_readBytes = -1;
	CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1, false);

	r = g_readBytes;
	if (r <= 0) {
		printc_err("bslosx_recv: usb_bulk_read no data\n");
		return -1;
	}

#ifdef DEBUG_BSLHID
	debug_hexdump("bslosx_recv", inbuf, r);
#endif

	if (r < 2) {
		printc_err("bslosx_recv: short transfer\n");
		return -1;
	}

	if (inbuf[0] != BSLHID_HEADER) {
		printc_err("bslosx_recv: missing transfer header\n");
		return -1;
	}

	len = inbuf[1];
	if ((len > max_len) || (len + 2 > r)) {
		printc_err("bslosx_recv: bad length: %d (%d byte transfer)\n",
			   len, r);
		return -1;
	}

	memcpy(data, inbuf + 2, len);
	return len;
}

static int bslosx_set_modem(transport_t base, transport_modem_t state)
{
	printc_err("bslosx: unsupported operation: set_modem\n");
	return -1;
}

static int bslosx_suspend(transport_t base)
{
	struct bslosx_transport *tr = (struct bslosx_transport *)base;

	if (tr->refDevice) {
		IOHIDDeviceClose(tr->refDevice, kIOHIDOptionsTypeNone);
		tr->refDevice = NULL;
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

static int bslosx_resume(transport_t base)
{
	return 0;
}

static const struct transport_class bslosx_transport_class = {
	.destroy	= bslosx_destroy,
	.send		= bslosx_send,
	.recv		= bslosx_recv,
	.flush		= bslosx_flush,
	.set_modem	= bslosx_set_modem,
	.suspend	= bslosx_suspend,
	.resume		= bslosx_resume
};

int getIntProperty(IOHIDDeviceRef inIOHIDDeviceRef, CFStringRef inKey) {
	int val;
	if (inIOHIDDeviceRef) {
		CFTypeRef tCFTypeRef = IOHIDDeviceGetProperty(inIOHIDDeviceRef, inKey);
		if (tCFTypeRef) {
			if (CFNumberGetTypeID() == CFGetTypeID(tCFTypeRef)) {
				if (!CFNumberGetValue( (CFNumberRef) tCFTypeRef, kCFNumberSInt32Type, &val)) {
val = -1;
				}
			}
		}
	}
	return val;
}

transport_t bslosx_open(const char *dev_path, const char *requested_serial)
{
	struct bslosx_transport *tr = malloc(sizeof(*tr));

	if (!tr) {
		pr_error("bslosx: can't allocate memory");
		return NULL;
	}

	memset(tr, 0, sizeof(*tr));

	IOHIDDeviceRef refDevice;
	IOHIDManagerRef refHidMgr = NULL;
	CFSetRef refDevSet = NULL;
	IOHIDDeviceRef *prefDevs = NULL;
	int i;
	int vid, pid;
	CFIndex numDevices;
	IOReturn ret;

	refHidMgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
	IOHIDManagerSetDeviceMatching(refHidMgr, NULL);
	IOHIDManagerScheduleWithRunLoop(refHidMgr, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
	IOHIDManagerOpen(refHidMgr, kIOHIDOptionsTypeNone);
	refDevSet = IOHIDManagerCopyDevices(refHidMgr);
	numDevices = CFSetGetCount(refDevSet);
	prefDevs = malloc(numDevices * sizeof(IOHIDDeviceRef));
	CFSetGetValues(refDevSet, (const void **)prefDevs);
	for (i = 0; i < numDevices; i++) {
		refDevice = prefDevs[i];
		vid = getIntProperty(refDevice, CFSTR(kIOHIDVendorIDKey)); 
		pid = getIntProperty(refDevice, CFSTR(kIOHIDProductIDKey));
		if (vid == BSLHID_VID && pid == BSLHID_PID) {
			break;
		}
	}

	if (!refDevice) {
		free(tr);
		return NULL;
	}

	ret = IOHIDDeviceOpen(refDevice, kIOHIDOptionsTypeNone);
	if (ret != kIOReturnSuccess) {
		printc_err("bslosx: failed to open BSL HID device\n");
		free(tr);
		return NULL;
	}
	tr->base.ops = &bslosx_transport_class;
	tr->refDevice = refDevice;
	strcpy(tr->bus_name, "macosxhid");

	bslosx_flush(&tr->base);
	return &tr->base;
}
