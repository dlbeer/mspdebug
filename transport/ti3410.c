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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libusb.h>
#include <time.h>
#include "ti3410.h"
#include "util.h"
#include "usbutil.h"
#include "output.h"
#include "ihex.h"

/************************************************************************
 * Definitions taken from drivers/usb/serial/ti_usb_3410_5052.h in the
 * Linux kernel (GPLv2+).
 */

/* Configuration ids */
#define TI_BOOT_CONFIG                  1
#define TI_ACTIVE_CONFIG                2

/* Pipe transfer mode and timeout */
#define TI_PIPE_MODE_CONTINOUS          0x01
#define TI_PIPE_MODE_MASK               0x03
#define TI_PIPE_TIMEOUT_MASK            0x7C
#define TI_PIPE_TIMEOUT_ENABLE          0x80

/* Module identifiers */
#define TI_I2C_PORT                     0x01
#define TI_IEEE1284_PORT                0x02
#define TI_UART1_PORT                   0x03
#define TI_UART2_PORT                   0x04
#define TI_RAM_PORT                     0x05

/* Purge modes */
#define TI_PURGE_OUTPUT                 0x00
#define TI_PURGE_INPUT                  0x80

/* Commands */
#define TI_GET_VERSION                  0x01
#define TI_GET_PORT_STATUS              0x02
#define TI_GET_PORT_DEV_INFO            0x03
#define TI_GET_CONFIG                   0x04
#define TI_SET_CONFIG                   0x05
#define TI_OPEN_PORT                    0x06
#define TI_CLOSE_PORT                   0x07
#define TI_START_PORT                   0x08
#define TI_STOP_PORT                    0x09
#define TI_TEST_PORT                    0x0A
#define TI_PURGE_PORT                   0x0B
#define TI_RESET_EXT_DEVICE             0x0C
#define TI_WRITE_DATA                   0x80
#define TI_READ_DATA                    0x81
#define TI_REQ_TYPE_CLASS               0x82

/* Bits per character */
#define TI_UART_5_DATA_BITS             0x00
#define TI_UART_6_DATA_BITS             0x01
#define TI_UART_7_DATA_BITS             0x02
#define TI_UART_8_DATA_BITS             0x03

/* Parity */
#define TI_UART_NO_PARITY               0x00
#define TI_UART_ODD_PARITY              0x01
#define TI_UART_EVEN_PARITY             0x02
#define TI_UART_MARK_PARITY             0x03
#define TI_UART_SPACE_PARITY            0x04

/* Stop bits */
#define TI_UART_1_STOP_BITS             0x00
#define TI_UART_1_5_STOP_BITS           0x01
#define TI_UART_2_STOP_BITS             0x02

/* Modem control */
#define TI_MCR_LOOP                     0x04
#define TI_MCR_DTR                      0x10
#define TI_MCR_RTS                      0x20

/* Read/Write data */
#define TI_RW_DATA_ADDR_SFR             0x10
#define TI_RW_DATA_ADDR_IDATA           0x20
#define TI_RW_DATA_ADDR_XDATA           0x30
#define TI_RW_DATA_ADDR_CODE            0x40
#define TI_RW_DATA_ADDR_GPIO            0x50
#define TI_RW_DATA_ADDR_I2C             0x60
#define TI_RW_DATA_ADDR_FLASH           0x70
#define TI_RW_DATA_ADDR_DSP             0x80

#define TI_RW_DATA_UNSPECIFIED          0x00
#define TI_RW_DATA_BYTE                 0x01
#define TI_RW_DATA_WORD                 0x02
#define TI_RW_DATA_DOUBLE_WORD          0x04

#define TI_TRANSFER_TIMEOUT		2
#define TI_FIRMWARE_BUF_SIZE		16284
#define TI_DOWNLOAD_MAX_PACKET_SIZE     64

/************************************************************************/

struct ti3410_transport {
	struct transport        base;
	libusb_device_handle	*hnd;
};

#define USB_FET_VENDOR			0x0451
#define USB_FET_PRODUCT			0xf430

#define USB_FET_INTERFACE		0
#define USB_FET_IN_EP			0x81
#define USB_FET_OUT_EP			0x01
#define USB_FET_INT_EP			0x83

#define USB_FDL_INTERFACE		0
#define USB_FDL_OUT_EP			0x01

#define TIMEOUT				1000
#define READ_TIMEOUT			5000

static int open_device(struct ti3410_transport *tr,
		       libusb_device *dev)
{
	libusb_device_handle *hnd;
	struct libusb_config_descriptor *c;
#if defined(__linux__)
	int drv;
#endif
	int config;

	if (libusb_open(dev, &hnd)) {
		pr_error("ti3410: failed to open USB device");
		return -1;
	}

#if defined(__linux__)
	drv = libusb_kernel_driver_active(hnd, USB_FET_INTERFACE);
	printc(__FILE__" : driver %d\n", drv);
	if (drv > 0) {
		if (libusb_detach_kernel_driver(hnd, USB_FET_INTERFACE))
			pr_error("ti3410: warning: can't "
					 "detach kernel driver");
	}
#endif

	/* This device has two configurations -- we need the one which
	 * has two bulk endpoints and a control.
	 */
	if (!libusb_get_active_config_descriptor(dev, &c)) {
		config = c->bConfigurationValue;
		libusb_free_config_descriptor(c);
	} else {
		config = TI_BOOT_CONFIG;
	}

	if (config == TI_BOOT_CONFIG) {
		printc_dbg("TI3410 device is in boot config, "
			   "setting active\n");

		if (libusb_set_configuration(hnd, TI_ACTIVE_CONFIG) < 0) {
			pr_error("ti3410: failed to set active config");
			libusb_close(hnd);
			return -1;
		}
	}

	if (libusb_claim_interface(hnd, USB_FET_INTERFACE) < 0) {
		pr_error("ti3410: can't claim interface");
		libusb_close(hnd);
		return -1;
	}

	tr->hnd = hnd;
	return 0;
}

static int set_termios(struct ti3410_transport *tr)
{
	static const uint8_t tios_data[10] = {
		0x00, 0x02, /* 460800 bps */
		0x60, 0x00, /* flags = ENABLE_MS_INTS | AUTO_START_DMA */
		TI_UART_8_DATA_BITS,
		TI_UART_NO_PARITY,
		TI_UART_1_STOP_BITS,
		0x00, /* cXon */
		0x00, /* cXoff */
		0x00  /* UART mode = RS232 */
	};

	if (libusb_control_transfer(tr->hnd,
	    LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
	    TI_SET_CONFIG,
	    0,
	    TI_UART1_PORT, (unsigned char *) tios_data, sizeof(tios_data), TIMEOUT) < 0) {
		pr_error("ti3410: TI_SET_CONFIG failed");
		return -1;
	}

	return 0;
}

static int set_mcr(struct ti3410_transport *tr)
{
	static const uint8_t wb_data[9] = {
		TI_RW_DATA_ADDR_XDATA,
		TI_RW_DATA_BYTE,
		1, /* byte count */
		0x00, 0x00, 0xff, 0xa4, /* base address */
		TI_MCR_LOOP | TI_MCR_RTS | TI_MCR_DTR, /* mask */
		TI_MCR_RTS | TI_MCR_DTR /* data */
	};

	if (libusb_control_transfer(tr->hnd,
	    LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
	    TI_WRITE_DATA,
	    0,
	    TI_RAM_PORT, (unsigned char *) wb_data, sizeof(wb_data), TIMEOUT) < 0) {
		pr_error("ti3410: TI_SET_CONFIG failed");
		return -1;
	}

	return 0;
}

static int do_open_start(struct ti3410_transport *tr)
{
	if (set_termios(tr) < 0)
		return -1;

	if (set_mcr(tr) < 0)
		return -1;

	if (libusb_control_transfer(tr->hnd,
	    LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
	    TI_OPEN_PORT,
	    TI_PIPE_MODE_CONTINOUS | TI_PIPE_TIMEOUT_ENABLE |
	    (TI_TRANSFER_TIMEOUT << 2),
	    TI_UART1_PORT, NULL, 0, TIMEOUT) < 0) {
		pr_error("ti3410: TI_OPEN_PORT failed");
		return -1;
	}

	if (libusb_control_transfer(tr->hnd,
	    LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
	    TI_START_PORT,
	    0,
	    TI_UART1_PORT, NULL, 0, TIMEOUT) < 0) {
		pr_error("ti3410: TI_START_PORT failed");
		return -1;
	}

	return 0;
}

static int interrupt_flush(struct ti3410_transport *tr)
{
	uint8_t buf[2];
	int rlen;

	return libusb_interrupt_transfer(tr->hnd, USB_FET_INT_EP,
			          buf, 2, &rlen, TIMEOUT) ? 0 : rlen;
}

static int setup_port(struct ti3410_transport *tr)
{
	interrupt_flush(tr);

	if (do_open_start(tr) < 0)
		return -1;

	if (libusb_control_transfer(tr->hnd,
	    LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
	    TI_PURGE_PORT,
	    TI_PURGE_INPUT,
	    TI_UART1_PORT, NULL, 0, TIMEOUT) < 0) {
		pr_error("ti3410: TI_PURGE_PORT (input) failed");
		return -1;
	}

	interrupt_flush(tr);
	interrupt_flush(tr);

	if (libusb_control_transfer(tr->hnd,
	    LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
	    TI_PURGE_PORT,
	    TI_PURGE_OUTPUT,
	    TI_UART1_PORT, NULL, 0, TIMEOUT) < 0) {
		pr_error("ti3410: TI_PURGE_PORT (output) failed");
		return -1;
	}

	interrupt_flush(tr);

	if (libusb_clear_halt(tr->hnd, USB_FET_IN_EP) < 0 ||
	    libusb_clear_halt(tr->hnd, USB_FET_OUT_EP) < 0) {
		pr_error("ti3410: failed to clear halt status");
		return -1;
	}

	if (do_open_start(tr) < 0)
		return -1;

	return 0;
}

static void teardown_port(struct ti3410_transport *tr)
{
	if (libusb_control_transfer(tr->hnd,
	    LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
	    TI_CLOSE_PORT,
	    0,
	    TI_UART1_PORT, NULL, 0, TIMEOUT) < 0)
		pr_error("ti3410: warning: TI_CLOSE_PORT failed");
}

static int ti3410_send(transport_t tr_base, const uint8_t *data, int len)
{
	struct ti3410_transport *tr = (struct ti3410_transport *)tr_base;
	int sent;

	while (len) {
		if (libusb_bulk_transfer(tr->hnd, USB_FET_OUT_EP,
								 (unsigned char *) data, len, &sent, TIMEOUT)) {
			pr_error("ti3410: can't send data");
			return -1;
		}

		data += sent;
		len -= sent;
	}

	return 0;
}

static int ti3410_recv(transport_t tr_base, uint8_t *databuf, int max_len)
{
	struct ti3410_transport *tr = (struct ti3410_transport *)tr_base;
	time_t deadline = time(NULL) + READ_TIMEOUT / 1000;

	while (time(NULL) < deadline) {
		int rlen, rv;

		if ((rv = libusb_bulk_transfer(tr->hnd, USB_FET_IN_EP, databuf,
				     max_len, &rlen, READ_TIMEOUT))) {
			printc_err("ti3410: usb_bulk_read: %s\n",
				   libusb_strerror(rv));
			return -1;
		}

		if (rlen > 0)
			return rlen;
	}

	printc_err("ti3410: read timeout\n");
	return -1;
}

static void ti3410_destroy(transport_t tr_base)
{
	struct ti3410_transport *tr = (struct ti3410_transport *)tr_base;

	teardown_port(tr);
	free(tr);
}

struct firmware {
	uint8_t		buf[TI_FIRMWARE_BUF_SIZE];
	unsigned int	size;
};

static FILE *find_firmware(void)
{
	char path[256];
	const char *env;
	FILE *in;

	printc_dbg("Searching for firmware for TI3410...\n");

	env = getenv("MSPDEBUG_TI3410_FW");
	if (env) {
		snprintf(path, sizeof(path), "%s", env);
		printc_dbg("    - checking %s\n", path);
		in = fopen(path, "r");
		if (in)
			return in;
	}

	snprintf(path, sizeof(path), "%s",
		 LIB_DIR "/mspdebug/ti_3410.fw.ihex");
	printc_dbg("    - checking %s\n", path);
	in = fopen(path, "r");
	if (in)
		return in;

	snprintf(path, sizeof(path), "%s", "ti_3410.fw.ihex");
	printc_dbg("    - checking %s\n", path);
	in = fopen(path, "r");
	if (in)
		return in;

	printc_err("ti3410: unable to locate firmware\n");
	return NULL;
}

static int do_extract(void *user_data, const struct binfile_chunk *ch)
{
	struct firmware *f = (struct firmware *)user_data;

	if (f->size != ch->addr) {
		printc_err("ti3410: firmware gap at 0x%x (ends at 0x%0x)\n",
			   f->size, ch->addr);
		return -1;
	}

	if (f->size + ch->len > sizeof(f->buf)) {
		printc_err("ti3410: maximum firmware size exceeded\n");
		return -1;
	}

	memcpy(f->buf + f->size, ch->data, ch->len);
	f->size += ch->len;
	return 0;
}

static int load_firmware(struct firmware *f)
{
	FILE *in = find_firmware();

	if (!in)
		return -1;

	if (!ihex_check(in)) {
		printc_err("ti3410: not a valid IHEX file\n");
		fclose(in);
		return -1;
	}

	memset(f, 0, sizeof(*f));
	if (ihex_extract(in, do_extract, f) < 0) {
		printc_err("ti3410: failed to load firmware\n");
		fclose(in);
		return -1;
	}

	fclose(in);
	return 0;
}

static void prepare_firmware(struct firmware *f)
{
	uint8_t cksum = 0;
	uint16_t real_size = f->size - 3;
	int i;

	for (i = 3; i < f->size; i++)
		cksum += f->buf[i];

	f->buf[0] = real_size & 0xff;
	f->buf[1] = real_size >> 8;
	f->buf[2] = cksum;

	printc_dbg("Loaded %d byte firmware image (checksum = 0x%02x)\n",
		   f->size, cksum);
}

static int do_download(libusb_device *dev, const struct firmware *f)
{
	libusb_device_handle *hnd;
	int offset = 0;

	printc_dbg("Starting download...\n");

	if (libusb_open(dev, &hnd)) {
		pr_error("ti3410: failed to open USB device");
		return -1;
	}

#if defined(__linux__)
	if (libusb_kernel_driver_active(hnd, USB_FET_INTERFACE) > 0) {
		if (libusb_detach_kernel_driver(hnd, USB_FET_INTERFACE))
			pr_error("ti3410: warning: can't "
					 "detach kernel driver");
	}
#endif

	if (libusb_claim_interface(hnd, USB_FDL_INTERFACE) < 0) {
		pr_error("ti3410: can't claim interface");
		libusb_close(hnd);
		return -1;
	}

	while (offset < f->size) {
		int plen = f->size - offset;
		int r;

		if (plen > TI_DOWNLOAD_MAX_PACKET_SIZE)
			plen = TI_DOWNLOAD_MAX_PACKET_SIZE;

		if (libusb_bulk_transfer(hnd, USB_FDL_OUT_EP, (unsigned char *) f->buf + offset, plen, &r, TIMEOUT)) {
			pr_error("ti3410: bulk write failed");
			libusb_close(hnd);
			return -1;
		}

		offset += r;
	}

	delay_ms(100);
	if (libusb_reset_device(hnd) < 0)
		pr_error("ti3410: warning: reset failed");

	libusb_close(hnd);
	return 0;
}

static int download_firmware(libusb_device *dev)
{
	struct firmware frm;

	if (load_firmware(&frm) < 0)
		return -1;

	prepare_firmware(&frm);

	if (do_download(dev, &frm) < 0)
		return -1;

	printc_dbg("Waiting for TI3410 reset...\n");
	delay_s(2);

	return 0;
}

static int ti3410_flush(transport_t tr_base)
{
	return 0;
}

static int ti3410_set_modem(transport_t tr_base, transport_modem_t state)
{
	printc_err("ti3410: unsupported operation: set_modem\n");
	return -1;
}

static const struct transport_class ti3410_transport = {
	.destroy	= ti3410_destroy,
	.send		= ti3410_send,
	.recv		= ti3410_recv,
	.flush		= ti3410_flush,
	.set_modem	= ti3410_set_modem
};

transport_t ti3410_open(const char *devpath, const char *requested_serial,
		int has_vid_pid, uint16_t vid, uint16_t pid)
{
	struct ti3410_transport *tr = malloc(sizeof(*tr));
	libusb_device *dev;
	struct libusb_device_descriptor desc;

	if (!tr) {
		pr_error("ti3410: can't allocate memory");
		return NULL;
	}

	tr->base.ops = &ti3410_transport;

	libusb_init(NULL);

	if (devpath)
		dev = usbutil_find_by_loc(devpath);
	else
		dev = usbutil_find_by_id(has_vid_pid ? vid : USB_FET_VENDOR,
								 has_vid_pid ? pid : USB_FET_PRODUCT,
								 requested_serial);

	if (!dev) {
		free(tr);
		return NULL;
	}

	if (libusb_get_device_descriptor(dev, &desc))
		return NULL;

	if (desc.bNumConfigurations == 1) {
		if (download_firmware(dev) < 0) {
			printc_err("ti3410: firmware download failed\n");
			free(tr);
			return NULL;
		}
		libusb_unref_device(dev);

		if (devpath)
			dev = usbutil_find_by_loc(devpath);
		else
			dev = usbutil_find_by_id(USB_FET_VENDOR,
						 USB_FET_PRODUCT,
						 requested_serial);

		if (!dev) {
			free(tr);
			return NULL;
		}
	}

	if (open_device(tr, dev) < 0) {
		printc_err("ti3410: failed to open TI3410 device\n");
		free(tr);
		return NULL;
	}

	if (setup_port(tr) < 0) {
		printc_err("ti3410: failed to set up port\n");
		teardown_port(tr);
		libusb_close(tr->hnd);
		free(tr);
		return NULL;
	}

	return (transport_t)tr;
}
