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
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include "bsl.h"
#include "util.h"
#include "fet_error.h"

#ifdef __APPLE__
#define B460800 460800
#endif

struct bsl_device {
	struct device   base;

	int             serial_fd;
	uint8_t         reply_buf[256];
	int             reply_len;
};

#define DATA_HDR	0x80
#define DATA_ACK	0x90
#define DATA_NAK	0xA0

static int bsl_ack(struct bsl_device *dev)
{
	uint8_t reply;

	if (read_with_timeout(dev->serial_fd, &reply, 1) < 0) {
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

static int bsl_sync(struct bsl_device *dev)
{
	static const uint8_t c = DATA_HDR;
	int tries = 2;

	if (tcflush(dev->serial_fd, TCIFLUSH) < 0) {
		perror("bsl: tcflush");
		return -1;
	}

	while (tries--)
		if (!(write_all(dev->serial_fd, &c, 1) || bsl_ack(dev)))
			return 0;

	fprintf(stderr, "bsl: sync failed\n");
	return -1;
}

static int send_command(struct bsl_device *dev,
			int code, uint16_t addr,
			const uint8_t *data, int len)
{
	uint8_t pktbuf[256];
	uint8_t cklow = 0xff;
	uint8_t ckhigh = 0xff;
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

	return write_all(dev->serial_fd, pktbuf, pktlen + 6);
}

static int verify_checksum(struct bsl_device *dev)
{
	uint8_t cklow = 0xff;
	uint8_t ckhigh = 0xff;
	int i;

	for (i = 0; i < dev->reply_len; i += 2)
		cklow ^= dev->reply_buf[i];
	for (i = 1; i < dev->reply_len; i += 2)
		ckhigh ^= dev->reply_buf[i];

	if (cklow || ckhigh) {
		fprintf(stderr, "bsl: checksum invalid (%02x %02x)\n",
			cklow, ckhigh);
		return -1;
	}

	return 0;
}

static int fetch_reply(struct bsl_device *dev)
{
	dev->reply_len = 0;

	for (;;) {
		int r = read_with_timeout(dev->serial_fd,
					  dev->reply_buf + dev->reply_len,
					  sizeof(dev->reply_buf) -
					  dev->reply_len);

		if (r < 0)
			return -1;

		dev->reply_len += r;

		if (dev->reply_buf[0] == DATA_ACK) {
			return 0;
		} else if (dev->reply_buf[0] == DATA_HDR) {
			if (dev->reply_len >= 6 &&
			    dev->reply_len == dev->reply_buf[2] + 6)
				return verify_checksum(dev);
		} else if (dev->reply_buf[0] == DATA_NAK) {
			fprintf(stderr, "bsl: received NAK\n");
			return -1;
		} else {
			fprintf(stderr, "bsl: unknown reply type: %02x\n",
				dev->reply_buf[0]);
			return -1;
		}

		if (dev->reply_len >= sizeof(dev->reply_buf)) {
			fprintf(stderr, "bsl: reply buffer overflow\n");
			return -1;
		}
	}
}

static int bsl_xfer(struct bsl_device *dev,
		    int command_code, uint16_t addr, const uint8_t *txdata,
		    int len)
{
	if (bsl_sync(dev) < 0 ||
	    send_command(dev, command_code, addr, txdata, len) < 0 ||
	    fetch_reply(dev) < 0) {
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

static void bsl_destroy(device_t dev_base)
{
	struct bsl_device *dev = (struct bsl_device *)dev_base;

	bsl_xfer(dev, CMD_RESET, 0, NULL, 0);
	close(dev->serial_fd);
	free(dev);
}

static int bsl_ctl(device_t dev_base, device_ctl_t type)
{
	fprintf(stderr, "bsl: CPU control is not implemented\n");
	return -1;
}

static device_status_t bsl_poll(device_t dev_base)
{
	return DEVICE_STATUS_HALTED;
}

static int bsl_getregs(device_t dev_base, uint16_t *regs)
{
	fprintf(stderr, "bsl: register fetch is not implemented\n");
	return -1;
}

static int bsl_setregs(device_t dev_base, const uint16_t *regs)
{
	fprintf(stderr, "bsl: register store is not implemented\n");
	return -1;
}

static int bsl_writemem(device_t dev_base,
			uint16_t addr, const uint8_t *mem, int len)
{
	fprintf(stderr, "bsl: memory write is not implemented\n");
	return -1;
}

static int bsl_readmem(device_t dev_base,
		       uint16_t addr, uint8_t *mem, int len)
{
	struct bsl_device *dev = (struct bsl_device *)dev_base;

	while (len) {
		int count = len;

		if (count > 128)
			count = 128;

		if (bsl_xfer(dev, CMD_TX_DATA, addr, NULL, count) < 0) {
			fprintf(stderr, "bsl: failed to read memory\n");
			return -1;
		}

		if (count > dev->reply_buf[2])
			count = dev->reply_buf[2];

		memcpy(mem, dev->reply_buf + 4, count);
		mem += count;
		len -= count;
	}

	return 0;
}

static int enter_via_fet(struct bsl_device *dev)
{
	uint8_t buf[16];
	uint8_t *data = buf;
	int len = 8;

	/* Enter bootloader command */
	if (write_all(dev->serial_fd,
		      (uint8_t *)"\x7e\x24\x01\x9d\x5a\x7e", 6)) {
		fprintf(stderr, "bsl: couldn't write bootloader transition "
			"command\n");
		return -1;
	}

	/* Wait for reply */
	while (len) {
		int r = read_with_timeout(dev->serial_fd, data, len);

		if (r < 0) {
			fprintf(stderr, "bsl: couldn't read bootloader "
				"transition acknowledgement\n");
			return -1;
		}

	        data += r;
		len -= r;
	}

	/* Check that it's what we expect */
	if (memcmp(buf, "\x06\x00\x24\x00\x00\x00\x61\x01", 8)) {
		fprintf(stderr, "bsl: bootloader start returned error "
			"%d (%s)\n", buf[5], fet_error(buf[5]));
		return -1;
	}

	return 0;
}

device_t bsl_open(const char *device)
{
	struct bsl_device *dev = malloc(sizeof(*dev));

	if (!dev) {
		perror("bsl: can't allocate memory");
		return NULL;
	}

	memset(dev, 0, sizeof(*dev));

	dev->base.destroy = bsl_destroy;
	dev->base.readmem = bsl_readmem;
	dev->base.writemem = bsl_writemem;
	dev->base.getregs = bsl_getregs;
	dev->base.setregs = bsl_setregs;
	dev->base.ctl = bsl_ctl;
	dev->base.poll = bsl_poll;

	dev->serial_fd = open_serial(device, B460800);
	if (dev->serial_fd < 0) {
		fprintf(stderr, "bsl: can't open %s: %s\n",
			device, strerror(errno));
		free(dev);
		return NULL;
	}

	if (enter_via_fet(dev) < 0)
		return NULL;

	usleep(500000);

	/* Show chip info */
	if (bsl_xfer(dev, CMD_TX_DATA, 0xff0, NULL, 0x10) < 0) {
		fprintf(stderr, "bsl: failed to read chip info\n");
		goto fail;
	}

	if (dev->reply_len < 0x16) {
		fprintf(stderr, "bsl: missing chip info\n");
		goto fail;
	}

	printf("Device ID: 0x%02x%02x\n",
	       dev->reply_buf[4], dev->reply_buf[5]);
	printf("BSL version is %x.%02x\n", dev->reply_buf[14],
	       dev->reply_buf[15]);

	return (device_t)dev;

 fail:
	close(dev->serial_fd);
	free(dev);
	return NULL;
}
