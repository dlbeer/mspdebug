/* MSPDebug - debugging tool for MSP430 MCUs
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

#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "sport.h"
#include "output.h"
#include "goodfet.h"
#include "ctrlc.h"

/* GoodFET protocol definitions */
#define APP_JTAG430		0x11
#define APP_DEBUG		0xFF

#define GLOBAL_READ		0x00
#define GLOBAL_WRITE		0x01
#define GLOBAL_PEEK		0x02
#define GLOBAL_POKE		0x03
#define GLOBAL_SETUP		0x10
#define GLOBAL_START		0x20
#define GLOBAL_STOP		0x21
#define GLOBAL_CALL		0x30
#define GLOBAL_EXEC		0x31
#define GLOBAL_LIMIT		0x7B
#define GLOBAL_EXIST		0x7C
#define GLOBAL_NMEM		0x7D
#define GLOBAL_NOK		0x7E
#define GLOBAL_OK		0x7F
#define GLOBAL_DEBUG		0xFF

#define JTAG430_HALTCPU		0xA0
#define JTAG430_RELEASECPU	0xA1
#define JTAG430_SETINSTRFETCH	0xC1
#define JTAG430_SETPC		0xC2
#define JTAG430_SETREG		0xD2
#define JTAG430_GETREG		0xD3
#define JTAG430_WRITEMEM	0xE0
#define JTAG430_WRITEFLASH	0xE1
#define JTAG430_READMEM		0xE2
#define JTAG430_ERASEFLASH	0xE3
#define JTAG430_ERASECHECK	0xE4
#define JTAG430_VERIFYMEM	0xE5
#define JTAG430_BLOWFUSE	0xE6
#define JTAG430_ISFUSEBLOWN	0xE7
#define JTAG430_ERASEINFO	0xE8
#define JTAG430_COREIP_ID	0xF0
#define JTAG430_DEVICE_ID	0xF1

/* GoodFET packet transfer */
#define MAX_LEN			1024
#define MAX_MEM_BLOCK		128


struct goodfet {
	struct device		base;

	sport_t			serial_fd;
};


/************************************************************************
 * GoodFET protocol handling
 */

struct packet {
	uint8_t		app;
	uint8_t		verb;
	uint16_t	len;
	uint8_t		data[MAX_LEN];
};

static int reset_sequence(sport_t fd)
{
	static const int states[] = {
		SPORT_MC_RTS,
		SPORT_MC_RTS | SPORT_MC_DTR,
		SPORT_MC_DTR
	};
	int i;

	printc_dbg("Resetting GoodFET...\n");

	for (i = 0; i < 3; i++) {
		if (sport_set_modem(fd, states[i]) < 0) {
			printc_err("goodfet: failed at step %d: %s\n",
				   i, last_error());
			return -1;
		}

		delay_ms(20);
	}

	return 0;
}

static int send_packet(sport_t fd,
		       uint8_t app, uint8_t verb, uint16_t len,
		       const uint8_t *data)
{
	uint8_t raw[MAX_LEN + 4];

	if (len > MAX_LEN) {
		printc_err("goodfet: send_packet: maximum length "
			   "exceeded (%d)\n", len);
		return -1;
	}

#ifdef DEBUG_GOODFET
	printc_dbg("SEND: %02x/%02x\n", app, verb);
	if (len)
		debug_hexdump("Data", data, len);
#endif

	raw[0] = app;
	raw[1] = verb;
	raw[2] = len & 0xff;
	raw[3] = len >> 8;

	memcpy(raw + 4, data, len);

	if (sport_write_all(fd, raw, len + 4) < 0) {
		printc_err("goodfet: send_packet: %s\n", last_error());
		return -1;
	}

	return 0;
}

static int recv_packet(sport_t fd, struct packet *pkt)
{
	uint8_t header[4];

	if (sport_read_all(fd, header, 4) < 0) {
		printc_err("goodfet: recv_packet (header): %s\n",
			   last_error());
		return -1;
	}

	pkt->app = header[0];
	pkt->verb = header[1];
	pkt->len = ((uint16_t)header[2]) | (((uint16_t)header[3]) << 8);

	if (pkt->len > MAX_LEN) {
		printc_err("goodfet: recv_packet: maximum length "
			   "exceeded (%d)\n", pkt->len);
		return -1;
	}

	if (sport_read_all(fd, pkt->data, pkt->len) < 0) {
		printc_err("goodfet: recv_packet (data): %s\n",
			   last_error());
		return -1;
	}

#ifdef DEBUG_GOODFET
	printc_dbg("RECV: %02x/%02x\n", pkt->app, pkt->verb);
	if (pkt->len)
		debug_hexdump("Data", pkt->data, pkt->len);
#endif

	return 0;
}

static int xfer(sport_t fd,
		uint8_t app, uint8_t verb, uint16_t len,
		const uint8_t *data, struct packet *pkt)
{
	if (send_packet(fd, app, verb, len, data) < 0)
		goto fail;

	while (recv_packet(fd, pkt) >= 0) {
		if (pkt->app == APP_DEBUG && pkt->verb == GLOBAL_DEBUG) {
			char text[MAX_LEN + 1];

			memcpy(text, pkt->data, pkt->len);
			text[pkt->len] = 0;
			printc_dbg("[GoodFET debug] %s\n", text);
		}

		if (pkt->app == app && pkt->verb == verb)
			return 0;
	}

fail:
	printc_err("goodfet: command 0x%02x/0x%02x "
		   "failed\n", app, verb);
	return -1;
}

/************************************************************************
 * GoodFET MSP430 JTAG operations
 */

/* Read a word-aligned block from any kind of memory.
 * returns the number of bytes read or -1 on failure
 */
static int read_words(device_t dev, const struct chipinfo_memory *m,
		address_t addr, address_t len, uint8_t *data)
{
	struct goodfet *gc = (struct goodfet *)dev;
	sport_t fd = gc->serial_fd;
	struct packet pkt;
	uint8_t req[6];

	if (len > MAX_MEM_BLOCK)
		len = MAX_MEM_BLOCK;

	req[0] = addr;
	req[1] = addr >> 8;
	req[2] = addr >> 16;
	req[3] = addr >> 24;
	req[4] = len;
	req[5] = len >> 8;

	if (xfer(fd, APP_JTAG430, GLOBAL_PEEK, sizeof(req), req, &pkt) < 0) {
		printc_err("goodfet: read %d bytes from 0x%x failed\n",
			   len, addr);
		return -1;
	}

	if (pkt.len != len) {
		printc_err("goodfet: short memory read (got %d, "
			   "expected %d)\n", pkt.len, len);
		return -1;
	}

	memcpy(data, pkt.data, pkt.len);
	return len;
}

/* Write a word to RAM. */
static int write_ram_word(sport_t fd, address_t addr, uint16_t value)
{
	uint8_t req[6];
	struct packet pkt;

	req[0] = addr;
	req[1] = addr >> 8;
	req[2] = 0;
	req[3] = 0;
	req[4] = value;
	req[5] = value >> 8;

	if (xfer(fd, APP_JTAG430, GLOBAL_POKE, sizeof(req), req, &pkt) < 0) {
		printc_err("goodfet: failed to write word at 0x%x\n", addr);
		return -1;
	}

	return 0;
}

/* Write a word-aligned flash block. The starting address must be within
 * the flash memory range.
 */
static int write_flash_block(sport_t fd, address_t addr,
			     address_t len, const uint8_t *data)
{
	uint8_t req[MAX_MEM_BLOCK + 4];
	struct packet pkt;

	req[0] = addr >> 0;
	req[1] = addr >> 8;
	req[2] = addr >> 16;
	req[3] = addr >> 24;
	memcpy(req + 4, data, len);

	if (xfer(fd, APP_JTAG430, JTAG430_WRITEFLASH,
		 len + 4, req, &pkt) < 0) {
		printc_err("goodfet: failed to write "
			   "flash block of size %d at 0x%x\n",
			   len, addr);
		return -1;
	}

	return 0;
}

/* Write a word-aligned block to any kind of memory.
 * returns the number of bytes written or -1 on failure
 */
static int write_words(device_t dev, const struct chipinfo_memory *m,
		address_t addr, address_t len, const uint8_t *data)
{
	struct goodfet *gc = (struct goodfet *)dev;
	sport_t fd = gc->serial_fd;
	int r;

	if (len > MAX_MEM_BLOCK)
		len = MAX_MEM_BLOCK;

	if (m->type != CHIPINFO_MEMTYPE_FLASH) {
		len = 2;
		r = write_ram_word(fd, addr, r16le(data));
	} else {
		r = write_flash_block(fd, addr, len, data);
	}

	if (r < 0) {
		printc_err("goodfet: write_words at address 0x%x failed\n", addr);
		return -1;
	}

	return len;
}

static int init_device(sport_t fd)
{
	struct packet pkt;

	printc_dbg("Initializing...\n");

	if (xfer(fd, APP_JTAG430, GLOBAL_NOK, 0, NULL, &pkt) < 0) {
		printc_err("goodfet: comms test failed\n");
		return -1;
	}

	printc_dbg("Setting up JTAG pins\n");

	if (xfer(fd, APP_JTAG430, GLOBAL_SETUP, 0, NULL, &pkt) < 0) {
		printc_err("goodfet: SETUP command failed\n");
		return -1;
	}

	printc_dbg("Starting JTAG\n");

	if (xfer(fd, APP_JTAG430, GLOBAL_START, 0, NULL, &pkt) < 0) {
		printc_err("goodfet: START command failed\n");
		return -1;
	}

	if (pkt.len < 1) {
		printc_err("goodfet: bad response to JTAG START\n");
		return -1;
	}

	printc("JTAG ID: 0x%02x\n", pkt.data[0]);
	if (pkt.data[0] != 0x89 && pkt.data[0] != 0x91) {
		printc_err("goodfet: unexpected JTAG ID: 0x%02x\n",
			   pkt.data[0]);
		xfer(fd, APP_JTAG430, GLOBAL_STOP, 0, NULL, &pkt);
		return -1;
	}

	printc_dbg("Halting CPU\n");

	if (xfer(fd, APP_JTAG430, JTAG430_HALTCPU, 0, NULL, &pkt) < 0) {
		printc_err("goodfet: HALTCPU command failed\n");
		xfer(fd, APP_JTAG430, GLOBAL_STOP, 0, NULL, &pkt);
		return -1;
	}

	return 0;
}

/************************************************************************
 * MSPDebug Device interface
 */

static int goodfet_readmem(device_t dev_base, address_t addr,
			   uint8_t *mem, address_t len)
{
	return readmem(dev_base, addr, mem, len, read_words);
}

static int goodfet_writemem(device_t dev_base, address_t addr,
			    const uint8_t *mem, address_t len)
{
	return writemem(dev_base, addr, mem, len, write_words, read_words);
}

static int goodfet_setregs(device_t dev_base, const address_t *regs)
{
	(void)dev_base;
	(void)regs;

	printc_err("goodfet: register write not implemented\n");
	return -1;
}

static int goodfet_getregs(device_t dev_base, address_t *regs)
{
	(void)dev_base;
	(void)regs;

	printc_err("goodfet: register read not implemented\n");
	return -1;
}

static int goodfet_reset(struct goodfet *gc)
{
	static const uint8_t cmd_seq[] = {
		JTAG430_RELEASECPU,
		GLOBAL_STOP,
		GLOBAL_START,
		JTAG430_HALTCPU
	};
	int i;

	/* We don't have a POR request, so just restart JTAG */
	for (i = 0; i < 4; i++) {
		struct packet pkt;

		if (xfer(gc->serial_fd, APP_JTAG430, cmd_seq[i],
			 0, NULL, &pkt) < 0) {
			printc_err("goodfet: reset: command 0x%02x failed\n",
				   cmd_seq[i]);
			return -1;
		}
	}

	return 0;
}

static int goodfet_run(struct goodfet *gc)
{
	struct packet pkt;

	if (xfer(gc->serial_fd, APP_JTAG430, JTAG430_RELEASECPU,
		 0, NULL, &pkt) < 0) {
		printc_err("goodfet: failed to release CPU\n");
		return -1;
	}

	return 0;
}

static int goodfet_halt(struct goodfet *gc)
{
	struct packet pkt;

	if (xfer(gc->serial_fd, APP_JTAG430, JTAG430_HALTCPU,
		 0, NULL, &pkt) < 0) {
		printc_err("goodfet: failed to release CPU\n");
		return -1;
	}

	return 0;
}

static int goodfet_ctl(device_t dev_base, device_ctl_t type)
{
	struct goodfet *gc = (struct goodfet *)dev_base;

	switch (type) {
	case DEVICE_CTL_RESET:
		return goodfet_reset(gc);

	case DEVICE_CTL_RUN:
		return goodfet_run(gc);

	case DEVICE_CTL_HALT:
		return goodfet_halt(gc);

	default:
		printc_err("goodfet: unsupported operation\n");
		return -1;
	}

	return 0;
}

static device_status_t goodfet_poll(device_t dev_base)
{
	(void)dev_base;

	if (delay_ms(100) < 0)
		return DEVICE_STATUS_INTR;

	return DEVICE_STATUS_RUNNING;
}

static int goodfet_erase(device_t dev_base, device_erase_type_t type,
			 address_t addr)
{
	struct goodfet *gc = (struct goodfet *)dev_base;
	struct packet pkt;

	if (type != DEVICE_ERASE_MAIN) {
		printc_err("goodfet: only main memory erase is supported\n");
		return -1;
	}

	if (xfer(gc->serial_fd, APP_JTAG430, JTAG430_ERASEFLASH,
		 0, NULL, &pkt) < 0) {
		printc_err("goodfet: erase failed\n");
		return -1;
	}

	return 0;
}

static device_t goodfet_open(const struct device_args *args)
{
	struct goodfet *gc;

	if (!(args->flags & DEVICE_FLAG_TTY)) {
		printc_err("goodfet: this driver does not support raw "
			   "USB access\n");
		return NULL;
	}

	if (!(args->flags & DEVICE_FLAG_JTAG)) {
		printc_err("goodfet: this driver does not support "
			   "Spy-Bi-Wire\n");
		return NULL;
	}

	gc = malloc(sizeof(*gc));
	if (!gc) {
		printc_err("goodfet: malloc: %s\n", last_error());
		return NULL;
	}

        memset(gc, 0, sizeof(*gc));
	gc->base.type = &device_goodfet;
	gc->base.max_breakpoints = 0;
	gc->base.need_probe = 1;

	gc->serial_fd = sport_open(args->path, 115200, 0);
	if (SPORT_ISERR(gc->serial_fd)) {
		printc_err("goodfet: sport_open: %s: %s\n",
			   args->path, last_error());
		free(gc);
		return NULL;
	}

	if ((args->flags & DEVICE_FLAG_FORCE_RESET) &&
	    reset_sequence(gc->serial_fd) < 0)
		printc_err("warning: goodfet: reset failed\n");

	if (sport_flush(gc->serial_fd) < 0)
		printc_err("warning: goodfet: sport_flush: %s\n",
			   last_error());

	if (init_device(gc->serial_fd) < 0) {
		printc_err("goodfet: initialization failed\n");
		free(gc);
		return NULL;
	}

	return &gc->base;
}

static void goodfet_destroy(device_t dev_base)
{
	struct goodfet *gc = (struct goodfet *)dev_base;
	struct packet pkt;

	xfer(gc->serial_fd, APP_JTAG430, JTAG430_RELEASECPU, 0, NULL, &pkt);
	xfer(gc->serial_fd, APP_JTAG430, GLOBAL_STOP, 0, NULL, &pkt);
	sport_close(gc->serial_fd);
	free(gc);
}

const struct device_class device_goodfet = {
	.name		= "goodfet",
	.help		= "GoodFET MSP430 JTAG",
	.open		= goodfet_open,
	.destroy	= goodfet_destroy,
	.readmem	= goodfet_readmem,
	.writemem	= goodfet_writemem,
	.getregs	= goodfet_getregs,
	.setregs	= goodfet_setregs,
	.ctl		= goodfet_ctl,
	.poll		= goodfet_poll,
	.erase		= goodfet_erase,
	.getconfigfuses = NULL
};
