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
#include <assert.h>
#include <stdint.h>
#include <unistd.h>

#include "uif.h"
#include "util.h"
#include "output.h"
#include "sport.h"

#if defined(__linux__)
#include <linux/serial.h>
#include <fcntl.h>
#endif

struct uif_transport {
	struct transport        base;

	sport_t                 serial_fd;
};

static int serial_send(transport_t tr_base, const uint8_t *data, int len)
{
	struct uif_transport *tr = (struct uif_transport *)tr_base;

#ifdef DEBUG_SERIAL
	debug_hexdump("Serial transfer out:", data, len);
#endif

	if (sport_write_all(tr->serial_fd, data, len) < 0) {
		pr_error("uif: write error");
		return -1;
	}

	return 0;
}

static int serial_recv(transport_t tr_base, uint8_t *data, int max_len)
{
	struct uif_transport *tr = (struct uif_transport *)tr_base;
	int r;

	r = sport_read(tr->serial_fd, data, max_len);
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

	sport_close(tr->serial_fd);
	free(tr);
}

#if defined(__linux__)
static int open_olimex_iso(const char *device)
{
        int fd = open(device, O_RDWR | O_NOCTTY);
        struct termios attr;
        struct serial_struct serial_info;

        if (fd < 0)
                return -1;

        tcgetattr(fd, &attr);
        cfmakeraw(&attr);
        cfsetispeed(&attr, B38400);
        cfsetospeed(&attr, B38400);

        serial_info.flags = ASYNC_SPD_CUST;
        serial_info.custom_divisor = 120;
        if (ioctl(fd, TIOCSSERIAL, &serial_info) < 0) {
		printc_err("open_olimex_iso: can't do "
			   "ioctl TIOCSSERIAL: %s\n",
			   last_error());
		return -1;
        }

        if (tcsetattr(fd, TCSAFLUSH, &attr) < 0)
                return -1;

        return fd;
}
#endif

transport_t uif_open(const char *device, uif_type_t type)
{
	struct uif_transport *tr = malloc(sizeof(*tr));

	if (!tr) {
		pr_error("uif: couldn't allocate memory");
		return NULL;
	}

	tr->base.send = serial_send;
	tr->base.recv = serial_recv;
	tr->base.destroy = serial_destroy;

	switch (type) {
	case UIF_TYPE_FET:
		printc("Trying to open UIF on %s...\n", device);
		tr->serial_fd = sport_open(device, B460800, 0);
		break;

	case UIF_TYPE_OLIMEX:
		printc("Trying to open Olimex on %s...\n", device);
		tr->serial_fd = sport_open(device, B500000, 0);
		break;

	case UIF_TYPE_OLIMEX_ISO:
#if defined(__linux__)
		printc("Trying to open Olimex (ISO) on %s...\n", device);
		tr->serial_fd = open_olimex_iso(device);
#else
		printc_err("uif_open: ioctl TIOCSSERIAL not supported "
			   "on this platform\n");
#endif
		break;
	}

	if (SPORT_ISERR(tr->serial_fd)) {
		printc_err("uif: can't open serial device: %s: %s\n",
			   device, last_error());
		free(tr);
		return NULL;
	}

	return (transport_t)tr;
}
