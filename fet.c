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
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "fet.h"

#define ARRAY_LEN(a) ((sizeof(a)) / sizeof((a)[0]))

static const struct fet_transport *fet_transport;
static int fet_is_rf2500;

/**********************************************************************
 * FET command codes.
 *
 * These come from uif430 by Robert Kavaler (kavaler@diva.com).
 * www.relavak.com
 */

#define C_INITIALIZE            1
#define C_CLOSE                 2
#define C_IDENTIFY              3
#define C_DEVICE                4
#define C_CONFIGURE             5
#define C_VCC                   6
#define C_RESET                 7
#define C_READREGISTERS         8
#define C_WRITEREGISTERS        9
#define C_READREGISTER          10
#define C_WRITEREGISTER         11
#define C_ERASE                 12
#define C_READMEMORY            13
#define C_WRITEMEMORY           14
#define C_FASTFLASHER           15
#define C_BREAKPOINT            16
#define C_RUN                   17
#define C_STATE                 18
#define C_SECURE                19
#define C_VERIFYMEMORY          20
#define C_FASTVERIFYMEMORY      21
#define C_ERASECHECK            22
#define C_EEMOPEN               23
#define C_EEMREADREGISTER       24
#define C_EEMREADREGISTERTEST   25
#define C_EEMWRITEREGISTER      26
#define C_EEMCLOSE              27
#define C_ERRORNUMBER           28
#define C_GETCURVCCT            29
#define C_GETEXTVOLTAGE         30
#define C_FETSELFTEST           31
#define C_FETSETSIGNALS         32
#define C_FETRESET              33
#define C_READI2C               34
#define C_WRITEI2C              35
#define C_ENTERBOOTLOADER       36

/*********************************************************************
 * Checksum calculation
 */

static u_int16_t code_left[65536];

/* Initialise the code table. The code table is a function which takes
 * us from one checksum position code to the next.
 */

static void init_codes(void)
{
	int i;

	for (i = 0; i < 65536; i++) {
		u_int16_t right = i << 1;

		if (i & 0x8000)
			right ^= 0x0811;

		code_left[right] = i;
	}
}

/* Calculate the checksum over the given payload and return it. This checksum
 * needs to be stored in little-endian format at the end of the payload.
 */

static u_int16_t calc_checksum(const char *data, int len)
{
	int i;
	u_int16_t cksum = 0xffff;
	u_int16_t code = 0x8408;

	for (i = len * 8; i; i--)
		cksum = code_left[cksum];

	for (i = len - 1; i >= 0; i--) {
		int j;
		u_int8_t c = data[i];

		for (j = 0; j < 8; j++) {
			if (c & 0x80)
				cksum ^= code;
			code = code_left[code];
			c <<= 1;
		}
	}

	return cksum ^ 0xffff;
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
static int send_rf2500_data(const char *data, int len)
{
	int offset = 0;

	assert (fet_transport != NULL);
	while (len) {
		char pbuf[63];
		int plen = len > 59 ? 59 : len;

		pbuf[0] = 0x83;
		pbuf[1] = offset & 0xff;
		pbuf[2] = offset >> 8;
		pbuf[3] = plen;
		memcpy(pbuf + 4, data, plen);
		if (fet_transport->send(pbuf, plen + 4) < 0)
			return -1;

		data += plen;
		len -= plen;
		offset += plen;
	}

	return 0;
}

static char fet_buf[65538];
static int fet_len;

#define MAX_PARAMS		16

/* Recieved packet is parsed into this struct */
static struct {
	int		command_code;
	int		state;

	int		argc;
	u_int32_t	argv[MAX_PARAMS];

	char		*data;
	int		datalen;
} fet_reply;

#define BUFFER_BYTE(b, x) ((int)((u_int8_t *)(b))[x])
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

static int parse_packet(int plen)
{
	u_int16_t c = calc_checksum(fet_buf + 2, plen - 2);
	u_int16_t r = BUFFER_WORD(fet_buf, plen);
	int i = 2;
	int type;
	int error;

	if (c != r) {
		fprintf(stderr, "parse_packet: checksum error (calc %04x,"
			" recv %04x)\n", c, r);
		return -1;
	}

	if (plen < 6)
		goto too_short;

	fet_reply.command_code = fet_buf[i++];
	type = fet_buf[i++];
	fet_reply.state = fet_buf[i++];
	error = fet_buf[i++];

	if (error) {
		fprintf(stderr, "parse_packet: FET returned error code %d\n",
			error);
		if (error > 0 && error < ARRAY_LEN(error_strings)) {
			fprintf(stderr, "    (%s)\n", error_strings[error]);
		}
		return -1;
	}

	/* Parse packet parameters */
	if (type == PTYPE_PARAM || type == PTYPE_MIXED) {
		int j;

		if (i + 2 > plen)
			goto too_short;

		fet_reply.argc = BUFFER_WORD(fet_buf, i);
		i += 2;

		if (fet_reply.argc >= MAX_PARAMS) {
			fprintf(stderr, "parse_packet: too many params: %d\n",
				fet_reply.argc);
			return -1;
		}

		for (j = 0; j < fet_reply.argc; j++) {
			if (i + 4 > plen)
				goto too_short;
			fet_reply.argv[j] = BUFFER_LONG(fet_buf, i);
			i += 4;
		}
	} else {
		fet_reply.argc = 0;
	}

	/* Extract a pointer to the data */
	if (type == PTYPE_DATA || type == PTYPE_MIXED) {
		if (i + 4 > plen)
			goto too_short;

		fet_reply.datalen = BUFFER_LONG(fet_buf, i);
		i += 4;

		if (i + fet_reply.datalen > plen)
			goto too_short;

		fet_reply.data = fet_buf + i;
	} else {
		fet_reply.data = NULL;
		fet_reply.datalen = 0;
	}

	return 0;

too_short:
	fprintf(stderr, "parse_packet: too short (%d bytes)\n",
		plen);
	return -1;
}

static int recv_packet(void)
{
	int plen = BUFFER_WORD(fet_buf, 0);

	assert (fet_transport != NULL);

	/* If there's a packet still here from last time, get rid of it */
	if (fet_len >= plen + 2) {
		memmove(fet_buf, fet_buf + plen + 2, fet_len - plen - 2);
		fet_len -= plen + 2;
	}

	/* Keep adding data to the buffer until we have a complete packet */
	for (;;) {
		int len;

		plen = BUFFER_WORD(fet_buf, 0);
		if (fet_len >= plen + 2)
			return parse_packet(plen);

		len = fet_transport->recv(fet_buf + fet_len,
					  sizeof(fet_buf) - fet_len);
		if (len < 0)
			return -1;
		fet_len += len;
	}

	return -1;
}

static int send_command(int command_code,
		        const u_int32_t *params, int nparams,
			const char *extra, int exlen)
{
	char datapkt[256];
	int len = 0;

	char buf[512];
	u_int16_t cksum;
	int i = 0;
	int j;

	assert (len + exlen + 2 <= sizeof(datapkt));
	assert (fet_transport != NULL);

	/* Command code and packet type */
	datapkt[len++] = command_code;
	datapkt[len++] = ((nparams > 0) ? 1 : 0) + ((exlen > 0) ? 2 : 0) + 1;

	/* Optional parameters */
	if (nparams > 0) {
		datapkt[len++] = nparams & 0xff;
		datapkt[len++] = nparams >> 8;

		for (j = 0; j < nparams; j++) {
			u_int32_t p = params[j];

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

	return fet_transport->send(buf, i);
}

static int xfer(int command_code, const char *data, int datalen,
		int nparams, ...)
{
	u_int32_t params[MAX_PARAMS];
	int i;
	va_list ap;

	assert (nparams <= MAX_PARAMS);

	va_start(ap, nparams);
	for (i = 0; i < nparams; i++)
		params[i] = va_arg(ap, unsigned int);
	va_end(ap);

	if (data && fet_is_rf2500) {
		if (send_rf2500_data(data, datalen) < 0)
			return -1;
		if (send_command(command_code, params, nparams, NULL, 0) < 0)
			return -1;
	} else if (send_command(command_code, params, nparams,
				data, datalen) < 0)
		return -1;

	if (recv_packet() < 0)
		return -1;

	if (fet_reply.command_code != command_code) {
		fprintf(stderr, "xfer: reply type mismatch\n");
		return -1;
	}

	return 0;
}

/**********************************************************************
 * MSP430 high-level control functions
 */

static int fet_version;

/* Reply data taken from uif430 */
#define ID_REPLY_LEN	18

static const struct {
	const u_int8_t	reply[ID_REPLY_LEN];
	const char	*idtext;
} id_table[] = {
	{
		.reply = {0xF2, 0x49, 0x02, 0x60, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x02, 0x02, 0x01, 0x00, 0xF3, 0x2B,
			  0x80, 0x00},
		.idtext = "MSP430F249"
	},
	{
		.reply = {0xF1, 0x49, 0x00, 0x43, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x01, 0x10, 0x00, 0x00, 0xF0, 0x1A,
			  0x10, 0x00},
		.idtext = "MSP430F149"
	},
	{
		.reply = {0xF1, 0x6C, 0x20, 0x40, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x01, 0x61, 0x01, 0x00, 0xD1, 0x4D,
			  0x80, 0x00},
		.idtext = "MSP430F1611"
	},
	{
		.reply = {0xf2, 0x27, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x02, 0x01, 0x01, 0x04, 0xb1, 0x62,
			  0x80, 0x00},
		.idtext = "MSP430F2274"
	},
	{
		.reply = {0xf2, 0x01, 0x10, 0x40, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00,
			  0x00, 0x00},
		.idtext = "MSP430F20x3"
	}
};

extern void hexdump(int addr, const char *data, int len);

static int do_identify(void)
{
	int i;

	if (fet_version < 20300000) {
		char idtext[64];

		if (xfer(C_IDENTIFY, NULL, 0, 2, 70, 0) < 0)
			return -1;

		if (!fet_reply.data) {
			fprintf(stderr, "do_indentify: missing info\n");
			return -1;
		}

		memcpy(idtext, fet_reply.data + 4, 32);
		idtext[32] = 0;
		printf("Device is %s\n", idtext);
		return 0;
	}

	if (xfer(40, NULL, 0, 2, 0, 0) < 0)
		return -1;

	if (!fet_reply.data) {
		fprintf(stderr, "do_indentify: missing info\n");
		return -1;
	}

	if (fet_reply.datalen >= ID_REPLY_LEN)
		for (i = 0; i < ARRAY_LEN(id_table); i++)
			if (!memcmp(id_table[i].reply, fet_reply.data,
				    ID_REPLY_LEN)) {
				printf("Device is %s\n", id_table[i].idtext);
				return 0;
			}

	printf("warning: unknown device data:\n");
	hexdump(0, fet_reply.data, fet_reply.datalen);
	return 0;
}

int fet_open(const struct fet_transport *tr, int proto_flags, int vcc_mv)
{
	fet_transport = tr;
	fet_is_rf2500 = proto_flags & FET_PROTO_RF2500;
	init_codes();

	if (xfer(C_INITIALIZE, NULL, 0, 0) < 0) {
		fprintf(stderr, "fet_open: open failed\n");
		return -1;
	}

	fet_version = fet_reply.argv[0];
	printf("FET protocol version is %d\n", fet_version);

	if (xfer(39, NULL, 0, 1, 4) < 0) {
		fprintf(stderr, "fet_open: init failed\n");
		return -1;
	}

	/* configure: Spy-Bi-Wire or JTAG */
	if (xfer(C_CONFIGURE, NULL, 0,
		 2, 8, (proto_flags & FET_PROTO_SPYBIWIRE) ? 1 : 0) < 0) {
		fprintf(stderr, "fet_open: configure failed\n");
		return -1;
	}

	printf("Configured for %s\n",
		(proto_flags & FET_PROTO_SPYBIWIRE) ? "Spy-Bi-Wire" : "JTAG");

	/* Identify the chip */
	if (do_identify() < 0) {
		fprintf(stderr, "fet_open: identify failed\n");
		return -1;
	}

	/* set VCC */
	if (xfer(C_VCC, NULL, 0, 2, vcc_mv, 0) < 0) {
		fprintf(stderr, "fet_open: set VCC failed\n");
		return -1;
	}

	printf("Set Vcc: %d mV\n", vcc_mv);

	/* I don't know what this is, but it appears to halt the MSP. Without
	 * it, memory reads return garbage. This is RF2500-specific.
	 */
	if (fet_is_rf2500 && xfer(0x28, NULL, 0, 2, 0, 0) < 0) {
		fprintf(stderr, "fet_open: command 0x28 failed\n");
		return -1;
	}

	/* Who knows what this is. Without it, register reads don't work.
	 * This is RF2500-specific.
	 */
	if (fet_is_rf2500) {
		static const char data[] = {
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
		};

		if (xfer(0x29, data, sizeof(data), 4, 0, 0x39, 0x31,
			 sizeof(data)) < 0) {
			fprintf(stderr, "fet_open: command 0x29 failed\n");
			return -1;
		}
	}

	return 0;
}

int fet_reset(int flags)
{
	int wh = flags & FET_RESET_HALT ? 0 : 1;
	int wr = flags & FET_RESET_RELEASE ? 1 : 0;

	if (xfer(C_RESET, NULL, 0, 3, flags & FET_RESET_ALL, wh, wr) < 0) {
		fprintf(stderr, "fet_reset: reset failed\n");
		return -1;
	}

	return 0;
}

int fet_close(void)
{
	if (xfer(C_CLOSE, NULL, 0, 1, 0) < 0) {
		fprintf(stderr, "fet_shutdown: close command failed\n");
		return -1;
	}

	fet_transport->close();
	fet_transport = NULL;

	return 0;
}

int fet_get_context(u_int16_t *regs)
{
	int i;

	if (xfer(C_READREGISTERS, NULL, 0, 0) < 0)
		return -1;

	if (fet_reply.datalen < FET_NUM_REGS * 4) {
		fprintf(stderr, "fet_get_context: short reply (%d bytes)\n",
			fet_reply.datalen);
		return -1;
	}

	for (i = 0; i < FET_NUM_REGS; i++)
		regs[i] = BUFFER_WORD(fet_reply.data, i * 4);

	return 0;
}

int fet_set_context(u_int16_t *regs)
{
	char buf[FET_NUM_REGS * 4];
	int i;
	int ret;

	memset(buf, 0, sizeof(buf));

	for (i = 0; i < FET_NUM_REGS; i++) {
		buf[i * 4] = regs[i] & 0xff;
		buf[i * 4 + 1] = regs[i] >> 8;
	}

	if (fet_is_rf2500)
		ret = xfer(C_WRITEREGISTERS, buf, sizeof(buf),
			   2, 0xffff, sizeof(buf));
	else
		ret = xfer(C_WRITEREGISTERS, buf, sizeof(buf),
			   1, 0xffff);

	if (ret < 0) {
		fprintf(stderr, "fet_set_context: context set failed\n");
		return -1;
	}

	return 0;
}

int fet_read_mem(u_int16_t addr, char *buffer, int count)
{
	while (count) {
		int plen = count > 128 ? 128 : count;

		if (xfer(C_READMEMORY, NULL, 0, 2, addr, plen) < 0) {
			fprintf(stderr, "fet_read_mem: failed to read "
				"from 0x%04x\n", addr);
			return -1;
		}

		if (fet_reply.datalen < plen) {
			fprintf(stderr, "fet_read_mem: short data: "
				"%d bytes\n", fet_reply.datalen);
			return -1;
		}

		memcpy(buffer, fet_reply.data, plen);
		buffer += plen;
		count -= plen;
		addr += plen;
	}

	return 0;
}

int fet_write_mem(u_int16_t addr, char *buffer, int count)
{
	while (count) {
		int plen = count > 128 ? 128 : count;
		int ret;

		if (fet_is_rf2500)
			ret = xfer(C_WRITEMEMORY, buffer, plen,
				   2, addr, plen);
		else
			ret = xfer(C_WRITEMEMORY, buffer, plen,
				   1, addr);

		if (ret < 0) {
			fprintf(stderr, "fet_write_mem: failed to write "
				"to 0x%04x\n", addr);
			return -1;
		}

		buffer += plen;
		count -= plen;
		addr += plen;
	}

	return 0;
}

int fet_erase(int type, u_int16_t addr, int len)
{
	if (xfer(C_CONFIGURE, NULL, 0, 2, 2, 0x26) < 0) {
		fprintf(stderr, "fet_erase: config (1) failed\n");
		return -1;
	}

	if (xfer(C_CONFIGURE, NULL, 0, 2, 5, 0) < 0) {
		fprintf(stderr, "fet_erase: config (2) failed\n");
		return -1;
	}

	if (xfer(C_ERASE, NULL, 0, 3, type, addr, len) < 0) {
		fprintf(stderr, "fet_erase: erase command failed\n");
		return -1;
	}

	return 0;
}

int fet_poll(void)
{
	/* Without this delay, breakpoints can get lost. */
	if (usleep(500000) < 0)
		return -1;

	if (xfer(C_STATE, NULL, 0, 1, 0) < 0) {
		fprintf(stderr, "fet_poll: polling failed\n");
		return -1;
	}

	return fet_reply.argv[0];
}

int fet_run(int type)
{
	int wr = type & FET_RUN_RELEASE ? 1 : 0;

	type &= ~FET_RUN_RELEASE;
	if (xfer(C_RUN, NULL, 0, 2, type, wr) < 0) {
		fprintf(stderr, "fet_run: run failed\n");
		return -1;
	}

	return 0;
}

int fet_stop(void)
{
	if (xfer(C_STATE, NULL, 0, 1, 1) < 0) {
		fprintf(stderr, "fet_stop: stop failed\n");
		return -1;
	}

	return 0;
}

int fet_break(int which, u_int16_t addr)
{
	if (xfer(C_BREAKPOINT, NULL, 0, 2, which, addr) < 0) {
		fprintf(stderr, "fet_break: set breakpoint failed\n");
		return -1;
	}

	return 0;
}
