/* MSPDebug - debugging tool for MSP430 MCUs
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

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "sport.h"

sport_t sport_open(const char *device, int rate, int flags)
{
	int fd = open(device, O_RDWR | O_NOCTTY);
	struct termios attr;

	if (fd < 0)
		return -1;

	tcgetattr(fd, &attr);
	cfmakeraw(&attr);
	cfsetispeed(&attr, rate);
	cfsetospeed(&attr, rate);

	if (flags & SPORT_EVEN_PARITY)
		attr.c_cflag |= PARENB;

	if (tcsetattr(fd, TCSAFLUSH, &attr) < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

void sport_close(sport_t s)
{
	close(s);
}

int sport_flush(sport_t s)
{
	return tcflush(s, TCIFLUSH);
}

int sport_set_modem(sport_t s, int bits)
{
	return ioctl(s, TIOCMSET, &bits);
}

int sport_read(sport_t s, uint8_t *data, int len)
{
	int r;

	do {
		struct timeval tv = {
			.tv_sec = 5,
			.tv_usec = 0
		};

		fd_set set;

		FD_ZERO(&set);
		FD_SET(s, &set);

		r = select(s + 1, &set, NULL, NULL, &tv);
		if (r > 0)
			r = read(s, data, len);

		if (!r)
			errno = ETIMEDOUT;
		if (r <= 0 && errno != EINTR)
			return -1;
	} while (r <= 0);

	return r;
}

int sport_write(sport_t s, const uint8_t *data, int len)
{
	return write(s, data, len);
}

int sport_read_all(sport_t s, uint8_t *data, int len)
{
	while (len) {
		int r = sport_read(s, data, len);

		if (r <= 0)
			return -1;

		data += r;
		len -= r;
	}

	return 0;
}

int sport_write_all(sport_t s, const uint8_t *data, int len)
{
	while (len) {
		int r = sport_write(s, data, len);

		if (r <= 0)
			return -1;

		data += r;
		len -= r;
	}

	return 0;
}
