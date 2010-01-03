/* MSPDebug - debugging tool for the eZ430
 * Copyright (C) 2009 Daniel Beer
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
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "fet.h"

static const struct fet_transport *fet_transport;
static int fet_is_rf2500;
static int fet_breakpoint_enable;

/**********************************************************************
 * FET command codes.
 *
 * These come from fet430uif by Robert Kavaler (kavaler@diva.com).
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

#define BUFFER_BYTE(b, x) ((int)((u_int8_t *)(b))[x])
#define BUFFER_WORD(b, x) ((BUFFER_BYTE(b, x + 1) << 8) | BUFFER_BYTE(b, x))

static const char *recv_packet(int *pktlen)
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
		if (fet_len >= plen + 2) {
			u_int16_t c = calc_checksum(fet_buf + 2, plen - 2);
			u_int16_t r = BUFFER_WORD(fet_buf, plen);

			if (pktlen)
				*pktlen = plen - 2;

			if (c != r) {
				fprintf(stderr, "recv_packet: checksum "
					"error (calc %04x, recv %04x)\n",
					c, r);
				return NULL;
			}

			return fet_buf + 2;
		}

		len = fet_transport->recv(fet_buf + fet_len,
					  sizeof(fet_buf) - fet_len);
		if (len < 0)
			return NULL;
		fet_len += len;
	}

	return NULL;
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

static const char *xfer(int command_code,
			const u_int32_t *params, int nparams,
			const char *data, int datalen,
			int *recvlen)
{
	const char *buf;

	if (data && fet_is_rf2500) {
		if (send_rf2500_data(data, datalen) < 0)
			return NULL;
		if (send_command(command_code, params, nparams, NULL, 0) < 0)
			return NULL;
	} else if (send_command(command_code, params, nparams,
				data, datalen) < 0)
		return NULL;

	buf = recv_packet(recvlen);
	if (!buf)
		return NULL;

	if (command_code != buf[0]) {
		fprintf(stderr, "xfer: reply type mismatch\n");
		return NULL;
	}

	return buf;
}

/**********************************************************************
 * MSP430 high-level control functions
 */

int fet_open(const struct fet_transport *tr, int proto_flags, int vcc_mv)
{
	static u_int32_t parm[4];

	fet_transport = tr;
	fet_is_rf2500 = proto_flags & FET_PROTO_RF2500;
	init_codes();

	if (!xfer(C_INITIALIZE, NULL, 0, NULL, 0, NULL)) {
		fprintf(stderr, "fet_open: open failed\n");
		return -1;
	}

	parm[0] = 4;
	if (!xfer(39, parm, 1, NULL, 0, NULL)) {
		fprintf(stderr, "fet_open: init failed\n");
		return -1;
	}

	/* configure: Spy-Bi-Wire or JTAG */
	parm[0] = 8;
	parm[1] = (proto_flags & FET_PROTO_SPYBIWIRE) ? 1 : 0;
	if (!xfer(C_CONFIGURE, parm, 2, NULL, 0, NULL)) {
		fprintf(stderr, "fet_open: configure failed\n");
		return -1;
	}

	parm[0] = 0x50;
	parm[1] = 0;
	if (!fet_is_rf2500 && !xfer(C_IDENTIFY, parm, 2, NULL, 0, NULL)) {
		fprintf(stderr, "fet_open: identify failed\n");
		return -1;
	}

	/* set VCC */
	parm[0] = vcc_mv;
	if (!xfer(C_VCC, parm, 2, NULL, 0, NULL)) {
		fprintf(stderr, "fet_open: set VCC failed\n");
		return -1;
	}

	/* I don't know what this is, but it appears to halt the MSP. Without
	 * it, memory reads return garbage. This is RF2500-specific.
	 */
	parm[0] = 0;
	parm[1] = 0;
	if (fet_is_rf2500 && !xfer(0x28, parm, 2, NULL, 0, NULL)) {
		fprintf(stderr, "fet_open: command 0x28 failed\n");
		return -1;
	}

	/* Who knows what this is. Without it, register reads don't work.
	 * This is RF2500-specific.
	 */
	if (fet_is_rf2500) {
		static char data[] = {
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

		parm[0] = 0;
		parm[1] = 0x39;
		parm[2] = 0x31;
		parm[3] = sizeof(data);

		if (!xfer(0x29, parm, 4, data, sizeof(data), NULL)) {
			fprintf(stderr, "fet_open: command 0x29 failed\n");
			return -1;
		}
	}

	printf("FET initialized: %s (VCC = %d mV)\n",
		(proto_flags & FET_PROTO_SPYBIWIRE) ?
		"Spy-Bi-Wire" : "JTAG", vcc_mv);
	return 0;
}

int fet_reset(int flags)
{
	u_int32_t parm[3] = {
		flags & FET_RESET_ALL,
		flags & FET_RESET_HALT ? 1 : 0,
		flags & FET_RESET_HALT ? 1 : 0
	};

	if (!xfer(C_RESET, parm, 3, NULL, 0, NULL)) {
		fprintf(stderr, "fet_reset: reset failed\n");
		return -1;
	}

	return 0;
}

int fet_close(void)
{
	u_int32_t parm = 0;

	if (!xfer(C_CLOSE, &parm, 1, NULL, 0, NULL)) {
		fprintf(stderr, "fet_shutdown: close command failed\n");
		return -1;
	}

	fet_transport->close();
	fet_transport = NULL;

	return 0;
}

int fet_get_context(u_int16_t *regs)
{
	int len;
	int i;
	const char *buf;

	buf = xfer(C_READREGISTERS, NULL, 0, NULL, 0, &len);
	if (len < 72) {
		fprintf(stderr, "fet_get_context: short reply (%d bytes)\n",
			len);
		return -1;
	}

	for (i = 0; i < FET_NUM_REGS; i++)
		regs[i] = BUFFER_WORD(buf, i * 4 + 8);

	return 0;
}

int fet_set_context(u_int16_t *regs)
{
	char buf[FET_NUM_REGS * 4];
	u_int32_t parm[4] = {
		0xffff,
		sizeof(buf)
	};
	int i;

	memset(buf, 0, sizeof(buf));

	for (i = 0; i < FET_NUM_REGS; i++) {
		buf[i * 4] = regs[i] & 0xff;
		buf[i * 4 + 1] = regs[i] >> 8;
	}

	if (!xfer(C_WRITEREGISTERS, parm, fet_is_rf2500 ? 2 : 1,
		  buf, sizeof(buf), NULL)) {
		fprintf(stderr, "fet_set_context: context set failed\n");
		return -1;
	}

	return 0;
}

int fet_read_mem(u_int16_t addr, char *buffer, int count)
{
	u_int32_t parm[2];

	while (count) {
		int plen = count > 128 ? 128 : count;
		const char *buf;
		int len;

		parm[0] = addr;
		parm[1] = plen;
		buf = xfer(C_READMEMORY, parm, 2, NULL, 0, &len);
		if (!buf) {
			fprintf(stderr, "fet_read_mem: failed to read "
				"from 0x%04x\n", addr);
			return -1;
		}

		if (len < plen + 8) {
			fprintf(stderr, "fet_read_mem: short read "
				"(%d bytes)\n", len);
			return -1;
		}

		memcpy(buffer, buf + 8, plen);
		buffer += plen;
		count -= plen;
		addr += plen;
	}

	return 0;
}

int fet_write_mem(u_int16_t addr, char *buffer, int count)
{
	u_int32_t parm[2];

	while (count) {
		int plen = count > 128 ? 128 : count;

		parm[0] = addr;
		parm[1] = plen;
		if (!xfer(C_WRITEMEMORY, parm, fet_is_rf2500 ? 2 : 1,
			  buffer, plen, NULL)) {
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

int fet_erase(int type, u_int16_t addr)
{
	u_int32_t parm[3];

	parm[0] = 2;
	parm[1] = 0x26;
	if (!xfer(C_CONFIGURE, parm, 2, NULL, 0, NULL)) {
		fprintf(stderr, "fet_erase: config (1) failed\n");
		return -1;
	}

	parm[0] = 5;
	parm[1] = 0;
	if (!xfer(C_CONFIGURE, parm, 2, NULL, 0, NULL)) {
		fprintf(stderr, "fet_erase: config (2) failed\n");
		return -1;
	}

	switch (type) {
	case FET_ERASE_MAIN:
		parm[0] = 1;
		parm[2] = 2;
		break;

	case FET_ERASE_ADDR:
		parm[0] = 0;
		parm[2] = 2;
		break;

	case FET_ERASE_INFO:
		parm[0] = 0;
		parm[2] = 1;
		break;

	case FET_ERASE_ALL:
	default:
		parm[0] = 2;
		parm[2] = 0x100;
		break;
	}

	parm[1] = addr;
	if (!xfer(C_ERASE, parm, 2, NULL, 0, NULL)) {
		fprintf(stderr, "fet_erase: erase command failed\n");
		return -1;
	}

	return 0;
}

int fet_poll(void)
{
	const char *reply;
	int len;
	u_int32_t parm = 0;

	/* Without this delay, breakpoints can get lost. */
	if (usleep(500000) < 0)
		return -1;

	reply = xfer(C_STATE, &parm, 1, NULL, 0, &len);
	if (!reply) {
		fprintf(stderr, "fet_poll: polling failed\n");
		return -1;
	}

	return reply[6];
}

int fet_step(void)
{
	static const u_int32_t parm[2] = {
		2, 0
	};

	if (!xfer(C_RUN, parm, 2, NULL, 0, NULL)) {
		fprintf(stderr, "fet_step: failed to single-step\n");
		return -1;
	}

	return 0;
}

int fet_run(void)
{
	u_int32_t parm[2] = {
		fet_breakpoint_enable ? 3 : 1, 0
	};

	if (!xfer(C_RUN, parm, 2, NULL, 0, NULL)) {
		fprintf(stderr, "fet_run: run failed\n");
		return -1;
	}

	return 0;
}

int fet_stop(void)
{
	u_int32_t parm = 1;

	if (!xfer(C_STATE, &parm, 1, NULL, 0, NULL)) {
		fprintf(stderr, "fet_stop: stop failed\n");
		return -1;
	}

	return 0;
}

int fet_break(int enable, u_int16_t addr)
{
	u_int32_t parm[2] = { 0, addr };

	fet_breakpoint_enable = enable;
	if (!xfer(C_BREAKPOINT, parm, 2, NULL, 0, NULL)) {
		fprintf(stderr, "fet_break: set breakpoint failed\n");
		return -1;
	}

	return 0;
}
