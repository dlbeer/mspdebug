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
static int fet_send_data(const char *data, int len)
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

static const char *fet_recv_packet(int *pktlen)
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
				fprintf(stderr, "fet_fecv_packet: checksum "
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

static int fet_send_command(const char *data, int len)
{
	char datapkt[256];
	char buf[256];
	u_int16_t cksum = calc_checksum(data, len);
	int i = 0;
	int j;

	assert (len + 4 <= sizeof(buf));
	assert (len + 2 <= sizeof(datapkt));
	assert (fet_transport != NULL);

	memcpy(datapkt, data, len);
	datapkt[len++] = cksum & 0xff;
	datapkt[len++] = cksum >> 8;

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

static const char *fet_send_recv(const char *data, int len, int *recvlen)
{
	const char *buf;

	if (fet_send_command(data, len) < 0)
		return NULL;

	buf = fet_recv_packet(recvlen);
	if (!buf)
		return NULL;

	if (data[0] != buf[0]) {
		fprintf(stderr, "fet_send_recv: reply type mismatch\n");
		return NULL;
	}

	return buf;
}

/**********************************************************************
 * MSP430 high-level control functions
 */

int fet_open(const struct fet_transport *tr, int proto_flags, int vcc_mv)
{
        static char config[12] = {
		0x05, 0x02, 0x02, 0x00, 0x08, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00
	};

        static char vcc[8] = {
		0x06, 0x02, 0x01, 0x00, 0xff, 0xff, 0x00, 0x00
	};

	fet_transport = tr;
	init_codes();

	/* open */
	if (!fet_send_recv("\x01\x01", 2, NULL)) {
		fprintf(stderr, "fet_startup: open failed\n");
		return -1;
	}

	/* init */
	if (!fet_send_recv("\x27\x02\x01\x00\x04\x00\x00\x00\x00", 8, NULL)) {
		fprintf(stderr, "fet_startup: init failed\n");
		return -1;
	}

	/* configure: Spy-Bi-Wire or JTAG */
	config[8] = (proto_flags & FET_PROTO_SPYBIWIRE) ? 1 : 0;
	if (!fet_send_recv(config, 12, NULL)) {
		fprintf(stderr, "fet_startup: configure failed\n");
		return -1;
	}

	/* I don't know what this is. It's RF2500-specific. It may have
	 * something to do with flash -- 0x1d is sent before an erase.
	 */
	if (!fet_send_recv("\x1e\x01", 2, NULL)) {
		fprintf(stderr, "fet_startup: command 0x1e failed\n");
		return -1;
	}

	/* set VCC */
	vcc[4] = vcc_mv & 0xff;
	vcc[5] = vcc_mv >> 8;
	if (!fet_send_recv(vcc, 8, NULL)) {
		fprintf(stderr, "fet_startup: set VCC failed\n");
		return -1;
	}

	/* I don't know what this is, but it appears to halt the MSP. Without
	 * it, memory reads return garbage. This is RF2500-specific.
	 */
	if (!fet_send_recv("\x28\x02\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00",
			   12, NULL)) {
		fprintf(stderr, "fet_startup: command 0x28 failed\n");
		return -1;
	}

	/* Who knows what this is. Without it, register reads don't work.
	 * This is RF2500-specific.
	 */
	{
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
			0xff, 0xff,
		};

		if (fet_send_data(data, sizeof(data)) < 0 ||
		    !fet_send_recv("\x29\x02\x04\x00\x00\x00\x00\x00"
				   "\x39\x00\x00\x00\x31\x00\x00\x00"
			           "\x4a\x00\x00\x00", 20, NULL)) {
			fprintf(stderr, "fet_startup: command 0x29 failed\n");
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
	static char reset[] = {
		0x07, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
	        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};

	reset[4] = flags & FET_RESET_ALL;
	if (flags & FET_RESET_HALT) {
		reset[8] = 0;
		reset[12] = 0;
	} else {
		reset[8] = 1;
		reset[12] = 1;
	}

	if (!fet_send_recv(reset, 16, NULL)) {
		fprintf(stderr, "fet_reset: reset failed\n");
		return -1;
	}

	return 0;
}

int fet_close(void)
{
	if (!fet_send_recv("\x02\x02\x01\x00", 4, NULL)) {
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

	buf = fet_send_recv("\x08\x01", 2, &len);
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
	int i;

	memset(buf, 0, sizeof(buf));

	for (i = 0; i < FET_NUM_REGS; i++) {
		buf[i * 4] = regs[i] & 0xff;
		buf[i * 4 + 1] = regs[i] >> 8;
	}

	if (fet_send_data(buf, sizeof(buf)) < 0 ||
	    !fet_send_recv("\x09\x02\x02\x00\xff\xff\x00\x00"
			   "\x40\x00\x00\x00", 12, NULL)) {
		fprintf(stderr, "fet_set_context: context set failed\n");
		return -1;
	}

	return 0;
}

int fet_read_mem(u_int16_t addr, char *buffer, int count)
{
	while (count) {
		int plen = count > 128 ? 128 : count;
		const char *buf;
		int len;

		static char readmem[] = {
			0x0d, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00
		};

		readmem[4] = addr & 0xff;
		readmem[5] = addr >> 8;
		readmem[8] = plen;

		buf = fet_send_recv(readmem, 12, &len);
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
	while (count) {
		int plen = count > 128 ? 128 : count;

		static char writemem[] = {
			0x0e, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00
		};

		writemem[4] = addr & 0xff;
		writemem[5] = addr >> 8;
		writemem[8] = plen;

		if (fet_send_data(buffer, plen) < 0 ||
		    !fet_send_recv(writemem, 12, NULL)) {
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
	static char erase[] = {
		0x0c, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};

	switch (type) {
	case FET_ERASE_MAIN:
		erase[4] = 1;
		erase[8] = 0xe0;
		erase[9] = 0xff;
		erase[12] = 2;
		break;

	case FET_ERASE_ADDR:
		erase[8] = addr & 0xff;
		erase[9] = addr >> 8;
		erase[12] = 2;
		break;

	case FET_ERASE_INFO:
		erase[9] = 0x10;
		erase[13] = 1;
		break;

	case FET_ERASE_ALL:
	default:
		erase[4] = 2;
		erase[9] = 0x10;
		erase[13] = 0x01;
		break;
	}

	if (!fet_send_recv("\x1d\x01", 2, NULL)) {
		fprintf(stderr, "fet_erase: command 1d failed\n");
		return -1;
	}

	if (!fet_send_recv("\x05\x02\x02\x00\x02\x00\x00\x00\x26\x00\x00\x00",
			   12, NULL)) {
		fprintf(stderr, "fet_erase: config (1) failed\n");
		return -1;
	}

	if (!fet_send_recv("\x05\x02\x02\x00\x05\x00\x00\x00\x00\x00\x00\x00",
			   12, NULL)) {
		fprintf(stderr, "fet_erase: config (2) failed\n");
		return -1;
	}

	if (!fet_send_recv(erase, 16, NULL)) {
		fprintf(stderr, "fet_erase: erase command failed\n");
		return -1;
	}

	return 0;
}

int fet_poll(void)
{
	const char *reply;
	int len;

	/* Without this delay, breakpoints can get lost. */
	if (usleep(500000) < 0)
		return -1;

	reply = fet_send_recv("\x12\x02\x01\x00\x00\x00\x00\x00", 8, &len);
	if (!reply) {
		fprintf(stderr, "fet_poll: polling failed\n");
		return -1;
	}

	return reply[6];
}

int fet_step(void)
{
	if (!fet_send_recv("\x11\x02\x02\x00\x02\x00\x00\x00\x00\x00\x00\x00",
			12, NULL)) {
		fprintf(stderr, "fet_step: failed to single-step\n");
		return -1;
	}

	return 0;
}

int fet_run(void)
{
	if (!fet_send_recv("\x11\x02\x02\x00\x03\x00\x00\x00\x00\x00\x00\x00",
			   12, NULL)) {
		fprintf(stderr, "fet_run: run failed\n");
		return -1;
	}

	return 0;
}

int fet_stop(void)
{
	if (!fet_send_recv("\x12\x02\x01\x00\x01\x00\x00\x00", 8, NULL)) {
		fprintf(stderr, "fet_stop: stop failed\n");
		return -1;
	}

	return 0;
}

int fet_break(int enable, u_int16_t addr)
{
	static char buf[] = {
		0x06, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
		0x08, 0x00, 0x00, 0x00, 0x14, 0x80, 0x00, 0x00,
		0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff,
		0x0e, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
		0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x80, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
		0x98, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00
	};

	if (enable) {
		buf[12] = addr & 0xff;
		buf[13] = addr >> 8;
		buf[30] = 0xff;
		buf[31] = 0xff;
		buf[36] = 2;
		buf[52] = 3;
	} else {
		buf[12] = 0;
		buf[13] = 0;
		buf[30] = 0;
		buf[31] = 0;
		buf[36] = 0;
		buf[52] = 1;
	}

	if (fet_send_data(buf, sizeof(buf)) < 0 ||
	    !fet_send_recv("\x2a\x02\x04\x00\x08\x00\x00\x00\xb0\x00\x00\x00"
			   "\x00\x00\x00\x00\x40\x00\x00\x00", 20, NULL)) {
		fprintf(stderr, "fet_break: set breakpoint failed\n");
		return -1;
	}

	return 0;
}

