/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2011 Daniel Beer
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
#include <unistd.h>

#include "sockets.h"
#include "output.h"
#include "gdbc.h"
#include "gdb_proto.h"
#include "opdb.h"
#include "util.h"
#include "ctrlc.h"

struct gdb_client {
	struct device			base;
	struct gdb_data			gdb;
	int				is_running;

	struct device_breakpoint	last_bps[DEVICE_MAX_BREAKPOINTS];
};

static int get_xfer_size(void)
{
	int x = opdb_get_numeric("gdbc_xfer_size");

	if (x < 2)
		return 2;

	if (x > GDB_MAX_XFER)
		return GDB_MAX_XFER;

	return x;
}

static int check_ok(struct gdb_data *gdb)
{
	char buf[GDB_BUF_SIZE];
	int len;

	len = gdb_read_packet(gdb, buf);
	if (len < 0)
		return -1;

	if (len < 1 || buf[0] == 'E') {
		printc_err("gdbc: bad response: %s\n", buf);
		return -1;
	}

	return 0;
}

static void gdbc_destroy(device_t dev_base)
{
	struct gdb_client *c = (struct gdb_client *)dev_base;

	shutdown(c->gdb.sock, 2);
	closesocket(c->gdb.sock);
	free(c);
}

static int gdbc_readmem(device_t dev_base, address_t addr,
		       uint8_t *mem, address_t len)
{
	struct gdb_client *dev = (struct gdb_client *)dev_base;
	int xfer_size = get_xfer_size();
	char buf[GDB_BUF_SIZE];

	while (len) {
		int plen = len > xfer_size ? xfer_size : len;
		int r;
		int i;

		gdb_packet_start(&dev->gdb);
		gdb_printf(&dev->gdb, "m%04x,%x", addr, plen);
		gdb_packet_end(&dev->gdb);
		if (gdb_flush_ack(&dev->gdb) < 0)
			return -1;

		r = gdb_read_packet(&dev->gdb, buf);
		if (r < 0)
			return -1;

		if (r < plen * 2) {
			printc_err("gdbc: short read at 0x%04x: expected %d "
				   "bytes, got %d\n", addr, plen, r / 2);
			return -1;
		}

		for (i = 0; i * 2 < r; i++)
			mem[i] = (hexval(buf[i * 2]) << 4) |
				  hexval(buf[i * 2 + 1]);

		mem += plen;
		len -= plen;
		addr += plen;
	}

	return 0;
}

static int gdbc_writemem(device_t dev_base, address_t addr,
			const uint8_t *mem, address_t len)
{
	struct gdb_client *dev = (struct gdb_client *)dev_base;
	int xfer_size = get_xfer_size();

	while (len) {
		int plen = len > xfer_size ? xfer_size : len;
		int i;

		gdb_packet_start(&dev->gdb);
		gdb_printf(&dev->gdb, "M%04x,%x:", addr, plen);
		for (i = 0; i < plen; i++)
			gdb_printf(&dev->gdb, "%02x", mem[i]);
		gdb_packet_end(&dev->gdb);
		if (gdb_flush_ack(&dev->gdb) < 0)
			return -1;

		if (check_ok(&dev->gdb) < 0)
			return -1;

		mem += plen;
		len -= plen;
		addr += plen;
	}

	return 0;
}

static int gdbc_getregs(device_t dev_base, address_t *regs)
{
	struct gdb_client *dev = (struct gdb_client *)dev_base;
	char buf[GDB_BUF_SIZE];
	int len;
	int i;

	if (gdb_send(&dev->gdb, "g") < 0)
		return -1;

	len = gdb_read_packet(&dev->gdb, buf);
	if (len < 0)
		return -1;

	if (len < DEVICE_NUM_REGS * 4) {
		printc_err("gdbc: short read: expected %d chars, got %d\n",
			   DEVICE_NUM_REGS * 4, len);
		return -1;
	}

	for (i = 0; i < DEVICE_NUM_REGS; i++) {
		char *text = buf + i * 4;

		regs[i] = (hexval(text[0]) << 4) | (hexval(text[1])) |
			  (hexval(text[2]) << 12) | (hexval(text[3]) << 8);
	}

	return 0;
}

static int gdbc_setregs(device_t dev_base, const address_t *regs)
{
	struct gdb_client *dev = (struct gdb_client *)dev_base;
	int i;

	gdb_packet_start(&dev->gdb);
	gdb_printf(&dev->gdb, "G");
	for (i = 0; i < DEVICE_NUM_REGS; i++)
		gdb_printf(&dev->gdb, "%02x%02x",
			   regs[i] & 0xff, (regs[i] >> 8) & 0xff);
	gdb_packet_end(&dev->gdb);
	if (gdb_flush_ack(&dev->gdb) < 0)
		return -1;

	return check_ok(&dev->gdb);
}

static int do_reset(struct gdb_client *dev)
{
	char buf[GDB_BUF_SIZE];
	int len;

	if (gdb_send(&dev->gdb, "R00") < 0)
		return -1;

	len = gdb_read_packet(&dev->gdb, buf);
	if (!len) {
		if (gdb_send(&dev->gdb, "r") < 0)
			return -1;
		len = gdb_read_packet(&dev->gdb, buf);
	}

	if (len < 0)
		return -1;

	if (len < 2 || buf[0] != 'O' || buf[1] != 'K') {
		printc_err("gdbc: reset: bad response: %s\n", buf);
		return -1;
	}

	return 0;
}

static int bp_send(struct gdb_data *gdb, int c, address_t addr,
		   device_bptype_t type)
{
	int type_code = 0;

	switch (type) {
	case DEVICE_BPTYPE_BREAK:
		type_code = 1;
		break;

	case DEVICE_BPTYPE_WRITE:
		type_code = 2;
		break;

	case DEVICE_BPTYPE_READ:
		type_code = 3;
		break;

	case DEVICE_BPTYPE_WATCH:
		type_code = 4;
		break;
	}

	gdb_packet_start(gdb);
	gdb_printf(gdb, "%c%d,%04x,2", c, type_code, addr);
	gdb_packet_end(gdb);
	if (gdb_flush_ack(gdb) < 0)
		return -1;

	return check_ok(gdb);
}

static int refresh_bps(struct gdb_client *dev)
{
	int i;

	for (i = 0; i < dev->base.max_breakpoints; i++) {
		struct device_breakpoint *bp = &dev->base.breakpoints[i];
		struct device_breakpoint *old = &dev->last_bps[i];

		if (!(bp->flags & DEVICE_BP_DIRTY))
			continue;

		if ((old->flags & DEVICE_BP_ENABLED) &&
		    (bp_send(&dev->gdb, 'z', old->addr, old->type) < 0))
			return -1;

		if ((bp->flags & DEVICE_BP_ENABLED) &&
		    (bp_send(&dev->gdb, 'Z', bp->addr, bp->type) < 0))
			return -1;

		bp->flags &= ~DEVICE_BP_DIRTY;
	}

	memcpy(dev->last_bps, dev->base.breakpoints, sizeof(dev->last_bps));
	return 0;
}

static int gdbc_ctl(device_t dev_base, device_ctl_t op)
{
	struct gdb_client *dev = (struct gdb_client *)dev_base;

	switch (op) {
	case DEVICE_CTL_STEP:
		if (gdb_send(&dev->gdb, "s") < 0)
			return -1;

		return check_ok(&dev->gdb);

	case DEVICE_CTL_RUN:
		if (refresh_bps(dev) < 0)
			return -1;
		if (gdb_send(&dev->gdb, "c") < 0)
			return -1;
		dev->is_running = 1;
		return 0;

	case DEVICE_CTL_HALT:
		if (dev->is_running) {
			if (sockets_send(dev->gdb.sock, "\003", 1, 0) < 1) {
				pr_error("gdbc: write");
				return -1;
			}

			dev->is_running = 0;
			return check_ok(&dev->gdb);
		}
		return 0;

	case DEVICE_CTL_RESET:
		return do_reset(dev);

	default:
		printc_err("gdbc: unsupported operation\n");
		return -1;
	}

	return 0;
}

static int gdbc_erase(device_t dev_base, device_erase_type_t type,
		     address_t addr)
{
	struct gdb_client *dev = (struct gdb_client *)dev_base;
	const char *cmd = "erase";
	char buf[GDB_BUF_SIZE];
	int len;

	(void)type;
	(void)addr;

	gdb_packet_start(&dev->gdb);
	gdb_printf(&dev->gdb, "qRcmd,");
	while (*cmd)
		gdb_printf(&dev->gdb, "%02x", *(cmd++));
	gdb_packet_end(&dev->gdb);

	if (gdb_flush_ack(&dev->gdb) < 0)
		return -1;

	len = gdb_read_packet(&dev->gdb, buf);
	if (len < 0)
		return -1;

	return 0;
}

static device_status_t gdbc_poll(device_t dev_base)
{
	struct gdb_client *dev = (struct gdb_client *)dev_base;
	char buf[GDB_BUF_SIZE];
	int len;

	if (!dev->is_running)
		return DEVICE_STATUS_HALTED;

	len = gdb_peek(&dev->gdb, 50);
	if (ctrlc_check())
		return DEVICE_STATUS_INTR;

	if (len < 0) {
		dev->is_running = 0;
		return DEVICE_STATUS_ERROR;
	}

	if (!len)
		return DEVICE_STATUS_RUNNING;

	len = gdb_read_packet(&dev->gdb, buf);
	if (len < 0) {
		dev->is_running = 0;
		return DEVICE_STATUS_ERROR;
	}

	dev->is_running = 0;
	return DEVICE_STATUS_HALTED;
}

static int connect_to(const char *spec)
{
	const char *port_text;
	int hn_len;
	int port = 2000;
	char hostname[128];
	struct hostent *ent;
	struct sockaddr_in addr;
	int sock;

	if (!spec) {
		printc_err("gdbc: no remote target specified\n");
		return -1;
	}

	port_text = strchr(spec, ':');
	if (port_text) {
		port = atoi(port_text + 1);
		hn_len = port_text - spec;
	} else {
		hn_len = strlen(spec);
	}

	if (hn_len + 1 > sizeof(hostname))
		hn_len = sizeof(hostname) - 1;
	memcpy(hostname, spec, hn_len);
	hostname[hn_len] = 0;

	printc_dbg("Looking up %s...\n", hostname);
	ent = gethostbyname(hostname);
	if (!ent) {
#ifdef __Windows__
		printc_err("No such host: %s: %s\n", hostname,
			   last_error());
#else
		printc_err("No such host: %s: %s\n", hostname,
			   hstrerror(h_errno));
#endif
		return -1;
	}

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (SOCKET_ISERR(sock)) {
		printc_err("socket: %s\n", last_error());
		return -1;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr = *(struct in_addr *)ent->h_addr;
	printc_dbg("Connecting to %s:%d...\n",
		   inet_ntoa(addr.sin_addr), port);

	if (sockets_connect(sock, (struct sockaddr *)&addr,
			    sizeof(addr)) < 0) {
		printc_err("connect: %s\n", last_error());
		closesocket(sock);
		return -1;
	}

	return sock;
}

static device_t gdbc_open(const struct device_args *args)
{
	int sock = connect_to(args->path);
	struct gdb_client *dev;

	if (sock < 0)
		return NULL;

	dev = malloc(sizeof(struct gdb_client));
	if (!dev) {
		printc_err("gdbc: can't allocate memory: %s\n",
			   last_error());
		return NULL;
	}

	memset(dev, 0, sizeof(*dev));
	dev->base.type = &device_gdbc;
	dev->base.max_breakpoints = DEVICE_MAX_BREAKPOINTS;

	gdb_init(&dev->gdb, sock);
	return (device_t)dev;
}

const struct device_class device_gdbc = {
	.name		= "gdbc",
	.help		= "GDB client mode",
	.open		= gdbc_open,
	.destroy	= gdbc_destroy,
	.readmem	= gdbc_readmem,
	.writemem	= gdbc_writemem,
	.erase		= gdbc_erase,
	.getregs	= gdbc_getregs,
	.setregs	= gdbc_setregs,
	.ctl		= gdbc_ctl,
	.poll		= gdbc_poll,
	.getconfigfuses = NULL
};
