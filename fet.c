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

#define MAX_PARAMS		16

struct fet_device {
	struct device                   base;

	transport_t                     transport;
	int                             is_rf2500;
	int                             version;
	int                             have_breakpoint;

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

/* Constants for parameters of various FET commands */
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

#define BUFFER_BYTE(b, x) ((int)((uint8_t *)(b))[x])
#define BUFFER_WORD(b, x) ((BUFFER_BYTE(b, x + 1) << 8) | BUFFER_BYTE(b, x))
#define BUFFER_LONG(b, x) ((BUFFER_WORD(b, x + 2) << 16) | BUFFER_WORD(b, x))

#define PTYPE_ACK		0
#define PTYPE_CMD		1
#define PTYPE_PARAM		2
#define PTYPE_DATA		3
#define PTYPE_MIXED		4
#define PTYPE_NAK		5
#define PTYPE_FLASH_ACK		6

/* This table is taken from uif430 */
static const char *error_strings[] =
{
        "No error",                                                     // 0
        "Could not initialize device interface",                        // 1
        "Could not close device interface",                             // 2
        "Invalid parameter(s)",                                         // 3
        "Could not find device (or device not supported)",              // 4
        "Unknown device",                                               // 5
        "Could not read device memory",                                 // 6
        "Could not write device memory",                                // 7
        "Could not read device configuration fuses",                    // 8
        "Incorrectly configured device; device derivative not supported",// 9

        "Could not set device Vcc",                                     // 10
        "Could not reset device",                                       // 11
        "Could not preserve/restore device memory",                     // 12
        "Could not set device operating frequency",                     // 13
        "Could not erase device memory",                                // 14
        "Could not set device breakpoint",                              // 15
        "Could not single step device",                                 // 16
        "Could not run device (to breakpoint)",                         // 17
        "Could not determine device state",                             // 18
        "Could not open Enhanced Emulation Module",                     // 19

        "Could not read Enhanced Emulation Module register",            // 20
        "Could not write Enhanced Emulation Module register",           // 21
        "Could not close Enhanced Emulation Module",                    // 22
        "File open error",                                              // 23
        "Could not determine file type",                                // 24
        "Unexpected end of file encountered",                           // 25
        "File input/output error",                                      // 26
        "File data error",                                              // 27
        "Verification error",                                           // 28
        "Could not blow device security fuse",                          // 29

        "Could not access device - security fuse is blown",             // 30
        "Error within Intel Hex file",                                  // 31
        "Could not write device Register",                              // 32
        "Could not read device Register",                               // 33
        "Not supported by selected Interface",                          // 34
        "Could not communicate with FET",                               // 35
        "No external power supply detected",                            // 36
        "External power too low",                                       // 37
        "External power detected",                                      // 38
        "External power too high",                                      // 39

        "Hardware Self Test Error",                                     // 40
        "Fast Flash Routine experienced a timeout",                     // 41
        "Could not create thread for polling",                          // 42
        "Could not initialize Enhanced Emulation Module",               // 43
        "Insufficient resources",                                       // 44
        "No clock control emulation on connected device",               // 45
        "No state storage buffer implemented on connected device",      // 46
        "Could not read trace buffer",                                  // 47
        "Enable the variable watch function",                           // 48
        "No trigger sequencer implemented on connected device",         // 49

        "Could not read sequencer state - Sequencer is disabled",       // 50
        "Could not remove trigger - Used in sequencer",                 // 51
        "Could not set combination - Trigger is used in sequencer",     // 52
        "Invalid error number",                                         // 53
};

static int parse_packet(struct fet_device *dev, int plen)
{
	uint16_t c = calc_checksum(dev->fet_buf + 2, plen - 2);
	uint16_t r = BUFFER_WORD(dev->fet_buf, plen);
	int i = 2;
	int type;
	int error;

	if (c != r) {
		fprintf(stderr, "fet: checksum error (calc %04x,"
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
		fprintf(stderr, "fet: FET returned error code %d\n",
			error);
		if (error > 0 && error < ARRAY_LEN(error_strings)) {
			fprintf(stderr, "    (%s)\n", error_strings[error]);
		}
		return -1;
	}

	if (type == PTYPE_NAK) {
		fprintf(stderr, "fet: FET returned NAK\n");
		return -1;
	}

	/* Parse packet parameters */
	if (type == PTYPE_PARAM || type == PTYPE_MIXED) {
		int j;

		if (i + 2 > plen)
			goto too_short;

		dev->fet_reply.argc = BUFFER_WORD(dev->fet_buf, i);
		i += 2;

		if (dev->fet_reply.argc >= MAX_PARAMS) {
			fprintf(stderr, "fet: too many params: %d\n",
				dev->fet_reply.argc);
			return -1;
		}

		for (j = 0; j < dev->fet_reply.argc; j++) {
			if (i + 4 > plen)
				goto too_short;
			dev->fet_reply.argv[j] = BUFFER_LONG(dev->fet_buf, i);
			i += 4;
		}
	} else {
		dev->fet_reply.argc = 0;
	}

	/* Extract a pointer to the data */
	if (type == PTYPE_DATA || type == PTYPE_MIXED) {
		if (i + 4 > plen)
			goto too_short;

		dev->fet_reply.datalen = BUFFER_LONG(dev->fet_buf, i);
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
	fprintf(stderr, "fet: too short (%d bytes)\n",
		plen);
	return -1;
}

static int recv_packet(struct fet_device *dev)
{
	int plen = BUFFER_WORD(dev->fet_buf, 0);

	/* If there's a packet still here from last time, get rid of it */
	if (dev->fet_len >= plen + 2) {
		memmove(dev->fet_buf, dev->fet_buf + plen + 2,
			dev->fet_len - plen - 2);
		dev->fet_len -= plen + 2;
	}

	/* Keep adding data to the buffer until we have a complete packet */
	for (;;) {
		int len;

		plen = BUFFER_WORD(dev->fet_buf, 0);
		if (dev->fet_len >= plen + 2)
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
	uint8_t datapkt[256];
	int len = 0;

	uint8_t buf[512];
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
		params[i] = va_arg(ap, unsigned int);
	va_end(ap);

	if (data && dev->is_rf2500) {
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
		fprintf(stderr, "fet: reply type mismatch\n");
		return -1;
	}

	return 0;
}

/**********************************************************************
 * MSP430 high-level control functions
 */

static int do_identify(struct fet_device *dev)
{
	char idtext[64];

	if (dev->version < 20300000) {
		if (xfer(dev, C_IDENTIFY, NULL, 0, 2, 70, 0) < 0)
			return -1;

		if (!dev->fet_reply.data) {
			fprintf(stderr, "fet: missing info\n");
			return -1;
		}

		memcpy(idtext, dev->fet_reply.data + 4, 32);
		idtext[32] = 0;
	} else {
		uint16_t id;

		if (xfer(dev, 0x28, NULL, 0, 2, 0, 0) < 0) {
			fprintf(stderr, "fet: command 0x28 failed\n");
			return -1;
		}

		if (dev->fet_reply.datalen < 2) {
			fprintf(stderr, "fet: missing info\n");
			return -1;
		}

		id = (dev->fet_reply.data[0] << 8) | dev->fet_reply.data[1];
		if (device_id_text(id, idtext, sizeof(idtext)) < 0) {
			printf("Unknown device ID: 0x%04x\n", id);
			return 0;
		}
	}

	printf("Device: %s\n", idtext);
	return 0;
}

static int do_run(struct fet_device *dev, int type)
{
	if (xfer(dev, C_RUN, NULL, 0, 2, type, 0) < 0) {
		fprintf(stderr, "fet: failed to restart CPU\n");
		return -1;
	}

	return 0;
}

static int do_erase(struct fet_device *dev)
{
	if (xfer(dev, C_RESET, NULL, 0, 3, FET_RESET_ALL, 0, 0) < 0) {
		fprintf(stderr, "fet: reset before erase failed\n");
		return -1;
	}

	if (xfer(dev, C_CONFIGURE, NULL, 0, 2, 2, 0x26) < 0) {
		fprintf(stderr, "fet: config (1) failed\n");
		return -1;
	}

	if (xfer(dev, C_CONFIGURE, NULL, 0, 2, 5, 0) < 0) {
		fprintf(stderr, "fet: config (2) failed\n");
		return -1;
	}

	if (xfer(dev, C_ERASE, NULL, 0, 3, FET_ERASE_MAIN, 0x8000, 2) < 0) {
		fprintf(stderr, "fet: erase command failed\n");
		return -1;
	}

	return 0;
}

static device_status_t fet_poll(device_t dev_base)
{
	struct fet_device *dev = (struct fet_device *)dev_base;

	/* Without this delay, breakpoints can get lost. */
	if (usleep(500000) < 0)
		return DEVICE_STATUS_INTR;

	if (xfer(dev, C_STATE, NULL, 0, 1, 0) < 0) {
		fprintf(stderr, "fet: polling failed\n");
		return DEVICE_STATUS_ERROR;
	}

	if (!(dev->fet_reply.argv[0] & FET_POLL_RUNNING))
		return DEVICE_STATUS_HALTED;

	return DEVICE_STATUS_RUNNING;
}

static int fet_ctl(device_t dev_base, device_ctl_t action)
{
	struct fet_device *dev = (struct fet_device *)dev_base;

	switch (action) {
	case DEVICE_CTL_RESET:
		if (xfer(dev, C_RESET, NULL, 0, 3, FET_RESET_ALL, 0, 0) < 0) {
			fprintf(stderr, "fet: reset failed\n");
			return -1;
		}
		break;

	case DEVICE_CTL_RUN:
		return do_run(dev, dev->have_breakpoint ?
			      FET_RUN_BREAKPOINT : FET_RUN_FREE);

	case DEVICE_CTL_HALT:
		if (xfer(dev, C_STATE, NULL, 0, 1, 1) < 0) {
			fprintf(stderr, "fet: failed to halt CPU\n");
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

	case DEVICE_CTL_ERASE:
		return do_erase(dev);
	}

	return 0;
}

static void fet_destroy(device_t dev_base)
{
	struct fet_device *dev = (struct fet_device *)dev_base;

	if (xfer(dev, C_RUN, NULL, 0, 2, FET_RUN_FREE, 1) < 0)
		fprintf(stderr, "fet: failed to restart CPU\n");

	if (xfer(dev, C_CLOSE, NULL, 0, 1, 0) < 0)
		fprintf(stderr, "fet: close command failed\n");

	dev->transport->destroy(dev->transport);
	free(dev);
}

int fet_readmem(device_t dev_base, uint16_t addr, uint8_t *buffer, int count)
{
	struct fet_device *dev = (struct fet_device *)dev_base;

	while (count) {
		int plen = count > 128 ? 128 : count;

		if (xfer(dev, C_READMEMORY, NULL, 0, 2, addr, plen) < 0) {
			fprintf(stderr, "fet: failed to read "
				"from 0x%04x\n", addr);
			return -1;
		}

		if (dev->fet_reply.datalen < plen) {
			fprintf(stderr, "fet: short data: "
				"%d bytes\n", dev->fet_reply.datalen);
			return -1;
		}

		memcpy(buffer, dev->fet_reply.data, plen);
		buffer += plen;
		count -= plen;
		addr += plen;
	}

	return 0;
}

int fet_writemem(device_t dev_base, uint16_t addr,
		 const uint8_t *buffer, int count)
{
	struct fet_device *dev = (struct fet_device *)dev_base;

	while (count) {
		int plen = count > 128 ? 128 : count;
		int ret;

		ret = xfer(dev, C_WRITEMEMORY, buffer, plen, 1, addr);

		if (ret < 0) {
			fprintf(stderr, "fet: failed to write to 0x%04x\n",
				addr);
			return -1;
		}

		buffer += plen;
		count -= plen;
		addr += plen;
	}

	return 0;
}

static int fet_getregs(device_t dev_base, uint16_t *regs)
{
	struct fet_device *dev = (struct fet_device *)dev_base;
	int i;

	if (xfer(dev, C_READREGISTERS, NULL, 0, 0) < 0)
		return -1;

	if (dev->fet_reply.datalen < DEVICE_NUM_REGS * 4) {
		fprintf(stderr, "fet: short reply (%d bytes)\n",
			dev->fet_reply.datalen);
		return -1;
	}

	for (i = 0; i < DEVICE_NUM_REGS; i++)
		regs[i] = BUFFER_WORD(dev->fet_reply.data, i * 4);

	return 0;
}

static int fet_setregs(device_t dev_base, const uint16_t *regs)
{
	struct fet_device *dev = (struct fet_device *)dev_base;
	uint8_t buf[DEVICE_NUM_REGS * 4];;
	int i;
	int ret;

	memset(buf, 0, sizeof(buf));

	for (i = 0; i < DEVICE_NUM_REGS; i++) {
		buf[i * 4] = regs[i] & 0xff;
		buf[i * 4 + 1] = regs[i] >> 8;
	}

	ret = xfer(dev, C_WRITEREGISTERS, buf, sizeof(buf), 1, 0xffff);

	if (ret < 0) {
		fprintf(stderr, "fet: context set failed\n");
		return -1;
	}

	return 0;
}

static int fet_breakpoint(device_t dev_base, int enabled, uint16_t addr)
{
	struct fet_device *dev = (struct fet_device *)dev_base;

	if (enabled) {
		dev->have_breakpoint = 1;

		if (xfer(dev, C_BREAKPOINT, NULL, 0, 2, 0, addr) < 0) {
			fprintf(stderr, "fet: set breakpoint failed\n");
			return -1;
		}
	} else {
		dev->have_breakpoint = 0;
	}

	return 0;
}

#define MAGIC_DATA_SIZE         0x4a
#define MAGIC_PARAM_COUNT       3

#define MAGIC_SEND_29           0x01
#define MAGIC_SEND_2B           0x02

struct magic_record {
	int             min_version;
	int             flags;
	uint32_t       param_29[MAGIC_PARAM_COUNT];
	const uint8_t  data_29[MAGIC_DATA_SIZE];
	const uint8_t  data_2b[MAGIC_DATA_SIZE];
};

/* The first entry in this table whose version exceeds the version
 * reported by the FET is used. Therefore, it must be kept in descending
 * order of version.
 */
const static struct magic_record magic_table[] = {
	{ /* TI Chronos */
		.min_version = 30001000,
		.flags = MAGIC_SEND_29 | MAGIC_SEND_2B,
		.param_29 = {0x77, 0x6f, 0x4a},
		.data_29 = {
			0x00, 0x80, 0xff, 0xff, 0x00, 0x00, 0x00, 0x18,
			0xff, 0x19, 0x80, 0x00, 0x00, 0x1c, 0xff, 0x2b,
			0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x02, 0x00,
			0x02, 0x00, 0x07, 0x24, 0x00, 0x00, 0x00, 0x00,
			0x08, 0x07, 0x10, 0x0e, 0xc4, 0x09, 0x70, 0x17,
			0x58, 0x1b, 0x01, 0x00, 0x03, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
			0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff
		},
		.data_2b = {
			0x00, 0x10, 0xff, 0x17, 0x00, 0x02, 0x01, 0x00,
			0x04, 0x00, 0x40, 0x00, 0x0a, 0x91, 0x8e, 0x00,
			0x00, 0xb0, 0x28, 0x29, 0x2a, 0x2b, 0x80, 0xd8,
			0xa8, 0x60, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00
		}
	},
	{ /* RF2500 */
		.min_version = 30000000,
		.flags = MAGIC_SEND_29,
		.param_29 = {0, 0x39, 0x31},
		.data_29 = {
			0x00, 0x80, 0xff, 0xff, 0x00, 0x00, 0x00, 0x10,
			0xff, 0x10, 0x40, 0x00, 0x00, 0x02, 0xff, 0x05,
			0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00,
			0x01, 0x00, 0xd7, 0x60, 0x00, 0x00, 0x00, 0x00,
			0x08, 0x07, 0x10, 0x0e, 0xc4, 0x09, 0x70, 0x17,
			0x58, 0x1b, 0x01, 0x00, 0x03, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
			0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x33, 0x0f, 0x1f, 0x0f,
			0xff, 0xff
		}
	},
	{ /* FET430UIF */
		.min_version = 20404000,
		.flags = MAGIC_SEND_29 | MAGIC_SEND_2B,
		.param_29 = {0, 7, 7},
		.data_29 = {
			0x00, 0x11, 0xff, 0xff, 0x00, 0x00, 0x00, 0x10,
			0xff, 0x10, 0x80, 0x00, 0x00, 0x02, 0xff, 0x09,
			0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x02, 0x00,
			0x00, 0x00, 0xd7, 0x60, 0x00, 0x00, 0x00, 0x00,
			0x08, 0x07, 0x10, 0x0e, 0xc4, 0x09, 0x70, 0x17,
			0x58, 0x1b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0xf3, 0x30, 0xd3, 0x30,
			0xc0, 0x30
		},
		.data_2b = {
			0x00, 0x0c, 0xff, 0x0f, 0x00, 0x02, 0x00, 0x00,
			0x03, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00
		}
	}
};

static int do_magic(struct fet_device *dev)
{
	int i;

	for (i = 0; i < ARRAY_LEN(magic_table); i++) {
		const struct magic_record *r = &magic_table[i];

		if (dev->version >= r->min_version) {
			printf("Sending magic messages for >= %d\n",
			       r->min_version);

			if ((r->flags & MAGIC_SEND_2B) &&
			    xfer(dev, 0x2b, r->data_2b,
				 MAGIC_DATA_SIZE, 0) < 0) {
				fprintf(stderr, "fet: command 0x2b failed\n");
				return -1;
			}

			if ((r->flags & MAGIC_SEND_29) &&
			    xfer(dev, 0x29, r->data_29, MAGIC_DATA_SIZE,
				 3, r->param_29[0], r->param_29[1],
				 r->param_29[2]) < 0) {
				fprintf(stderr, "fet: command 0x29 failed\n");
				return -1;
			}

			return 0;
		}
	}

	return 0;
}

device_t fet_open(transport_t transport, int proto_flags, int vcc_mv)
{
	struct fet_device *dev = malloc(sizeof(*dev));

	if (!dev) {
		perror("fet: failed to allocate memory");
		return NULL;
	}

	dev->base.destroy = fet_destroy;
	dev->base.readmem = fet_readmem;
	dev->base.writemem = fet_writemem;
	dev->base.getregs = fet_getregs;
	dev->base.setregs = fet_setregs;
	dev->base.breakpoint = fet_breakpoint;
	dev->base.ctl = fet_ctl;
	dev->base.poll = fet_poll;

	dev->transport = transport;
	dev->is_rf2500 = proto_flags & FET_PROTO_RF2500;
	dev->have_breakpoint = 0;

	dev->fet_len = 0;

	if (xfer(dev, C_INITIALIZE, NULL, 0, 0) < 0) {
		fprintf(stderr, "fet: open failed\n");
		goto fail;
	}

	dev->version = dev->fet_reply.argv[0];
	printf("FET protocol version is %d\n", dev->version);

	if (xfer(dev, 0x27, NULL, 0, 1, 4) < 0) {
		fprintf(stderr, "fet: init failed\n");
		goto fail;
	}

	/* configure: Spy-Bi-Wire or JTAG */
	if (xfer(dev, C_CONFIGURE, NULL, 0,
		 2, 8, (proto_flags & FET_PROTO_SPYBIWIRE) ? 1 : 0) < 0) {
		fprintf(stderr, "fet: configure failed\n");
		goto fail;
	}

	printf("Configured for %s\n",
		(proto_flags & FET_PROTO_SPYBIWIRE) ? "Spy-Bi-Wire" : "JTAG");

	/* set VCC */
	if (xfer(dev, C_VCC, NULL, 0, 1, vcc_mv) < 0) {
		fprintf(stderr, "fet: set VCC failed\n");
		goto fail;
	}

	printf("Set Vcc: %d mV\n", vcc_mv);

	/* Identify the chip */
	if (do_identify(dev) < 0) {
		fprintf(stderr, "fet: identify failed\n");
		goto fail;
	}

	/* Send the magic required by RF2500 and Chronos FETs */
	if (do_magic(dev) < 0) {
		fprintf(stderr, "fet: init magic failed\n");
		goto fail;
	}

	return (device_t)dev;

 fail:
	free(dev);
	return NULL;
}
