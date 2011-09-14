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

#ifndef GDB_PROTO_H_
#define GDB_PROTO_H_

#define GDB_MAX_XFER    8192
#define GDB_BUF_SIZE	(GDB_MAX_XFER * 2 + 64)

struct gdb_data {
	int             sock;
	int             error;

	char            xbuf[1024];
	int             head;
	int             tail;

	char            outbuf[GDB_BUF_SIZE];
	int             outlen;
};

void gdb_init(struct gdb_data *d, int sock);
void gdb_printf(struct gdb_data *data, const char *fmt, ...);
int gdb_send(struct gdb_data *data, const char *msg);
void gdb_packet_start(struct gdb_data *data);
void gdb_packet_end(struct gdb_data *data);
int gdb_peek(struct gdb_data *data, int timeout_ms);
int gdb_getc(struct gdb_data *data);
int gdb_flush_ack(struct gdb_data *data);
int gdb_read_packet(struct gdb_data *data, char *buf);

#endif
