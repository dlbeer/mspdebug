/* MSPDebug - debugging tool for the eZ430
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <unistd.h>

#include "comport.h"
#include "util.h"
#include "output.h"
#include "sport.h"

struct comport_transport {
	struct transport        base;

	sport_t                 serial_fd;
};

static int serial_send(transport_t tr_base, const uint8_t *data, int len)
{
	struct comport_transport *tr = (struct comport_transport *)tr_base;

#ifdef DEBUG_SERIAL
	debug_hexdump("Serial transfer out:", data, len);
#endif

	if (sport_write_all(tr->serial_fd, data, len) < 0) {
		pr_error("comport: write error");
		return -1;
	}

	return 0;
}

static int serial_recv(transport_t tr_base, uint8_t *data, int max_len)
{
	struct comport_transport *tr = (struct comport_transport *)tr_base;
	int r;

	r = sport_read(tr->serial_fd, data, max_len);
	if (r < 0) {
		pr_error("comport: read error");
		return -1;
	}

#ifdef DEBUG_SERIAL
	debug_hexdump("Serial transfer in", data, r);
#endif
	return r;
}

static void serial_destroy(transport_t tr_base)
{
	struct comport_transport *tr = (struct comport_transport *)tr_base;

	sport_close(tr->serial_fd);
	free(tr);
}

static int serial_flush(transport_t tr_base)
{
	struct comport_transport *tr = (struct comport_transport *)tr_base;

	if (sport_flush(tr->serial_fd) < 0) {
		pr_error("comport: flush failed");
		return -1;
	}

	return 0;
}

static int serial_set_modem(transport_t tr_base, transport_modem_t state)
{
	struct comport_transport *tr = (struct comport_transport *)tr_base;
	int bits = 0;

	if (state & TRANSPORT_MODEM_DTR)
		bits |= SPORT_MC_DTR;

	if (state & TRANSPORT_MODEM_RTS)
		bits |= SPORT_MC_RTS;

	if (sport_set_modem(tr->serial_fd, bits) < 0) {
		pr_error("comport: failed to set modem control lines\n");
		return -1;
	}

	return 0;
}

static const struct transport_class comport_transport = {
	.destroy	= serial_destroy,
	.send		= serial_send,
	.recv		= serial_recv,
	.flush		= serial_flush,
	.set_modem	= serial_set_modem
};

transport_t comport_open(const char *device, int baud_rate)
{
	struct comport_transport *tr = malloc(sizeof(*tr));

	if (!tr) {
		pr_error("comport: couldn't allocate memory");
		return NULL;
	}

	tr->base.ops = &comport_transport;

	printc_dbg("Trying to open %s at %d bps...\n", device, baud_rate);
	tr->serial_fd = sport_open(device, baud_rate, 0);
	if (SPORT_ISERR(tr->serial_fd)) {
		printc_err("comport: can't open serial device: %s: %s\n",
			   device, last_error());
		free(tr);
		return NULL;
	}

	if (sport_set_modem(tr->serial_fd, 0) < 0)
		pr_error("warning: comport: failed to set "
			 "modem control lines");

	return (transport_t)tr;
}
