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
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include "fet.h"

extern void hexdump(int addr, const u_int8_t *data, int len);

static int serial_fd = -1;

static int serial_send(const u_int8_t *data, int len)
{
	int result;

	assert (serial_fd >= 0);

#ifdef DEBUG_SERIAL
	puts("Serial transfer out:");
	hexdump(0, data, len);
#endif

	while (len) {
		result = write(serial_fd, data, len);
		if (result < 0) {
			if (errno == EINTR)
				continue;

			perror("serial_send");
			return -1;
		}

		data += result;
		len -= result;
	}

	return 0;
}

static int serial_recv(u_int8_t *data, int max_len)
{
	int len;

	assert (serial_fd >= 0);

	len = read(serial_fd, data, max_len);

	if (len < 0) {
		perror("serial_recv");
		return -1;
	}

#ifdef DEBUG_SERIAL
	puts("Serial transfer in:");
	hexdump(0, data, len);
#endif
	return len;
}

static void serial_close(void)
{
	assert (serial_fd >= 0);

	close(serial_fd);
}

static const struct fet_transport serial_transport = {
	.send = serial_send,
	.recv = serial_recv,
	.close = serial_close
};

int uif_open(const char *device, int want_jtag)
{
	struct termios attr;

	printf("Trying to open UIF on %s...\n", device);

	serial_fd = open(device, O_RDWR | O_NOCTTY);
	if (serial_fd < 0) {
		fprintf(stderr, "uif_open: open: %s: %s\n",
			device, strerror(errno));
		return -1;
	}

	tcgetattr(serial_fd, &attr);
	cfmakeraw(&attr);
	cfsetspeed(&attr, B460800);
	if (tcsetattr(serial_fd, TCSAFLUSH, &attr) < 0) {
		fprintf(stderr, "uif_open: tcsetattr: %s: %s\n",
			device, strerror(errno));
		return -1;
	}

	if (fet_open(&serial_transport, want_jtag ? 0 : FET_PROTO_SPYBIWIRE,
			3000) < 0) {
		close(serial_fd);
		return -1;
	}

	return 0;
}
