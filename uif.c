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
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <termios.h>

#include "transport.h"
#include "util.h"

static int serial_fd = -1;

static int serial_send(const u_int8_t *data, int len)
{
	assert (serial_fd >= 0);

#ifdef DEBUG_SERIAL
	puts("Serial transfer out:");
	hexdump(0, data, len);
#endif

	if (write_all(serial_fd, data, len) < 0) {
		perror("uif: write error");
		return -1;
	}

	return 0;
}

static int serial_recv(u_int8_t *data, int max_len)
{
	int r;

	assert (serial_fd >= 0);

	r = read_with_timeout(serial_fd, data, max_len);
	if (r < 0) {
		perror("uif: read error");
		return -1;
	}

#ifdef DEBUG_SERIAL
	puts("Serial transfer in:");
	hexdump(0, data, r);
#endif
	return r;
}

static void serial_close(void)
{
	assert (serial_fd >= 0);

	close(serial_fd);
}

static int serial_flush(void)
{
	if (tcflush(serial_fd, TCIFLUSH) < 0) {
		perror("uif: tcflush");
		return -1;
	}

	return 0;
}

static const struct fet_transport serial_transport = {
	.flush = serial_flush,
	.send = serial_send,
	.recv = serial_recv,
	.close = serial_close
};

const struct fet_transport *uif_open(const char *device)
{
	printf("Trying to open UIF on %s...\n", device);

	serial_fd = open_serial(device, B460800);
	if (serial_fd < 0) {
		fprintf(stderr, "uif: can't open serial device: %s: %s\n",
			device, strerror(errno));
		return NULL;
	}

	return &serial_transport;
}
