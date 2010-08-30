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
#include <errno.h>
#include <assert.h>
#include <stdint.h>
#include <unistd.h>
#include <termios.h>

#include "uif.h"
#include "util.h"
#include "output.h"

#ifdef __APPLE__
#define B460800 460800
#define B500000 500000
#endif

struct uif_transport {
	struct transport        base;

	int                     serial_fd;
};

static int serial_send(transport_t tr_base, const uint8_t *data, int len)
{
	struct uif_transport *tr = (struct uif_transport *)tr_base;

#ifdef DEBUG_SERIAL
	debug_hexdump("Serial transfer out:", data, len);
#endif

	if (write_all(tr->serial_fd, data, len) < 0) {
		pr_error("uif: write error");
		return -1;
	}

	return 0;
}

static int serial_recv(transport_t tr_base, uint8_t *data, int max_len)
{
	struct uif_transport *tr = (struct uif_transport *)tr_base;
	int r;

	r = read_with_timeout(tr->serial_fd, data, max_len);
	if (r < 0) {
		pr_error("uif: read error");
		return -1;
	}

#ifdef DEBUG_SERIAL
	debug_hexdump("Serial transfer in", data, r);
#endif
	return r;
}

static void serial_destroy(transport_t tr_base)
{
	struct uif_transport *tr = (struct uif_transport *)tr_base;

	close(tr->serial_fd);
	free(tr);
}

transport_t uif_open(const char *device, int is_olimex)
{
	struct uif_transport *tr = malloc(sizeof(*tr));

	if (!tr) {
		pr_error("uif: couldn't allocate memory");
		return NULL;
	}

	tr->base.send = serial_send;
	tr->base.recv = serial_recv;
	tr->base.destroy = serial_destroy;

	printc("Trying to open %s on %s...\n",
		is_olimex ? "Olimex" : "UIF", device);

	tr->serial_fd = open_serial(device, is_olimex ? B500000 : B460800);
	if (tr->serial_fd < 0) {
		printc_err("uif: can't open serial device: %s: %s\n",
			device, strerror(errno));
		free(tr);
		return NULL;
	}

	return (transport_t)tr;
}
