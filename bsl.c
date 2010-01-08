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
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include "device.h"
#include "util.h"

static const struct fet_transport *trans;

static int bufio_get_bytes(u_int8_t *data, int len)
{
	while (len) {
		int r = trans->recv(data, len);

		if (r < 0)
			return -1;

		data += r;
		len -= r;
	}

	return 0;
}

#define DATA_HDR	0x80
#define DATA_ACK	0x90
#define DATA_NAK	0xA0

static int bsl_ack(void)
{
	u_int8_t reply;

	if (trans->recv(&reply, 1) < 0) {
		fprintf(stderr, "bsl: failed to receive reply\n");
		return -1;
	}

	if (reply == DATA_NAK) {
		fprintf(stderr, "bsl: received NAK\n");
		return -1;
	}

	if (reply != DATA_ACK) {
		fprintf(stderr, "bsl: bad ack character: %x\n", reply);
		return -1;
	}

	return 0;
}

static int bsl_sync(void)
{
	static const u_int8_t c = DATA_HDR;
	int tries = 2;

	trans->flush();

	while (tries--)
		if (!trans->send(&c, 1) && !bsl_ack())
			return 0;

	fprintf(stderr, "bsl: sync failed\n");
	return -1;
}

static int send_command(int code, u_int16_t addr,
			const u_int8_t *data, int len)
{
	u_int8_t pktbuf[256];
	u_int8_t cklow = 0xff;
	u_int8_t ckhigh = 0xff;
	int pktlen = data ? len + 4 : 4;
	int i;

	if (pktlen + 6 > sizeof(pktbuf)) {
		fprintf(stderr, "bsl: payload too large: %d\n", len);
		return -1;
	}

	pktbuf[0] = DATA_HDR;
	pktbuf[1] = code;
	pktbuf[2] = pktlen;
	pktbuf[3] = pktlen;
	pktbuf[4] = addr & 0xff;
	pktbuf[5] = addr >> 8;
	pktbuf[6] = len & 0xff;
	pktbuf[7] = len >> 8;

	if (data)
		memcpy(pktbuf + 8, data, len);

	for (i = 0; i < pktlen + 4; i += 2)
		cklow ^= pktbuf[i];
	for (i = 1; i < pktlen + 4; i += 2)
		ckhigh ^= pktbuf[i];

	pktbuf[pktlen + 4] = cklow;
	pktbuf[pktlen + 5] = ckhigh;

	return trans->send(pktbuf, pktlen + 6);
}

static u_int8_t reply_buf[256];
static int reply_len;

static int verify_checksum(void)
{
	u_int8_t cklow = 0xff;
	u_int8_t ckhigh = 0xff;
	int i;


	for (i = 0; i < reply_len; i += 2)
		cklow ^= reply_buf[i];
	for (i = 1; i < reply_len; i += 2)
		ckhigh ^= reply_buf[i];

	if (cklow || ckhigh) {
		fprintf(stderr, "bsl: checksum invalid (%02x %02x)\n",
			cklow, ckhigh);
		return -1;
	}

	return 0;
}

static int fetch_reply(void)
{
	reply_len = 0;

	for (;;) {
		int r = trans->recv(reply_buf + reply_len,
				sizeof(reply_buf) - reply_len);

		if (r < 0)
			return -1;

		reply_len += r;

		if (reply_buf[0] == DATA_ACK) {
			return 0;
		} else if (reply_buf[0] == DATA_HDR) {
			if (reply_len >= 6 &&
			    reply_len == reply_buf[2] + 6)
				return verify_checksum();
		} else if (reply_buf[0] == DATA_NAK) {
			fprintf(stderr, "bsl: received NAK\n");
			return -1;
		} else {
			fprintf(stderr, "bsl: unknown reply type: %02x\n",
				reply_buf[0]);
			return -1;
		}

		if (reply_len >= sizeof(reply_buf)) {
			fprintf(stderr, "bsl: reply buffer overflow\n");
			return -1;
		}
	}
}

static int bsl_xfer(int command_code, u_int16_t addr, const u_int8_t *txdata,
		    int len)
{
	if (bsl_sync() < 0 ||
	    send_command(command_code, addr, txdata, len) < 0 ||
	    fetch_reply() < 0) {
		fprintf(stderr, "bsl: failed on command 0x%02x "
			"(addr = 0x%04x, len = 0x%04x)\n",
			command_code, addr, len);
		return -1;
	}

	return 0;
}

#define CMD_TX_DATA		0x38
#define CMD_ERASE		0x39
#define CMD_RX_DATA		0x3a
#define CMD_RESET		0x3b

static void bsl_close(void)
{
	if (trans) {
		bsl_xfer(CMD_RESET, 0, NULL, 0);
		trans->close();
		trans = NULL;
	}
}

static int bsl_control(device_ctl_t type)
{
	fprintf(stderr, "bsl: CPU control is not implemented\n");
	return -1;
}

static int bsl_wait(void)
{
	return 0;
}

static int bsl_breakpoint(u_int16_t addr)
{
	fprintf(stderr, "bsl: breakpoints are not implemented\n");
	return -1;
}

static int bsl_getregs(u_int16_t *regs)
{
	fprintf(stderr, "bsl: register fetch is not implemented\n");
	return -1;
}

static int bsl_setregs(const u_int16_t *regs)
{
	fprintf(stderr, "bsl: register store is not implemented\n");
	return -1;
}

static int bsl_writemem(u_int16_t addr, const u_int8_t *mem, int len)
{
	fprintf(stderr, "bsl: memory write is not implemented\n");
	return -1;
}

static int bsl_readmem(u_int16_t addr, u_int8_t *mem, int len)
{
	while (len) {
		int count = len;

		if (count > 128)
			count = 128;

		if (bsl_xfer(CMD_TX_DATA, addr, NULL, count) < 0) {
			fprintf(stderr, "bsl: failed to read memory\n");
			return -1;
		}

		if (count > reply_buf[2])
			count = reply_buf[2];

		memcpy(mem, reply_buf + 4, count);
		mem += count;
		len -= count;
	}

	return 0;
}

const static struct device bsl_device = {
	.close		= bsl_close,
	.control	= bsl_control,
	.wait		= bsl_wait,
	.breakpoint	= bsl_breakpoint,
	.getregs	= bsl_getregs,
	.setregs	= bsl_setregs,
	.writemem	= bsl_writemem,
	.readmem	= bsl_readmem
};

const struct device *fet_open_bl(const struct fet_transport *tr)
{
	u_int8_t buf[16];

	trans = tr;

	/* Enter bootloader command */
	if (trans->send((u_int8_t *)"\x7e\x24\x01\x9d\x5a\x7e", 6) < 0 ||
	    bufio_get_bytes(buf, 8) < 0) {
		fprintf(stderr, "bsl: failed to init bootloader\n");
		return NULL;
	}

	if (memcmp(buf, "\x06\x00\x24\x00\x00\x00\x61\x01", 8)) {
		fprintf(stderr, "bsl: bootloader start returned error %d\n",
			buf[5]);
		return NULL;
	}

	usleep(500000);

	/* Show chip info */
	if (bsl_xfer(CMD_TX_DATA, 0xff0, NULL, 0x10) < 0) {
		fprintf(stderr, "bsl: failed to read chip info\n");
		return NULL;
	}

	if (reply_len < 0x16) {
		fprintf(stderr, "bsl: missing chip info\n");
		return NULL;
	}

	print_devid((reply_buf[4] << 8) | reply_buf[5]);
	printf("BSL version is %d.%02d\n", reply_buf[16], reply_buf[17]);
	return &bsl_device;
}
