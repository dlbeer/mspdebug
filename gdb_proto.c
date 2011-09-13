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

#include <stdarg.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>

#include "sockets.h"
#include "gdb_proto.h"
#include "output.h"
#include "util.h"

void gdb_init(struct gdb_data *data, int sock)
{
	data->sock = sock;
	data->error = 0;
	data->head = 0;
	data->tail = 0;
	data->outlen = 0;
}

void gdb_printf(struct gdb_data *data, const char *fmt, ...)
{
	va_list ap;
	int len;

	va_start(ap, fmt);
	len = vsnprintf(data->outbuf + data->outlen,
			sizeof(data->outbuf) - data->outlen,
			fmt, ap);
	va_end(ap);

	data->outlen += len;
}

/* Returns -1 for error, 0 for timeout, >0 if data received. */
static int gdb_read(struct gdb_data *data, int timeout_ms)
{
	int was_timeout;
	int len = sockets_recv(data->sock, data->xbuf, sizeof(data->xbuf), 0,
			       timeout_ms, &was_timeout);

	if (was_timeout)
		return 0;

	if (len < 0) {
		data->error = 1;
		pr_error("gdb: recv");
		return -1;
	}

	if (!len) {
		data->error = 1;
		printc("Connection closed\n");
		return -1;
	}

	data->head = 0;
	data->tail = len;
	return len;
}

int gdb_peek(struct gdb_data *data, int timeout_ms)
{
	if (data->head == data->tail)
		return gdb_read(data, timeout_ms);

	return data->head != data->tail;
}

int gdb_getc(struct gdb_data *data)
{
	int c;

	/* If the buffer is empty, receive some more data */
	if (data->head == data->tail && gdb_read(data, -1) <= 0)
		return -1;

	c = data->xbuf[data->head];
	data->head++;

	return c;
}

static int gdb_flush(struct gdb_data *data)
{
	if (sockets_send(data->sock, data->outbuf, data->outlen, 0) < 0) {
		data->error = 1;
		pr_error("gdb: flush");
		return -1;
	}

	data->outlen = 0;
	return 0;
}

int gdb_flush_ack(struct gdb_data *data)
{
	int c;

#ifdef DEBUG_GDB
	printc("-> %s\n", data->outbuf);
#endif
	data->outbuf[data->outlen] = 0;

	do {
		if (sockets_send(data->sock, data->outbuf,
				 data->outlen, 0) < 0) {
			data->error = 1;
			pr_error("gdb: flush_ack");
			return -1;
		}

		do {
			c = gdb_getc(data);
			if (c < 0)
				return -1;
		} while (c != '+' && c != '-');
	} while (c != '+');

	data->outlen = 0;
	return 0;
}

void gdb_packet_start(struct gdb_data *data)
{
	gdb_printf(data, "$");
}

void gdb_packet_end(struct gdb_data *data)
{
	int i;
	int c = 0;

	for (i = 1; i < data->outlen; i++)
		c = (c + data->outbuf[i]) & 0xff;
	gdb_printf(data, "#%02x", c);
}

int gdb_send(struct gdb_data *data, const char *msg)
{
	gdb_packet_start(data);
	gdb_printf(data, "%s", msg);
	gdb_packet_end(data);
	return gdb_flush_ack(data);
}

int gdb_read_packet(struct gdb_data *data, char *buf)
{
	int c;
	int len = 0;
	int cksum_calc = 0;
	int cksum_recv = 0;

	/* Wait for packet start */
	do {
		c = gdb_getc(data);
		if (c < 0)
			return -1;
	} while (c != '$');

	/* Read packet payload */
	while (len + 1 < GDB_BUF_SIZE) {
		c = gdb_getc(data);
		if (c < 0)
			return -1;
		if (c == '#')
			break;

		buf[len++] = c;
		cksum_calc = (cksum_calc + c) & 0xff;
	}
	buf[len] = 0;

	/* Read packet checksum */
	c = gdb_getc(data);
	if (c < 0)
		return -1;
	cksum_recv = hexval(c);
	c = gdb_getc(data);
	if (c < 0)
		return -1;
	cksum_recv = (cksum_recv << 4) | hexval(c);

#ifdef DEBUG_GDB
	printc("<- $%s#%02x\n", buf, cksum_recv);
#endif

	if (cksum_recv != cksum_calc) {
		printc_err("gdb: bad checksum (calc = 0x%02x, "
			"recv = 0x%02x)\n", cksum_calc, cksum_recv);
		printc_err("gdb: packet data was: %s\n", buf);
		gdb_printf(data, "-");
		if (gdb_flush(data) < 0)
			return -1;
		return 0;
	}

	/* Send acknowledgement */
	gdb_printf(data, "+");
	if (gdb_flush(data) < 0)
		return -1;

	return len;
}
