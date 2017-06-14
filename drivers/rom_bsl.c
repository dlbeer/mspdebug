/* MSPDebug - debugging tool for the eZ430
 * Copyright (C) 2009-2014 Daniel Beer
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

#include "rom_bsl.h"
#include "util.h"
#include "output.h"
#include "bsllib.h"
#include "util/sport.h"

struct rom_bsl_device {
	struct device   base;

	sport_t		fd;
	const char	*seq;

	uint8_t         reply_buf[256];
	int             reply_len;
};

#define DATA_HDR	0x80
#define DATA_ACK	0x90
#define DATA_NAK	0xA0

static int rom_bsl_ack(struct rom_bsl_device *dev)
{
	uint8_t reply;

	if (sport_read_all(dev->fd, &reply, 1) < 0) {
		pr_error("rom_bsl: failed to receive reply");
		return -1;
	}

	if (reply == DATA_NAK) {
		printc_err("rom_bsl: received NAK\n");
		return -1;
	}

	if (reply != DATA_ACK) {
		printc_err("rom_bsl: bad ack character: %x\n", reply);
		return -1;
	}

	return 0;
}

static int rom_bsl_sync(struct rom_bsl_device *dev)
{
	static const uint8_t c = DATA_HDR;
	int tries = 2;

	if (sport_flush(dev->fd) < 0) {
		pr_error("rom_bsl: tcflush");
		return -1;
	}

	while (tries--) {
		if (sport_write_all(dev->fd, &c, 1) < 0) {
			pr_error("rom_bsl: write error");
			continue;
		}

		if (!rom_bsl_ack(dev))
			return 0;
	}

	printc_err("rom_bsl: sync failed\n");
	return -1;
}

static int send_command(struct rom_bsl_device *dev,
			int code, uint16_t addr,
			const uint8_t *data, int len)
{
	uint8_t pktbuf[256];
	uint8_t cklow = 0xff;
	uint8_t ckhigh = 0xff;
	int pktlen;
	int evenlen = len;
	int i;

	if (len % 2 != 0) {
	    printc_dbg("Making length even\n");
	    evenlen = len + 1;
	}

	pktlen = data ? evenlen + 4 : 4;

	if (pktlen + 6 > sizeof(pktbuf)) {
		printc_err("rom_bsl: payload too large: %d\n", len);
		return -1;
	}

	pktbuf[0] = DATA_HDR;
	pktbuf[1] = code;
	pktbuf[2] = pktlen;
	pktbuf[3] = pktlen;
	pktbuf[4] = addr & 0xff;
	pktbuf[5] = addr >> 8;
	pktbuf[6] = evenlen & 0xff;
	pktbuf[7] = evenlen >> 8;

	if (data) {
		memcpy(pktbuf + 8, data, len);
		if (len != evenlen)
		    pktbuf[8 + len] = 0xff;
	}

	for (i = 0; i < pktlen + 4; i += 2)
		cklow ^= pktbuf[i];
	for (i = 1; i < pktlen + 4; i += 2)
		ckhigh ^= pktbuf[i];

	pktbuf[pktlen + 4] = cklow;
	pktbuf[pktlen + 5] = ckhigh;

#ifdef DEBUG_ROM_BSL
	debug_hexdump("Send", pktbuf, pktlen + 6);
#endif

	if (sport_write_all(dev->fd, pktbuf, pktlen + 6) < 0) {
		pr_error("rom_bsl: write error");
		return -1;
	}

	return 0;
}

static int verify_checksum(struct rom_bsl_device *dev)
{
	uint8_t cklow = 0xff;
	uint8_t ckhigh = 0xff;
	int i;

	for (i = 0; i < dev->reply_len; i += 2)
		cklow ^= dev->reply_buf[i];
	for (i = 1; i < dev->reply_len; i += 2)
		ckhigh ^= dev->reply_buf[i];

	if (cklow || ckhigh) {
		printc_err("rom_bsl: checksum invalid (%02x %02x)\n",
			cklow, ckhigh);
		return -1;
	}

	return 0;
}

static int fetch_reply(struct rom_bsl_device *dev)
{
	dev->reply_len = 0;

	for (;;) {
		int r = sport_read(dev->fd,
			       dev->reply_buf + dev->reply_len,
			       sizeof(dev->reply_buf) - dev->reply_len);

		if (!r) {
			printc_err("rom_bsl: read timeout\n");
			return -1;
		}

		if (r < 0) {
			pr_error("rom_bsl: read error");
			return -1;
		}

#ifdef DEBUG_ROM_BSL
		debug_hexdump("Receive", dev->reply_buf + dev->reply_len, r);
#endif
		dev->reply_len += r;

		if (dev->reply_buf[0] == DATA_ACK) {
			return 0;
		} else if (dev->reply_buf[0] == DATA_HDR) {
			if (dev->reply_len >= 6 &&
			    dev->reply_len == dev->reply_buf[2] + 6)
				return verify_checksum(dev);
		} else if (dev->reply_buf[0] == DATA_NAK) {
			printc_err("rom_bsl: received NAK\n");
			return -1;
		} else {
			printc_err("rom_bsl: unknown reply type: %02x\n",
				dev->reply_buf[0]);
			return -1;
		}

		if (dev->reply_len >= sizeof(dev->reply_buf)) {
			printc_err("rom_bsl: reply buffer overflow\n");
			return -1;
		}
	}
}

static int rom_bsl_xfer(struct rom_bsl_device *dev,
		    int command_code, uint16_t addr, const uint8_t *txdata,
		    int len)
{
	if (rom_bsl_sync(dev) < 0 ||
	    send_command(dev, command_code, addr, txdata, len) < 0 ||
	    fetch_reply(dev) < 0) {
		printc_err("rom_bsl: failed on command 0x%02x "
			"(addr = 0x%04x, len = 0x%04x)\n",
			command_code, addr, len);
		return -1;
	}

	return 0;
}

#define CMD_MASS_ERASE		0x18
#define CMD_ERASE_SEGMENT	0x16
#define CMD_TX_DATA		0x14
#define CMD_RX_DATA		0x12
#define CMD_TX_VERSION		0x1e
#define CMD_RX_PASSWORD		0x10

static void rom_bsl_destroy(device_t dev_base)
{
	struct rom_bsl_device *dev = (struct rom_bsl_device *)dev_base;

	if (bsllib_seq_do(dev->fd, bsllib_seq_next(dev->seq)) < 0)
		pr_error("warning: rom_bsl: exit sequence failed");

	sport_close(dev->fd);
	free(dev);
}

static int rom_bsl_ctl(device_t dev_base, device_ctl_t type)
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
		printc_err("rom_bsl: CPU control is not possible\n");
	}

	return -1;
}

static device_status_t rom_bsl_poll(device_t dev_base)
{
	(void)dev_base;

	return DEVICE_STATUS_HALTED;
}

static int rom_bsl_getregs(device_t dev_base, address_t *regs)
{
	(void)dev_base;
	(void)regs;

	printc_err("rom_bsl: register fetch is not implemented\n");
	return -1;
}

static int rom_bsl_setregs(device_t dev_base, const address_t *regs)
{
	(void)dev_base;
	(void)regs;

	printc_err("rom_bsl: register store is not implemented\n");
	return -1;
}

static int rom_bsl_writemem(device_t dev_base,
			address_t addr, const uint8_t *mem, address_t len)
{
	struct rom_bsl_device *dev = (struct rom_bsl_device *)dev_base;

	if (addr >= 0x10000 || len > 0x10000 || addr + len > 0x10000) {
		printc_err("rom_bsl: memory write out of range\n");
		return -1;
	}

	while (len) {
		int wlen = len > 100 ? 100 : len;
		int r;
		uint8_t memtmp[256];
		const uint8_t *memptr;

		if (addr % 2) {
		    printc_dbg("Memory aligning\n");
		    memcpy(memtmp + 1, mem, wlen);
		    memtmp[0] = 0xff;
		    memptr = memtmp;

		    wlen++;
		    len++;
		    addr--;
		    mem--;
		}
		else {
		    memptr = mem;
		}

		r = rom_bsl_xfer(dev, CMD_RX_DATA, addr, memptr, wlen);

		if (r < 0) {
			printc_err("rom_bsl: failed to write to 0x%04x\n",
				addr);
			return -1;
		}

		mem += wlen;
		len -= wlen;
		addr += wlen;
	}

	return 0;
}

static int rom_bsl_readmem(device_t dev_base,
		       address_t addr, uint8_t *mem, address_t len)
{
	struct rom_bsl_device *dev = (struct rom_bsl_device *)dev_base;

	if (addr >= 0x10000 || len > 0x10000 || addr + len > 0x10000) {
		printc_err("rom_bsl: memory read out of range\n");
		return -1;
	}

	while (len) {
		address_t count = len;
		int align = 0;

		if (addr % 2 != 0) {
		    printc_dbg("Memory aligning\n");
		    count++;
		    addr--;
		    align = 1;
		}

		if (count > 220)
			count = 220;

		if (rom_bsl_xfer(dev, CMD_TX_DATA, addr, NULL, count) < 0) {
			printc_err("rom_bsl: failed to read memory\n");
			return -1;
		}

		if (count > dev->reply_buf[2])
			count = dev->reply_buf[2];

		memcpy(mem, dev->reply_buf + 4 + align, count - align);
		mem += (count - align);
		len -= (count - align);
		addr += (count - align);
	}

	return 0;
}

static int rom_bsl_erase(device_t dev_base, device_erase_type_t type,
			 address_t addr)
{
	struct rom_bsl_device *dev = (struct rom_bsl_device *)dev_base;

	(void)addr;

	switch (type) {
	case DEVICE_ERASE_MAIN:
		return rom_bsl_xfer(dev, CMD_ERASE_SEGMENT,
				    0xfffe, NULL, 0xa504);

	case DEVICE_ERASE_SEGMENT:
		return rom_bsl_xfer(dev, CMD_ERASE_SEGMENT,
				    addr, NULL, 0xa502);

	case DEVICE_ERASE_ALL:
		return rom_bsl_xfer(dev, CMD_MASS_ERASE,
				    0xfffe, NULL, 0xa506);
	}

	return 0;
}

static int unlock_device(struct rom_bsl_device *dev)
{
	const static uint8_t password[32] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	};

	printc_dbg("Performing mass erase...\n");

	if (rom_bsl_xfer(dev, CMD_MASS_ERASE, 0xfffe, NULL, 0xa506) < 0) {
		printc_err("rom_bsl: initial mass erase failed\n");
		return -1;
	}

	printc_dbg("Sending password...\n");

	if (rom_bsl_xfer(dev, CMD_RX_PASSWORD, 0,
			 password, sizeof(password)) < 0) {
		printc_err("rom_bsl: RX password failed\n");
		return -1;
	}

	return 0;
}

static device_t rom_bsl_open(const struct device_args *args)
{
	struct rom_bsl_device *dev;

	if (!(args->flags & DEVICE_FLAG_TTY)) {
		printc_err("rom_bsl: raw USB access is not supported");
		return NULL;
	}

	dev = malloc(sizeof(*dev));
	if (!dev) {
		pr_error("rom_bsl: can't allocate memory");
		return NULL;
	}

	memset(dev, 0, sizeof(*dev));

	dev->base.type = &device_rom_bsl;

	dev->fd = sport_open(args->path, 9600, SPORT_EVEN_PARITY);
	if (SPORT_ISERR(dev->fd)) {
		pr_error("sport_open");
		free(dev);
		return NULL;
	}

	dev->seq = args->bsl_entry_seq;
	if (!dev->seq)
		dev->seq = "DR,r,R,r,d,R:DR,r";

	if ( args->bsl_gpio_used )
	{
		if (bsllib_seq_do_gpio(args->bsl_gpio_rts, args->bsl_gpio_dtr, dev->seq) < 0) {
			pr_error("rom_bsl: entry sequence failed");
			goto fail;
		}
	}
	else
	{
		if (bsllib_seq_do(dev->fd, dev->seq) < 0) {
			pr_error("rom_bsl: entry sequence failed");
			goto fail;
		}
	}

	delay_ms(500);

	/* Show BSL version */
	if (rom_bsl_xfer(dev, CMD_TX_VERSION, 0, NULL, 0) < 0)
		printc_err("warning: rom_bsl: failed to read version\n");
	else if (dev->reply_len < 19)
		printc_err("warning: rom_bsl: short reply\n");
	else
		printc_dbg("BSL version is %x.%02x\n",
			   dev->reply_buf[15],
			   dev->reply_buf[16]);

	if (unlock_device(dev) < 0) {
		printc_err("rom_bsl: failed to unlock\n");
		goto fail;
	}

	return (device_t)dev;

 fail:
	sport_close(dev->fd);
	free(dev);
	return NULL;
}

const struct device_class device_rom_bsl = {
	.name		= "rom-bsl",
	.help		= "ROM bootstrap loader",
	.open		= rom_bsl_open,
	.destroy	= rom_bsl_destroy,
	.readmem	= rom_bsl_readmem,
	.writemem	= rom_bsl_writemem,
	.erase		= rom_bsl_erase,
	.getregs	= rom_bsl_getregs,
	.setregs	= rom_bsl_setregs,
	.ctl		= rom_bsl_ctl,
	.poll		= rom_bsl_poll,
	.getconfigfuses = NULL
};
