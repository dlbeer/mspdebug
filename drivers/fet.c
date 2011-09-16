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
 *
 * Various constants and tables come from uif430, written by Robert
 * Kavaler (kavaler@diva.com). This is available under the same license
 * as this program, from www.relavak.com.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "util.h"
#include "fet.h"
#include "fet_error.h"
#include "fet_db.h"
#include "output.h"
#include "opdb.h"

#include "uif.h"
#include "olimex.h"
#include "rf2500.h"
#include "ti3410.h"

/* Send data in separate packets, as in the RF2500 */
#define FET_PROTO_SEPARATE_DATA		0x01

/* Received packets have an extra trailing byte */
#define FET_PROTO_EXTRA_RECV		0x02

/* Command packets have no leading \x7e */
#define FET_PROTO_NOLEAD_SEND		0x04

/* The new identify method should always be used */
#define FET_PROTO_IDENTIFY_NEW		0x08

#define MAX_PARAMS		16
#define MAX_BLOCK_SIZE		4096

struct fet_device {
	struct device                   base;

	transport_t                     transport;
	int				flags;
	int                             version;

	/* Device-specific information */
	address_t			code_start;

	uint8_t                         fet_buf[65538];
	int                             fet_len;

	/* Recieved packet is parsed into this struct */
	struct {
		int		command_code;
		int		state;

		int		argc;
		uint32_t	argv[MAX_PARAMS];

		uint8_t	*data;
		int		datalen;
	} fet_reply;
};

/**********************************************************************
 * FET command codes.
 *
 * These come from uif430 by Robert Kavaler (kavaler@diva.com).
 * www.relavak.com
 */

#define C_INITIALIZE            0x01
#define C_CLOSE                 0x02
#define C_IDENTIFY              0x03
#define C_DEVICE                0x04
#define C_CONFIGURE             0x05
#define C_VCC                   0x06
#define C_RESET                 0x07
#define C_READREGISTERS         0x08
#define C_WRITEREGISTERS        0x09
#define C_READREGISTER          0x0a
#define C_WRITEREGISTER         0x0b
#define C_ERASE                 0x0c
#define C_READMEMORY            0x0d
#define C_WRITEMEMORY           0x0e
#define C_FASTFLASHER           0x0f
#define C_BREAKPOINT            0x10
#define C_RUN                   0x11
#define C_STATE                 0x12
#define C_SECURE                0x13
#define C_VERIFYMEMORY          0x14
#define C_FASTVERIFYMEMORY      0x15
#define C_ERASECHECK            0x16
#define C_EEMOPEN               0x17
#define C_EEMREADREGISTER       0x18
#define C_EEMREADREGISTERTEST   0x19
#define C_EEMWRITEREGISTER      0x1a
#define C_EEMCLOSE              0x1b
#define C_ERRORNUMBER           0x1c
#define C_GETCURVCCT            0x1d
#define C_GETEXTVOLTAGE         0x1e
#define C_FETSELFTEST           0x1f
#define C_FETSETSIGNALS         0x20
#define C_FETRESET              0x21
#define C_READI2C               0x22
#define C_WRITEI2C              0x23
#define C_ENTERBOOTLOADER       0x24

#define C_IDENT1                0x28
#define C_IDENT2                0x29
#define C_IDENT3                0x2b

/* Constants for parameters of various FET commands */
#define FET_CONFIG_VERIFICATION 0
#define FET_CONFIG_EMULATION    1
#define FET_CONFIG_CLKCTRL      2
#define FET_CONFIG_MCLKCTRL     3
#define FET_CONFIG_FLASH_TESET  4
#define FET_CONFIG_FLASH_LOCK   5
#define FET_CONFIG_PROTOCOL     8

#define FET_RUN_FREE           1
#define FET_RUN_STEP           2
#define FET_RUN_BREAKPOINT     3

#define FET_RESET_PUC          0x01
#define FET_RESET_RST          0x02
#define FET_RESET_VCC          0x04
#define FET_RESET_ALL          0x07

#define FET_ERASE_SEGMENT      0
#define FET_ERASE_MAIN         1
#define FET_ERASE_ALL          2

#define FET_POLL_RUNNING        0x01
#define FET_POLL_BREAKPOINT     0x02

/*********************************************************************
 * Checksum calculation
 *
 * This code table is also derived from uif430.
 */

static const uint16_t fcstab[256] = {
        0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
        0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
        0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
        0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
        0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
        0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
        0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
        0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
        0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
        0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
        0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
        0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
        0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
        0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
        0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
        0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
        0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
        0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
        0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
        0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
        0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
        0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
        0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
        0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
        0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
        0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
        0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
        0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
        0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
        0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
        0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
        0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78
};

static uint16_t calc_checksum(uint8_t *cp, int len)
{
	uint16_t fcs = 0xffff;

        while (len--) {
                fcs = (fcs >> 8) ^ fcstab[(fcs ^ *cp++) & 0xff];
        }

        return fcs ^ 0xffff;
}

/*********************************************************************
 * FET packet transfer. This level of the interface deals in packets
 * send to/from the device.
 */

/* This is a type of data transfer which appears to be unique to
 * the RF2500. Blocks of data are sent to an internal buffer. Each
 * block is prefixed with a buffer offset and a payload length.
 *
 * No checksums are included.
 */
static int send_rf2500_data(struct fet_device *dev,
			    const uint8_t *data, int len)
{
	int offset = 0;

	while (len) {
		uint8_t pbuf[63];
		int plen = len > 59 ? 59 : len;

		pbuf[0] = 0x83;
		pbuf[1] = offset & 0xff;
		pbuf[2] = offset >> 8;
		pbuf[3] = plen;
		memcpy(pbuf + 4, data, plen);
		if (dev->transport->send(dev->transport, pbuf, plen + 4) < 0)
			return -1;

		data += plen;
		len -= plen;
		offset += plen;
	}

	return 0;
}

#define PTYPE_ACK		0
#define PTYPE_CMD		1
#define PTYPE_PARAM		2
#define PTYPE_DATA		3
#define PTYPE_MIXED		4
#define PTYPE_NAK		5
#define PTYPE_FLASH_ACK		6

static int parse_packet(struct fet_device *dev, int plen)
{
	uint16_t c = calc_checksum(dev->fet_buf + 2, plen - 2);
	uint16_t r = LE_WORD(dev->fet_buf, plen);
	int i = 2;
	int type;
	int error;

	if (c != r) {
		printc_err("fet: checksum error (calc %04x,"
			" recv %04x)\n", c, r);
		return -1;
	}

	if (plen < 6)
		goto too_short;

	dev->fet_reply.command_code = dev->fet_buf[i++];
	type = dev->fet_buf[i++];
	dev->fet_reply.state = dev->fet_buf[i++];
	error = dev->fet_buf[i++];

	if (error) {
		printc_err("fet: FET returned error code %d (%s)\n",
			error, fet_error(error));
		return -1;
	}

	if (type == PTYPE_NAK) {
		printc_err("fet: FET returned NAK\n");
		return -1;
	}

	/* Parse packet parameters */
	if (type == PTYPE_PARAM || type == PTYPE_MIXED) {
		int j;

		if (i + 2 > plen)
			goto too_short;

		dev->fet_reply.argc = LE_WORD(dev->fet_buf, i);
		i += 2;

		if (dev->fet_reply.argc >= MAX_PARAMS) {
			printc_err("fet: too many params: %d\n",
				dev->fet_reply.argc);
			return -1;
		}

		for (j = 0; j < dev->fet_reply.argc; j++) {
			if (i + 4 > plen)
				goto too_short;
			dev->fet_reply.argv[j] = LE_LONG(dev->fet_buf, i);
			i += 4;
		}
	} else {
		dev->fet_reply.argc = 0;
	}

	/* Extract a pointer to the data */
	if (type == PTYPE_DATA || type == PTYPE_MIXED) {
		if (i + 4 > plen)
			goto too_short;

		dev->fet_reply.datalen = LE_LONG(dev->fet_buf, i);
		i += 4;

		if (i + dev->fet_reply.datalen > plen)
			goto too_short;

		dev->fet_reply.data = dev->fet_buf + i;
	} else {
		dev->fet_reply.data = NULL;
		dev->fet_reply.datalen = 0;
	}

	return 0;

too_short:
	printc_err("fet: too short (%d bytes)\n",
		plen);
	return -1;
}

/* Receive a packet from the FET. The usual format is:
 *     <length (2 bytes)> <data> <checksum>
 *
 * The length is that of the data + checksum. Olimex JTAG adapters follow
 * all packets with a trailing 0x7e byte, which must be discarded.
 */
static int recv_packet(struct fet_device *dev)
{
	int pkt_extra = (dev->flags & FET_PROTO_EXTRA_RECV) ? 3 : 2;
	int plen = LE_WORD(dev->fet_buf, 0);

	/* If there's a packet still here from last time, get rid of it */
	if (dev->fet_len >= plen + pkt_extra) {
		memmove(dev->fet_buf, dev->fet_buf + plen + pkt_extra,
			dev->fet_len - plen - pkt_extra);
		dev->fet_len -= plen + pkt_extra;
	}

	/* Keep adding data to the buffer until we have a complete packet */
	for (;;) {
		int len;

		plen = LE_WORD(dev->fet_buf, 0);
		if (dev->fet_len >= plen + pkt_extra)
			return parse_packet(dev, plen);

		len = dev->transport->recv(dev->transport,
					   dev->fet_buf + dev->fet_len,
					   sizeof(dev->fet_buf) -
					   dev->fet_len);
		if (len < 0)
			return -1;
		dev->fet_len += len;
	}

	return -1;
}

static int send_command(struct fet_device *dev, int command_code,
		        const uint32_t *params, int nparams,
			const uint8_t *extra, int exlen)
{
	uint8_t datapkt[MAX_BLOCK_SIZE * 2];
	int len = 0;

	uint8_t buf[MAX_BLOCK_SIZE * 3];
	uint16_t cksum;
	int i = 0;
	int j;

	assert (len + exlen + 2 <= sizeof(datapkt));

	/* Command code and packet type */
	datapkt[len++] = command_code;
	datapkt[len++] = ((nparams > 0) ? 1 : 0) + ((exlen > 0) ? 2 : 0) + 1;

	/* Optional parameters */
	if (nparams > 0) {
		datapkt[len++] = nparams & 0xff;
		datapkt[len++] = nparams >> 8;

		for (j = 0; j < nparams; j++) {
			uint32_t p = params[j];

			datapkt[len++] = p & 0xff;
			p >>= 8;
			datapkt[len++] = p & 0xff;
			p >>= 8;
			datapkt[len++] = p & 0xff;
			p >>= 8;
			datapkt[len++] = p & 0xff;
		}
	}

	/* Extra data */
	if (extra) {
		int x = exlen;

		datapkt[len++] = x & 0xff;
		x >>= 8;
		datapkt[len++] = x & 0xff;
		x >>= 8;
		datapkt[len++] = x & 0xff;
		x >>= 8;
		datapkt[len++] = x & 0xff;

		memcpy(datapkt + len, extra, exlen);
		len += exlen;
	}

	/* Checksum */
	cksum = calc_checksum(datapkt, len);
	datapkt[len++] = cksum & 0xff;
	datapkt[len++] = cksum >> 8;

	/* Copy into buf, escaping special characters and adding
	 * delimeters.
	 */
	if (!(dev->flags & FET_PROTO_NOLEAD_SEND))
		buf[i++] = 0x7e;

	for (j = 0; j < len; j++) {
		char c = datapkt[j];

		if (c == 0x7e || c == 0x7d) {
			buf[i++] = 0x7d;
			c ^= 0x20;
		}

		buf[i++] = c;
	}
	buf[i++] = 0x7e;

	assert (i < sizeof(buf));

	return dev->transport->send(dev->transport, buf, i);
}

static int xfer(struct fet_device *dev,
		int command_code, const uint8_t *data, int datalen,
		int nparams, ...)
{
	uint32_t params[MAX_PARAMS];
	int i;
	va_list ap;

	assert (nparams <= MAX_PARAMS);

	va_start(ap, nparams);
	for (i = 0; i < nparams; i++)
		params[i] = va_arg(ap, uint32_t);
	va_end(ap);

	if (data && (dev->flags & FET_PROTO_SEPARATE_DATA)) {
		assert (nparams + 1 <= MAX_PARAMS);
		params[nparams++] = datalen;

		if (send_rf2500_data(dev, data, datalen) < 0)
			return -1;
		if (send_command(dev, command_code, params, nparams,
				 NULL, 0) < 0)
			return -1;
	} else if (send_command(dev, command_code, params, nparams,
				data, datalen) < 0)
		return -1;

	if (recv_packet(dev) < 0)
		return -1;

	if (dev->fet_reply.command_code != command_code) {
		printc_err("fet: reply type mismatch\n");
		return -1;
	}

	return 0;
}

/**********************************************************************
 * MSP430 high-level control functions
 */

static void show_dev_info(const char *name, const struct fet_device *dev)
{
	printc("Device: %s\n", name);
	printc_dbg("Code memory starts at 0x%04x\n", dev->code_start);
	printc_dbg("Number of breakpoints: %d\n", dev->base.max_breakpoints);
}

static int identify_old(struct fet_device *dev)
{
	char idtext[64];

	if (xfer(dev, C_IDENTIFY, NULL, 0, 2, 70, 0) < 0)
		return -1;

	if (dev->fet_reply.datalen < 0x26) {
		printc_err("fet: missing info\n");
		return -1;
	}

	memcpy(idtext, dev->fet_reply.data + 4, 32);
	idtext[32] = 0;

	dev->code_start = LE_WORD(dev->fet_reply.data, 0x24);
	dev->base.max_breakpoints = LE_WORD(dev->fet_reply.data, 0x2a);

	show_dev_info(idtext, dev);

	return 0;
}

static int identify_new(struct fet_device *dev, const char *force_id)
{
	const struct fet_db_record *r;

	if (xfer(dev, C_IDENT1, NULL, 0, 2, 0, 0) < 0) {
		printc_err("fet: command C_IDENT1 failed\n");
		return -1;
	}

	if (dev->fet_reply.datalen < 2) {
		printc_err("fet: missing info\n");
		return -1;
	}

	printc_dbg("Device ID: 0x%02x%02x\n",
	       dev->fet_reply.data[0], dev->fet_reply.data[1]);

	if (force_id)
		r = fet_db_find_by_name(force_id);
	else
		r = fet_db_find_by_msg28(dev->fet_reply.data,
					 dev->fet_reply.datalen);

	if (!r) {
		printc_err("fet: unknown device\n");
		debug_hexdump("msg28_data:", dev->fet_reply.data,
			      dev->fet_reply.datalen);
		return -1;
	}

	dev->code_start = LE_WORD(r->msg29_data, 0);
	dev->base.max_breakpoints = LE_WORD(r->msg29_data, 0x14);

	show_dev_info(r->name, dev);

	if (xfer(dev, C_IDENT3, r->msg2b_data, r->msg2b_len, 0) < 0)
		printc_err("fet: warning: message C_IDENT3 failed\n");

	if (xfer(dev, C_IDENT2, r->msg29_data, FET_DB_MSG29_LEN,
		 3, r->msg29_params[0], r->msg29_params[1],
		 r->msg29_params[2]) < 0) {
		printc_err("fet: message C_IDENT2 failed\n");
		return -1;
	}

	/* This packet seems to be necessary in order to program on the
	 * MSP430FR5739 development board.
	 */
	if (xfer(dev, 0x30, NULL, 0, 0) < 0)
		printc_dbg("fet: warning: message 0x30 failed\n");

	return 0;
}

static int do_identify(struct fet_device *dev, const char *force_id)
{
	if (dev->flags & FET_PROTO_IDENTIFY_NEW)
		return identify_new(dev, force_id);

	if (dev->version < 20300000)
		return identify_old(dev);

	return identify_new(dev, force_id);
}

static int do_run(struct fet_device *dev, int type)
{
	if (xfer(dev, C_RUN, NULL, 0, 2, type, 0) < 0) {
		printc_err("fet: failed to restart CPU\n");
		return -1;
	}

	return 0;
}

static int fet_erase(device_t dev_base, device_erase_type_t type,
		     address_t addr)
{
	struct fet_device *dev = (struct fet_device *)dev_base;
	int fet_erase_type = FET_ERASE_MAIN;

	if (xfer(dev, C_CONFIGURE, NULL, 0, 2, FET_CONFIG_CLKCTRL, 0x26) < 0) {
		printc_err("fet: config (1) failed\n");
		return -1;
	}

	if (xfer(dev, C_CONFIGURE, NULL, 0, 2, FET_CONFIG_FLASH_LOCK, 0) < 0) {
		printc_err("fet: config (2) failed\n");
		return -1;
	}

	switch (type) {
	case DEVICE_ERASE_MAIN:
		fet_erase_type = FET_ERASE_MAIN;
		addr = dev->code_start;
		break;

	case DEVICE_ERASE_SEGMENT:
		fet_erase_type = FET_ERASE_SEGMENT;
		break;

	case DEVICE_ERASE_ALL:
		fet_erase_type = FET_ERASE_ALL;
		addr = dev->code_start;
		break;

	default:
		printc_err("fet: unsupported erase type\n");
		return -1;
	}

	if (xfer(dev, C_ERASE, NULL, 0, 3, fet_erase_type, addr, 0) < 0) {
		printc_err("fet: erase command failed\n");
		return -1;
	}

	return 0;
}

static device_status_t fet_poll(device_t dev_base)
{
	struct fet_device *dev = (struct fet_device *)dev_base;

	ctrlc_reset();
	if ((usleep(50000) < 0) || ctrlc_check())
		return DEVICE_STATUS_INTR;

	if (xfer(dev, C_STATE, NULL, 0, 1, 0) < 0) {
		printc_err("fet: polling failed\n");
		return DEVICE_STATUS_ERROR;
	}

	if (!(dev->fet_reply.argv[0] & FET_POLL_RUNNING))
		return DEVICE_STATUS_HALTED;

	return DEVICE_STATUS_RUNNING;
}

static int refresh_bps(struct fet_device *dev)
{
	int i;
	int ret = 0;

	for (i = 0; i < dev->base.max_breakpoints; i++) {
		struct device_breakpoint *bp = &dev->base.breakpoints[i];

		if (bp->flags & DEVICE_BP_DIRTY) {
			uint16_t addr = bp->addr;

			if (!(bp->flags & DEVICE_BP_ENABLED))
				addr = 0;

			if (xfer(dev, C_BREAKPOINT, NULL, 0, 2, i, addr) < 0) {
				printc_err("fet: failed to refresh "
					"breakpoint #%d\n", i);
				ret = -1;
			} else {
				bp->flags &= ~DEVICE_BP_DIRTY;
			}
		}
	}

	return ret;
}

static int fet_ctl(device_t dev_base, device_ctl_t action)
{
	struct fet_device *dev = (struct fet_device *)dev_base;

	switch (action) {
	case DEVICE_CTL_RESET:
		if (xfer(dev, C_RESET, NULL, 0, 3, FET_RESET_ALL, 0, 0) < 0) {
			printc_err("fet: reset failed\n");
			return -1;
		}
		break;

	case DEVICE_CTL_RUN:
		if (refresh_bps(dev) < 0)
			printc_err("warning: fet: failed to refresh "
				"breakpoints\n");
		return do_run(dev, FET_RUN_BREAKPOINT);

	case DEVICE_CTL_HALT:
		if (xfer(dev, C_STATE, NULL, 0, 1, 1) < 0) {
			printc_err("fet: failed to halt CPU\n");
			return -1;
		}
		break;

	case DEVICE_CTL_STEP:
		if (do_run(dev, FET_RUN_STEP) < 0)
			return -1;

		for (;;) {
			device_status_t status = fet_poll(dev_base);

			if (status == DEVICE_STATUS_ERROR ||
			    status == DEVICE_STATUS_INTR)
				return -1;

			if (status == DEVICE_STATUS_HALTED)
				break;
		}
		break;
	}

	return 0;
}

static void fet_destroy(device_t dev_base)
{
	struct fet_device *dev = (struct fet_device *)dev_base;

	if (xfer(dev, C_RESET, NULL, 0, 3, FET_RESET_ALL, 1, 1) < 0)
		printc_err("fet: final reset failed\n");

	if (xfer(dev, C_CLOSE, NULL, 0, 1, 0) < 0)
		printc_err("fet: close command failed\n");

	dev->transport->destroy(dev->transport);
	free(dev);
}

static int read_byte(struct fet_device *dev, address_t addr, uint8_t *out)
{
	address_t base = addr & ~1;

	if (xfer(dev, C_READMEMORY, NULL, 0, 2, base, 2) < 0) {
		printc_err("fet: failed to read byte from 0x%04x\n", addr);
		return -1;
	}

	*out = dev->fet_reply.data[addr & 1];
	return 0;
}

static int write_byte(struct fet_device *dev, address_t addr, uint8_t value)
{
	uint8_t buf[2];
	address_t base = addr & ~1;

	if (xfer(dev, C_READMEMORY, NULL, 0, 2, base, 2) < 0) {
		printc_err("fet: failed to read byte from 0x%04x\n", addr);
		return -1;
	}

	buf[0] = dev->fet_reply.data[0];
	buf[1] = dev->fet_reply.data[1];
	buf[addr & 1] = value;

	if (xfer(dev, C_WRITEMEMORY, buf, 2, 1, base) < 0) {
		printc_err("fet: failed to write byte from 0x%04x\n", addr);
		return -1;
	}

	return 0;
}

static int get_adjusted_block_size(void)
{
	int block_size = opdb_get_numeric("fet_block_size") & ~1;

	if (block_size < 2)
		block_size = 2;
	if (block_size > MAX_BLOCK_SIZE)
		block_size = MAX_BLOCK_SIZE;

	return block_size;
}

int fet_readmem(device_t dev_base, address_t addr, uint8_t *buffer,
		address_t count)
{
	struct fet_device *dev = (struct fet_device *)dev_base;
	int block_size = get_adjusted_block_size();

	if (addr & 1) {
		if (read_byte(dev, addr, buffer) < 0)
			return -1;
		addr++;
		buffer++;
		count--;
	}

	while (count > 1) {
		int plen = count > block_size ? block_size : count;

		plen &= ~0x1;

		if (xfer(dev, C_READMEMORY, NULL, 0, 2, addr, plen) < 0) {
			printc_err("fet: failed to read "
				"from 0x%04x\n", addr);
			return -1;
		}

		if (dev->fet_reply.datalen < plen) {
			printc_err("fet: short data: "
				"%d bytes\n", dev->fet_reply.datalen);
			return -1;
		}

		memcpy(buffer, dev->fet_reply.data, plen);
		buffer += plen;
		count -= plen;
		addr += plen;
	}

	if (count && read_byte(dev, addr, buffer) < 0)
		return -1;

	return 0;
}

int fet_writemem(device_t dev_base, address_t addr,
		 const uint8_t *buffer, address_t count)
{
	struct fet_device *dev = (struct fet_device *)dev_base;
	int block_size = get_adjusted_block_size();

	if (addr & 1) {
		if (write_byte(dev, addr, *buffer) < 0)
			return -1;
		addr++;
		buffer++;
		count--;
	}

	while (count > 1) {
		int plen = count > block_size ? block_size : count;
		int ret;

		plen &= ~0x1;

		ret = xfer(dev, C_WRITEMEMORY, buffer, plen, 1, addr);

		if (ret < 0) {
			printc_err("fet: failed to write to 0x%04x\n",
				addr);
			return -1;
		}

		buffer += plen;
		count -= plen;
		addr += plen;
	}

	if (count && write_byte(dev, addr, *buffer) < 0)
		return -1;

	return 0;
}

static int fet_getregs(device_t dev_base, address_t *regs)
{
	struct fet_device *dev = (struct fet_device *)dev_base;
	int i;

	if (xfer(dev, C_READREGISTERS, NULL, 0, 0) < 0)
		return -1;

	if (dev->fet_reply.datalen < DEVICE_NUM_REGS * 4) {
		printc_err("fet: short reply (%d bytes)\n",
			dev->fet_reply.datalen);
		return -1;
	}

	for (i = 0; i < DEVICE_NUM_REGS; i++)
		regs[i] = LE_LONG(dev->fet_reply.data, i * 4);

	return 0;
}

static int fet_setregs(device_t dev_base, const address_t *regs)
{
	struct fet_device *dev = (struct fet_device *)dev_base;
	uint8_t buf[DEVICE_NUM_REGS * 4];;
	int i;
	int ret;

	memset(buf, 0, sizeof(buf));

	for (i = 0; i < DEVICE_NUM_REGS; i++) {
		buf[i * 4] = regs[i] & 0xff;
		buf[i * 4 + 1] = (regs[i] >> 8) & 0xff;
		buf[i * 4 + 2] = (regs[i] >> 16) & 0xff;
		buf[i * 4 + 3] = regs[i] >> 24;
	}

	ret = xfer(dev, C_WRITEREGISTERS, buf, sizeof(buf), 1, 0xffff);

	if (ret < 0) {
		printc_err("fet: context set failed\n");
		return -1;
	}

	return 0;
}

static int do_configure(struct fet_device *dev,
		        const struct device_args *args)
{
	if (!(args->flags & DEVICE_FLAG_JTAG)) {
		if (!xfer(dev, C_CONFIGURE, NULL, 0,
			  2, FET_CONFIG_PROTOCOL, 1)) {
			printc_dbg("Configured for Spy-Bi-Wire\n");
			return 0;
		}

		printc_err("fet: Spy-Bi-Wire configuration failed\n");
		return -1;
	}

	if (!xfer(dev, C_CONFIGURE, NULL, 0,
		  2, FET_CONFIG_PROTOCOL, 2)) {
		printc_dbg("Configured for JTAG (2)\n");
		return 0;
	}

	printc_err("fet: warning: JTAG configuration failed -- "
		"retrying\n");

	if (!xfer(dev, C_CONFIGURE, NULL, 0,
		  2, FET_CONFIG_PROTOCOL, 0)) {
		printc_dbg("Configured for JTAG (0)\n");
		return 0;
	}

	printc_err("fet: JTAG configuration failed\n");
	return -1;
}

int try_open(struct fet_device *dev, const struct device_args *args,
	     int send_reset)
{
	transport_t transport = dev->transport;

	if (dev->flags & FET_PROTO_NOLEAD_SEND) {
		printc("Resetting Olimex command processor...\n");
		transport->send(dev->transport, (const uint8_t *)"\x7e", 1);
		usleep(5000);
		transport->send(dev->transport, (const uint8_t *)"\x7e", 1);
		usleep(5000);
	}

	printc_dbg("Initializing FET...\n");
	if (xfer(dev, C_INITIALIZE, NULL, 0, 0) < 0) {
		printc_err("fet: open failed\n");
		return -1;
	}

	dev->version = dev->fet_reply.argv[0];
	printc_dbg("FET protocol version is %d\n", dev->version);

	if (xfer(dev, 0x27, NULL, 0, 1, 4) < 0) {
		printc_err("fet: init failed\n");
		return -1;
	}

	if (do_configure(dev, args) < 0)
		return -1;

	if (send_reset || args->flags & DEVICE_FLAG_FORCE_RESET) {
		printc_dbg("Sending reset...\n");
		if (xfer(dev, C_RESET, NULL, 0, 3, FET_RESET_ALL, 0, 0) < 0)
			printc_err("warning: fet: reset failed\n");
	}

	/* set VCC */
	if (xfer(dev, C_VCC, NULL, 0, 1, args->vcc_mv) < 0)
		printc_err("warning: fet: set VCC failed\n");
	else
		printc_dbg("Set Vcc: %d mV\n", args->vcc_mv);

	/* Identify the chip */
	if (do_identify(dev, args->forced_chip_id) < 0) {
		printc_err("fet: identify failed\n");
		return -1;
	}

	return 0;
}

static device_t fet_open(const struct device_args *args,
		         int flags, transport_t transport,
		         const struct device_class *type)
{
	struct fet_device *dev = malloc(sizeof(*dev));
	int i;

	if (!dev) {
		pr_error("fet: failed to allocate memory");
		return NULL;
	}

	memset(dev, 0, sizeof(*dev));

	dev->base.type = type;
	dev->transport = transport;
	dev->flags = flags;

	if (try_open(dev, args, 0) < 0) {
		usleep(500000);
		printc("Trying again...\n");
		if (try_open(dev, args, 1) < 0)
			goto fail;
	}

	/* Make sure breakpoints get reset on the first run */
	if (dev->base.max_breakpoints > DEVICE_MAX_BREAKPOINTS)
		dev->base.max_breakpoints = DEVICE_MAX_BREAKPOINTS;
	for (i = 0; i < dev->base.max_breakpoints; i++)
		dev->base.breakpoints[i].flags = DEVICE_BP_DIRTY;

	return (device_t)dev;

 fail:
	transport->destroy(transport);
	free(dev);
	return NULL;
}

static device_t fet_open_rf2500(const struct device_args *args)
{
	transport_t trans;

	if (args->flags & DEVICE_FLAG_TTY) {
		printc_err("This driver does not support TTY devices.\n");
		return NULL;
	}

        trans = rf2500_open(args->path, args->requested_serial);
        if (!trans)
                return NULL;

        return fet_open(args, FET_PROTO_SEPARATE_DATA, trans, &device_rf2500);
}

const struct device_class device_rf2500 = {
	.name		= "rf2500",
	.help		=
"eZ430-RF2500 devices. Only USB connection is supported.",
	.open		= fet_open_rf2500,
	.destroy	= fet_destroy,
	.readmem	= fet_readmem,
	.writemem	= fet_writemem,
	.erase		= fet_erase,
	.getregs	= fet_getregs,
	.setregs	= fet_setregs,
	.ctl		= fet_ctl,
	.poll		= fet_poll
};

static device_t fet_open_olimex(const struct device_args *args)
{
	transport_t trans;

	if (args->flags & DEVICE_FLAG_TTY)
		trans = uif_open(args->path, UIF_TYPE_OLIMEX);
	else
		trans = olimex_open(args->path, args->requested_serial);

        if (!trans)
                return NULL;

	return fet_open(args, FET_PROTO_NOLEAD_SEND | FET_PROTO_EXTRA_RECV |
			      FET_PROTO_IDENTIFY_NEW,
			trans, &device_olimex);
}

const struct device_class device_olimex = {
	.name		= "olimex",
	.help		=
"Olimex MSP-JTAG-TINY.",
	.open		= fet_open_olimex,
	.destroy	= fet_destroy,
	.readmem	= fet_readmem,
	.writemem	= fet_writemem,
	.erase		= fet_erase,
	.getregs	= fet_getregs,
	.setregs	= fet_setregs,
	.ctl		= fet_ctl,
	.poll		= fet_poll
};

static device_t fet_open_olimex_iso(const struct device_args *args)
{
	transport_t trans;

	if (!(args->flags & DEVICE_FLAG_TTY)) {
		printc_err("This driver does not support raw USB access.\n");
		return NULL;
	}

	trans = uif_open(args->path, UIF_TYPE_OLIMEX_ISO);
        if (!trans)
                return NULL;

	return fet_open(args, FET_PROTO_NOLEAD_SEND | FET_PROTO_EXTRA_RECV |
			      FET_PROTO_IDENTIFY_NEW,
			trans, &device_olimex_iso);
}

const struct device_class device_olimex_iso = {
	.name		= "olimex-iso",
	.help		=
"Olimex MSP-JTAG-ISO.",
	.open		= fet_open_olimex_iso,
	.destroy	= fet_destroy,
	.readmem	= fet_readmem,
	.writemem	= fet_writemem,
	.erase		= fet_erase,
	.getregs	= fet_getregs,
	.setregs	= fet_setregs,
	.ctl		= fet_ctl,
	.poll		= fet_poll
};

static device_t fet_open_uif(const struct device_args *args)
{
	transport_t trans;

	if (args->flags & DEVICE_FLAG_TTY)
		trans = uif_open(args->path, UIF_TYPE_FET);
	else
		trans = ti3410_open(args->path, args->requested_serial);

	if (!trans)
		return NULL;

	return fet_open(args, 0, trans, &device_uif);
}

const struct device_class device_uif = {
	.name		= "uif",
	.help		=
"TI FET430UIF and compatible devices (e.g. eZ430).",
	.open		= fet_open_uif,
	.destroy	= fet_destroy,
	.readmem	= fet_readmem,
	.writemem	= fet_writemem,
	.erase		= fet_erase,
	.getregs	= fet_getregs,
	.setregs	= fet_setregs,
	.ctl		= fet_ctl,
	.poll		= fet_poll
};
