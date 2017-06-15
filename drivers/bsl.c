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
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fet_proto.h"
#include "bsl.h"
#include "util.h"
#include "output.h"
#include "transport.h"
#include "comport.h"
#include "ti3410.h"

struct bsl_device {
	struct device   base;

	transport_t     serial;

	uint8_t         reply_buf[256];
	int             reply_len;
};

#define DATA_HDR	0x80
#define DATA_ACK	0x90
#define DATA_NAK	0xA0

static int bsl_ack(struct bsl_device *dev)
{
	uint8_t reply;

	if (dev->serial->ops->recv(dev->serial, &reply, 1) < 0) {
		printc_err("bsl: failed to receive reply\n");
		return -1;
	}

	if (reply == DATA_NAK) {
		printc_err("bsl: received NAK\n");
		return -1;
	}

	if (reply != DATA_ACK) {
		printc_err("bsl: bad ack character: %x\n", reply);
		return -1;
	}

	return 0;
}

static int bsl_sync(struct bsl_device *dev)
{
	static const uint8_t c = DATA_HDR;
	int tries = 2;

	if (dev->serial->ops->flush(dev->serial) < 0) {
		pr_error("bsl: tcflush");
		return -1;
	}

	while (tries--)
		if (!(dev->serial->ops->send(dev->serial, &c, 1) ||
		      bsl_ack(dev)))
			return 0;

	printc_err("bsl: sync failed\n");
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
		printc_err("bsl: payload too large: %d\n", len);
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

	return dev->serial->ops->send(dev->serial, pktbuf, pktlen + 6);
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
		printc_err("bsl: checksum invalid (%02x %02x)\n",
			cklow, ckhigh);
		return -1;
	}

	return 0;
}

static int fetch_reply(struct bsl_device *dev)
{
	dev->reply_len = 0;

	for (;;) {
		int r = dev->serial->ops->recv(dev->serial,
			       dev->reply_buf + dev->reply_len,
			       sizeof(dev->reply_buf) - dev->reply_len);

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
			printc_err("bsl: received NAK\n");
			return -1;
		} else {
			printc_err("bsl: unknown reply type: %02x\n",
				dev->reply_buf[0]);
			return -1;
		}

		if (dev->reply_len >= sizeof(dev->reply_buf)) {
			printc_err("bsl: reply buffer overflow\n");
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
		printc_err("bsl: failed on command 0x%02x "
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
	dev->serial->ops->destroy(dev->serial);
	free(dev);
}

static int bsl_ctl(device_t dev_base, device_ctl_t type)
{
	(void)dev_base;

	switch (type) {
	case DEVICE_CTL_HALT:
		/* Ignore halt requests */
		return 0;

	case DEVICE_CTL_RESET:
		/* Ignore reset requests */
		return 0;

	default:
		printc_err("bsl: CPU control is not possible\n");
	}

	return -1;
}

static device_status_t bsl_poll(device_t dev_base)
{
	(void)dev_base;

	return DEVICE_STATUS_HALTED;
}

static int bsl_getregs(device_t dev_base, address_t *regs)
{
	(void)dev_base;
	(void)regs;

	printc_err("bsl: register fetch is not implemented\n");
	return -1;
}

static int bsl_setregs(device_t dev_base, const address_t *regs)
{
	(void)dev_base;
	(void)regs;

	printc_err("bsl: register store is not implemented\n");
	return -1;
}

static int bsl_writemem(device_t dev_base,
			address_t addr, const uint8_t *mem, address_t len)
{
	struct bsl_device *dev = (struct bsl_device *)dev_base;

	if (addr >= 0x10000 || len > 0x10000 || addr + len > 0x10000) {
		printc_err("bsl: memory write out of range\n");
		return -1;
	}

	while (len) {
		int wlen = len > 100 ? 100 : len;
		int r;

		r = bsl_xfer(dev, CMD_RX_DATA, addr, mem, wlen);

		if (r < 0) {
			printc_err("bsl: failed to write to 0x%04x\n",
				addr);
			return -1;
		}

		mem += wlen;
		len -= wlen;
		addr += wlen;
	}

	return 0;
}

static int bsl_readmem(device_t dev_base,
		       address_t addr, uint8_t *mem, address_t len)
{
	struct bsl_device *dev = (struct bsl_device *)dev_base;

	if (addr >= 0x10000 || len > 0x10000 || addr + len > 0x10000) {
		printc_err("bsl: memory read out of range\n");
		return -1;
	}

	while (len) {
		address_t count = len;

		if (count > 128)
			count = 128;

		if (bsl_xfer(dev, CMD_TX_DATA, addr, NULL, count) < 0) {
			printc_err("bsl: failed to read memory\n");
			return -1;
		}

		if (count > dev->reply_buf[2])
			count = dev->reply_buf[2];

		memcpy(mem, dev->reply_buf + 4, count);
		mem += count;
		len -= count;
		addr += count;
	}

	return 0;
}

static int bsl_erase(device_t dev_base, device_erase_type_t type,
		     address_t addr)
{
	struct bsl_device *dev = (struct bsl_device *)dev_base;

	(void)addr;

	if (type != DEVICE_ERASE_MAIN) {
		printc_err("bsl: only main erase is supported\n");
		return -1;
	}

	/* Constants found from viewing gdbproxy's activities */
	return bsl_xfer(dev, CMD_ERASE, 0x2500, NULL, 0x0069);
}

static int enter_via_fet(struct bsl_device *dev)
{
	struct fet_proto proto;

	fet_proto_init(&proto, dev->serial, 0);

	if (fet_proto_xfer(&proto, 0x24, NULL, 0, 0) < 0) {
		printc_err("bsl: failed to enter bootloader\n");
		return -1;
	}

	return 0;
}

static device_t bsl_open(const struct device_args *args)
{
	struct bsl_device *dev;

	dev = malloc(sizeof(*dev));
	if (!dev) {
		pr_error("bsl: can't allocate memory");
		return NULL;
	}

	memset(dev, 0, sizeof(*dev));

	dev->base.type = &device_bsl;

	if (args->flags & DEVICE_FLAG_TTY)
		dev->serial = comport_open(args->path, 460800);
	else
		dev->serial = ti3410_open(args->path, args->requested_serial);

	if (!dev->serial) {
		free(dev);
		return NULL;
	}

	if (enter_via_fet(dev) < 0)
		printc_err("bsl: warning: FET firmware not responding\n");

	delay_ms(500);

	/* Show chip info */
	if (bsl_xfer(dev, CMD_TX_DATA, 0xff0, NULL, 0x10) < 0) {
		printc_err("bsl: failed to read chip info\n");
		goto fail;
	}

	if (dev->reply_len < 0x16) {
		printc_err("bsl: missing chip info\n");
		goto fail;
	}

	printc_dbg("Device ID: 0x%02x%02x\n",
	       dev->reply_buf[4], dev->reply_buf[5]);
	printc_dbg("BSL version is %x.%02x\n", dev->reply_buf[14],
	       dev->reply_buf[15]);

	return (device_t)dev;

 fail:
	dev->serial->ops->destroy(dev->serial);
	free(dev);
	return NULL;
}

const struct device_class device_bsl = {
	.name		= "uif-bsl",
	.help		= "TI FET430UIF bootloader.",
	.open		= bsl_open,
	.destroy	= bsl_destroy,
	.readmem	= bsl_readmem,
	.writemem	= bsl_writemem,
	.erase		= bsl_erase,
	.getregs	= bsl_getregs,
	.setregs	= bsl_setregs,
	.ctl		= bsl_ctl,
	.poll		= bsl_poll,
	.getconfigfuses = NULL
};
