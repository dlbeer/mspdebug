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

#include <stdarg.h>
#include <assert.h>
#include <string.h>

#include "util.h"
#include "fet_proto.h"
#include "fet_error.h"
#include "output.h"

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
static int send_rf2500_data(struct fet_proto *dev,
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
		if (dev->transport->ops->send(dev->transport,
			pbuf, plen + 4) < 0)
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

static int parse_packet(struct fet_proto *dev, int plen)
{
	uint16_t c = calc_checksum(dev->fet_buf + 2, plen - 2);
	uint16_t r = LE_WORD(dev->fet_buf, plen);
	int i = 2;
	int type;

	if (c != r) {
		printc_err("fet: checksum error (calc %04x,"
			" recv %04x)\n", c, r);
		return -1;
	}

	if (plen < 6)
		goto too_short;

	dev->command_code = dev->fet_buf[i++];
	type = dev->fet_buf[i++];
	dev->state = dev->fet_buf[i++];
	dev->error = dev->fet_buf[i++];

	if (dev->error) {
		printc_err("fet: FET returned error code %d (%s)\n",
				dev->error, fet_error(dev->error));
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

		dev->argc = LE_WORD(dev->fet_buf, i);
		i += 2;

		if (dev->argc >= FET_PROTO_MAX_PARAMS) {
			printc_err("fet: too many params: %d\n", dev->argc);
			return -1;
		}

		for (j = 0; j < dev->argc; j++) {
			if (i + 4 > plen)
				goto too_short;
			dev->argv[j] = LE_LONG(dev->fet_buf, i);
			i += 4;
		}
	} else {
		dev->argc = 0;
	}

	/* Extract a pointer to the data */
	if (type == PTYPE_DATA || type == PTYPE_MIXED) {
		if (i + 4 > plen)
			goto too_short;

		dev->datalen = LE_LONG(dev->fet_buf, i);
		i += 4;

		if (i + dev->datalen > plen)
			goto too_short;

		dev->data = dev->fet_buf + i;
	} else {
		dev->data = NULL;
		dev->datalen = 0;
	}

	return 0;

too_short:
	printc_err("fet: too short (%d bytes)\n",
		plen);
	return -1;
}

static void do_chomp_ff(struct fet_proto *dev)
{
	int chomp_len = 0;

	while ((chomp_len < dev->fet_len) && dev->fet_buf[chomp_len] == 0xff)
	       chomp_len++;

	if (chomp_len)
		memmove(dev->fet_buf, dev->fet_buf + chomp_len,
			dev->fet_len - chomp_len);

	dev->fet_len -= chomp_len;
}

/* Receive a packet from the FET. The usual format is:
 *     <length (2 bytes)> <data> <checksum>
 *
 * The length is that of the data + checksum. Olimex JTAG adapters follow
 * all packets with a trailing 0x7e byte, which must be discarded.
 */
static int recv_packet(struct fet_proto *dev, int chomp_ff)
{
	int pkt_extra = (dev->proto_flags & FET_PROTO_EXTRA_RECV) ? 3 : 2;
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

		len = dev->transport->ops->recv(dev->transport,
						dev->fet_buf + dev->fet_len,
						sizeof(dev->fet_buf) -
						dev->fet_len);
		if (len < 0)
			return -1;
		dev->fet_len += len;

		if (chomp_ff)
			do_chomp_ff(dev);
	}

	return -1;
}

static int send_command(struct fet_proto *dev, int command_code,
		        const uint32_t *params, int nparams,
			const uint8_t *extra, int exlen)
{
	uint8_t datapkt[FET_PROTO_MAX_BLOCK * 2];
	int len = 0;

	uint8_t buf[FET_PROTO_MAX_BLOCK * 3];
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
	if (!(dev->proto_flags & FET_PROTO_NOLEAD_SEND))
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

	return dev->transport->ops->send(dev->transport, buf, i);
}

void fet_proto_init(struct fet_proto *dev, transport_t transport,
		    int proto_flags)
{
	dev->transport = transport;
	dev->proto_flags = proto_flags;
	dev->fet_len = 0;
}

int fet_proto_xfer(struct fet_proto *dev,
		   int command_code, const uint8_t *data, int datalen,
		   int nparams, ...)
{
	uint32_t params[FET_PROTO_MAX_PARAMS];
	int i;
	va_list ap;

	assert (nparams <= FET_PROTO_MAX_PARAMS);

	va_start(ap, nparams);
	for (i = 0; i < nparams; i++)
		params[i] = va_arg(ap, uint32_t);
	va_end(ap);

	if (data && (dev->proto_flags & FET_PROTO_SEPARATE_DATA)) {
		assert (nparams + 1 <= FET_PROTO_MAX_PARAMS);
		params[nparams++] = datalen;

		if (send_rf2500_data(dev, data, datalen) < 0)
			return -1;
		if (send_command(dev, command_code, params, nparams,
				 NULL, 0) < 0)
			return -1;
	} else if (send_command(dev, command_code, params, nparams,
				data, datalen) < 0)
		return -1;

	/* Olimex devices sometimes return a spurious 0xff before their
	 * response to C_INITIALIZE.
	 */
	if (recv_packet(dev, (command_code == 0x01)) < 0)
		return -1;

	if (dev->command_code != command_code) {
		printc_err("fet: reply type mismatch\n");
		return -1;
	}

	return 0;
}
