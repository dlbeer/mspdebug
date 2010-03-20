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
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include "util.h"

void hexdump(int addr, const u_int8_t *data, int len)
{
	int offset = 0;

	while (offset < len) {
		int i, j;

		/* Address label */
		printf("    %04x:", offset + addr);

		/* Hex portion */
		for (i = 0; i < 16 && offset + i < len; i++)
			printf(" %02x", data[offset + i]);
		for (j = i; j < 16; j++)
			printf("   ");

		/* Printable characters */
		printf(" |");
		for (j = 0; j < i; j++) {
			int c = data[offset + j];

			printf("%c", (c >= 32 && c <= 126) ? c : '.');
		}
		for (; j < 16; j++)
			printf(" ");
		printf("|\n");

		offset += i;
	}
}

/* This table of device IDs is sourced mainly from the MSP430 Memory
 * Programming User's Guide (SLAU265).
 *
 * The table should be kept sorted by device ID.
 */

static struct {
	u_int16_t	id;
	const char	*id_text;
} id_table[] = {
	{0x1132, "F1122"},
	{0x1132, "F1132"},
	{0x1232, "F1222"},
	{0x1232, "F1232"},
	{0xF112, "F11x"},   /* obsolete */
	{0xF112, "F11x1"},  /* obsolete */
	{0xF112, "F11x1A"}, /* obsolete */
	{0xF123, "F122"},
	{0xF123, "F123x"},
	{0xF143, "F14x"},
	{0xF149, "F13x"},
	{0xF149, "F14x1"},
	{0xF149, "F149"},
	{0xF169, "F16x"},
	{0xF16C, "F161x"},
	{0xF201, "F20x3"},
	{0xF213, "F21x1"},
	{0xF227, "F22xx"},
	{0xF249, "F24x"},
	{0xF26F, "F261x"},
	{0xF413, "F41x"},
	{0xF427, "FE42x"},
	{0xF427, "FW42x"},
	{0xF427, "F415"},
	{0xF427, "F417"},
	{0xF427, "F42x0"},
	{0xF439, "FG43x"},
	{0xF449, "F43x"},
	{0xF449, "F44x"},
	{0xF46F, "FG46xx"},
	{0xF46F, "F471xx"}
};

void print_devid(u_int16_t id)
{
	int i = 0;

	while (i < ARRAY_LEN(id_table) && id_table[i].id != id)
		i++;

	if (i < ARRAY_LEN(id_table)) {
		printf("Device: MSP430%s", id_table[i++].id_text);
		while (id_table[i].id == id)
			printf("/MSP430%s", id_table[i++].id_text);
		printf("\n");
	} else {
		printf("Unknown device ID: 0x%04x\n", id);
	}
}

int read_with_timeout(int fd, u_int8_t *data, int max_len)
{
	int r;

	do {
		struct timeval tv = {
			.tv_sec = 5,
			.tv_usec = 0
		};

		fd_set set;

		FD_ZERO(&set);
		FD_SET(fd, &set);

		r = select(fd + 1, &set, NULL, NULL, &tv);
		if (r > 0)
			r = read(fd, data, max_len);

		if (!r)
			errno = ETIMEDOUT;
		if (r <= 0 && errno != EINTR)
			return -1;
	} while (r <= 0);

	return r;
}

int write_all(int fd, const u_int8_t *data, int len)
{
	while (len) {
		int result = write(fd, data, len);

		if (result < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}

		data += result;
		len -= result;
	}

	return 0;
}

int open_serial(const char *device, int rate)
{
	int fd = open(device, O_RDWR | O_NOCTTY);
	struct termios attr;

	tcgetattr(fd, &attr);
	cfmakeraw(&attr);
	cfsetspeed(&attr, rate);

	if (tcsetattr(fd, TCSAFLUSH, &attr) < 0)
		return -1;

	return fd;
}

static volatile int ctrlc_flag;

static void sigint_handler(int signum)
{
	ctrlc_flag = 1;
}

void ctrlc_init(void)
{
	const static struct sigaction siga = {
		.sa_handler = sigint_handler,
		.sa_flags = 0
	};

	sigaction(SIGINT, &siga, NULL);
}

void ctrlc_reset(void)
{
	ctrlc_flag = 0;
}

int ctrlc_check(void)
{
	return ctrlc_flag;
}
