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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "device.h"
#include "util.h"
#include "gdb.h"

#define MAX_MEM_XFER    1024

/************************************************************************
 * GDB IO routines
 */

struct gdb_data {
	int             sock;
	int             error;

	char            xbuf[1024];
	int             head;
	int             tail;

	char            outbuf[MAX_MEM_XFER * 2 + 64];
	int             outlen;

	device_t        device;
};

static void gdb_printf(struct gdb_data *data, const char *fmt, ...)
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

static int gdb_read(struct gdb_data *data, int blocking)
{
	fd_set r;
	int len;
	struct timeval to = {
		.tv_sec = 0,
		.tv_usec = 0
	};

	FD_ZERO(&r);
	FD_SET(data->sock, &r);

	if (select(data->sock + 1, &r, NULL, NULL,
		   blocking ? NULL : &to) < 0) {
		perror("gdb: select");
		return -1;
	}

	if (!FD_ISSET(data->sock, &r))
		return 0;

	len = recv(data->sock, data->xbuf, sizeof(data->xbuf), 0);

	if (len < 0) {
		data->error = errno;
		perror("gdb: recv");
		return -1;
	}

	if (!len) {
		printf("Connection closed\n");
		return -1;
	}

	data->head = 0;
	data->tail = len;
	return len;
}

static int gdb_peek(struct gdb_data *data)
{
	if (data->head == data->tail && gdb_read(data, 0) < 0)
		return -1;

	return data->head != data->tail;
}

static int gdb_getc(struct gdb_data *data)
{
	int c;

	/* If the buffer is empty, receive some more data */
	if (data->head == data->tail && gdb_read(data, 1) < 0)
		return -1;

	c = data->xbuf[data->head];
	data->head++;

	return c;
}

static int gdb_flush(struct gdb_data *data)
{
	if (send(data->sock, data->outbuf, data->outlen, 0) < 0) {
		data->error = errno;
		perror("gdb: flush");
		return -1;
	}

	data->outlen = 0;
	return 0;
}

static int gdb_flush_ack(struct gdb_data *data)
{
	int c;

	do {
		data->outbuf[data->outlen] = 0;
#ifdef DEBUG_GDB
		printf("-> %s\n", data->outbuf);
#endif
		if (send(data->sock, data->outbuf, data->outlen, 0) < 0) {
			data->error = errno;
			perror("gdb: flush_ack");
			return -1;
		}

		c = gdb_getc(data);
		if (c < 0)
			return -1;
	} while (c != '+');

	data->outlen = 0;
	return 0;
}

static void gdb_packet_start(struct gdb_data *data)
{
	gdb_printf(data, "$");
}

static void gdb_packet_end(struct gdb_data *data)
{
	int i;
	int c = 0;

	for (i = 1; i < data->outlen; i++)
		c = (c + data->outbuf[i]) & 0xff;
	gdb_printf(data, "#%02x", c);
}

static int gdb_send_hex(struct gdb_data *data, const char *text)
{
	gdb_packet_start(data);
	while (*text)
		gdb_printf(data, "%02x", *(text++));
	gdb_packet_end(data);

	return gdb_flush_ack(data);
}

static int hexval(int c)
{
	if (isdigit(c))
		return c - '0';
	if (isupper(c))
		return c - 'A' + 10;
	if (islower(c))
		return c - 'a' + 10;

	return 0;
}

static int gdb_send(struct gdb_data *data, const char *msg)
{
	gdb_packet_start(data);
	gdb_printf(data, "%s", msg);
	gdb_packet_end(data);
	return gdb_flush_ack(data);
}

/************************************************************************
 * GDB server
 */

static int read_registers(struct gdb_data *data)
{
	uint16_t regs[DEVICE_NUM_REGS];
	int i;

	printf("Reading registers\n");
	if (data->device->getregs(data->device, regs) < 0)
		return gdb_send(data, "E00");

	gdb_packet_start(data);
	for (i = 0; i < DEVICE_NUM_REGS; i++)
		gdb_printf(data, "%02x%02x", regs[i] & 0xff, regs[i] >> 8);
	gdb_packet_end(data);
	return gdb_flush_ack(data);
}

static int monitor_command(struct gdb_data *data, char *buf)
{
	char cmd[128];
	int len = 0;

	while (len + 1 < sizeof(cmd) && *buf && buf[1]) {
		if (len + 1 >= sizeof(cmd))
			break;

		cmd[len++] = (hexval(buf[0]) << 4) | hexval(buf[1]);
		buf += 2;
	}
	cmd[len] = 0;

	printf("Monitor command received: %s\n", cmd);

	if (!strcasecmp(cmd, "reset")) {
		printf("Resetting device\n");
		if (data->device->ctl(data->device, DEVICE_CTL_RESET) < 0)
			return gdb_send_hex(data, "Reset failed\n");
	} else if (!strcasecmp(cmd, "erase")) {
		printf("Erasing device\n");
		if (data->device->ctl(data->device, DEVICE_CTL_ERASE) < 0)
			return gdb_send_hex(data, "Erase failed\n");
	}

	return gdb_send(data, "OK");
}

static int write_registers(struct gdb_data *data, char *buf)
{
	uint16_t regs[DEVICE_NUM_REGS];
	int i;

	if (strlen(buf) < DEVICE_NUM_REGS * 4)
		return gdb_send(data, "E00");

	printf("Writing registers\n");
	for (i = 0; i < DEVICE_NUM_REGS; i++) {
		regs[i] = (hexval(buf[2]) << 12) |
			(hexval(buf[3]) << 8) |
			(hexval(buf[0]) << 4) |
			hexval(buf[1]);
		buf += 4;
	}

	if (data->device->setregs(data->device, regs) < 0)
		return gdb_send(data, "E00");

	return gdb_send(data, "OK");
}

static int read_memory(struct gdb_data *data, char *text)
{
	char *length_text = strchr(text, ',');
	int length, addr;
	uint8_t buf[MAX_MEM_XFER];
	int i;

	if (!length_text) {
		fprintf(stderr, "gdb: malformed memory read request\n");
		return gdb_send(data, "E00");
	}

	*(length_text++) = 0;

	length = strtoul(length_text, NULL, 16);
	addr = strtoul(text, NULL, 16);

	if (length > sizeof(buf))
		length = sizeof(buf);

	printf("Reading %d bytes from 0x%04x\n", length, addr);

	if (data->device->readmem(data->device, addr, buf, length) < 0)
		return gdb_send(data, "E00");

	gdb_packet_start(data);
	for (i = 0; i < length; i++)
		gdb_printf(data, "%02x", buf[i]);
	gdb_packet_end(data);

	return gdb_flush_ack(data);
}

static int write_memory(struct gdb_data *data, char *text)
{
	char *data_text = strchr(text, ':');
	char *length_text = strchr(text, ',');
	int length, addr;
	uint8_t buf[MAX_MEM_XFER];
	int buflen = 0;

	if (!(data_text && length_text)) {
		fprintf(stderr, "gdb: malformed memory write request\n");
		return gdb_send(data, "E00");
	}

	*(data_text++) = 0;
	*(length_text++) = 0;

	length = strtoul(length_text, NULL, 16);
	addr = strtoul(text, NULL, 16);

	while (buflen < sizeof(buf) && *data_text && data_text[1]) {
		buf[buflen++] = (hexval(data_text[0]) << 4) |
			hexval(data_text[1]);
		data_text += 2;
	}

	if (buflen != length) {
		fprintf(stderr, "gdb: length mismatch\n");
		return gdb_send(data, "E00");
	}

	printf("Writing %d bytes to 0x%04x\n", buflen, addr);

	if (data->device->writemem(data->device, addr, buf, buflen) < 0)
		return gdb_send(data, "E00");

	return gdb_send(data, "OK");
}

static int run_set_pc(struct gdb_data *data, char *buf)
{
	uint16_t regs[DEVICE_NUM_REGS];

	if (!*buf)
		return 0;

	if (data->device->getregs(data->device, regs) < 0)
		return -1;

	regs[0] = strtoul(buf, NULL, 16);
	return data->device->setregs(data->device, regs);
}

static int run_final_status(struct gdb_data *data)
{
	uint16_t regs[DEVICE_NUM_REGS];
	int i;

	if (data->device->getregs(data->device, regs) < 0)
		return gdb_send(data, "E00");

	gdb_packet_start(data);
	gdb_printf(data, "T05");
	for (i = 0; i < 16; i++)
		gdb_printf(data, "%02x:%02x%02x;", i,
			   regs[i] & 0xff, regs[i] >> 8);
	gdb_packet_end(data);

	return gdb_flush_ack(data);
}

static int single_step(struct gdb_data *data, char *buf)
{
	printf("Single stepping\n");

	if (run_set_pc(data, buf) < 0 ||
	    data->device->ctl(data->device, DEVICE_CTL_STEP) < 0)
		gdb_send(data, "E00");

	return run_final_status(data);
}

static int run(struct gdb_data *data, char *buf)
{
	printf("Running\n");

	if (run_set_pc(data, buf) < 0 ||
	    data->device->ctl(data->device, DEVICE_CTL_RUN) < 0)
		return gdb_send(data, "E00");

	for (;;) {
		device_status_t status = data->device->poll(data->device);

		if (status == DEVICE_STATUS_ERROR)
			return gdb_send(data, "E00");

		if (status == DEVICE_STATUS_HALTED) {
			printf("Target halted\n");
			goto out;
		}

		if (status == DEVICE_STATUS_INTR)
			goto out;

		while (gdb_peek(data)) {
			int c = gdb_getc(data);

			if (c < 0)
				return -1;

			if (c == 3) {
				printf("Interrupted by gdb\n");
				goto out;
			}
		}
	}

 out:
	if (data->device->ctl(data->device, DEVICE_CTL_HALT) < 0)
		return gdb_send(data, "E00");

	return run_final_status(data);
}

static int set_breakpoint(struct gdb_data *data, int enable, char *buf)
{
	char *parts[2];
	int type;
	int addr;
	int i;

	/* Break up the arguments */
	for (i = 0; i < 2; i++)
		parts[i] = strsep(&buf, ",");

	/* Make sure there's a type argument */
	if (!parts[0]) {
		fprintf(stderr, "gdb: breakpoint requested with no type\n");
		return gdb_send(data, "E00");
	}

	/* We only support breakpoints */
	type = atoi(parts[0]);
	if (type < 0 || type > 1) {
		fprintf(stderr, "gdb: unsupported breakpoint type: %s\n",
			parts[0]);
		return gdb_send(data, "");
	}

	/* There needs to be an address specified */
	if (!parts[1]) {
		fprintf(stderr, "gdb: breakpoint address missing\n");
		return gdb_send(data, "E00");
	}

	/* Parse the breakpoint address */
	addr = strtoul(parts[1], NULL, 16);

	if (enable) {
		if (device_setbrk(data->device, -1, 1, addr) < 0) {
			fprintf(stderr, "gdb: can't add breakpoint at "
				"0x%04x\n", addr);
			return gdb_send(data, "E00");
		}

		printf("Breakpoint set at 0x%04x\n", addr);
	} else {
		device_setbrk(data->device, -1, 0, addr);
		printf("Breakpoint cleared at 0x%04x\n", addr);
	}

	return gdb_send(data, "OK");
}

static int process_gdb_command(struct gdb_data *data, char *buf, int len)
{
	switch (buf[0]) {
	case '?': /* Return target halt reason */
		return run_final_status(data);

	case 'z':
	case 'Z':
		return set_breakpoint(data, buf[0] == 'Z', buf + 1);

	case 'g': /* Read registers */
		return read_registers(data);

	case 'G': /* Write registers */
		return write_registers(data, buf + 1);

	case 'q': /* Query */
		if (!strncmp(buf, "qRcmd,", 6))
			return monitor_command(data, buf + 6);
		break;

	case 'm': /* Read memory */
		return read_memory(data, buf + 1);

	case 'M': /* Write memory */
		return write_memory(data, buf + 1);

	case 'c': /* Continue */
		return run(data, buf + 1);

	case 's': /* Single step */
		return single_step(data, buf + 1);
	}

	/* For unknown/unsupported packets, return an empty reply */
	return gdb_send(data, "");
}

static void gdb_reader_loop(struct gdb_data *data)
{
	for (;;) {
		char buf[MAX_MEM_XFER * 2 + 64];
		int len = 0;
		int cksum_calc = 0;
		int cksum_recv = 0;
		int c;

		/* Wait for packet start */
		do {
			c = gdb_getc(data);
			if (c < 0)
				return;
		} while (c != '$');

		/* Read packet payload */
		while (len + 1 < sizeof(buf)) {
			c = gdb_getc(data);
			if (c < 0)
				return;
			if (c == '#')
				break;

			buf[len++] = c;
			cksum_calc = (cksum_calc + c) & 0xff;
		}
		buf[len] = 0;

		/* Read packet checksum */
		c = gdb_getc(data);
		if (c < 0)
			return;
		cksum_recv = hexval(c);
		c = gdb_getc(data);
		if (c < 0)
			return;
		cksum_recv = (cksum_recv << 4) | hexval(c);

#ifdef DEBUG_GDB
		printf("<- $%s#%02x\n", buf, cksum_recv);
#endif

		if (cksum_recv != cksum_calc) {
			fprintf(stderr, "gdb: bad checksum (calc = 0x%02x, "
				"recv = 0x%02x)\n", cksum_calc, cksum_recv);
			fprintf(stderr, "gdb: packet data was: %s\n", buf);
			gdb_printf(data, "-");
			if (gdb_flush(data) < 0)
				return;
			continue;
		}

		/* Send acknowledgement */
		gdb_printf(data, "+");
		if (gdb_flush(data) < 0)
			return;

		if (len && process_gdb_command(data, buf, len) < 0)
			return;
	}
}

static int gdb_server(device_t device, int port)
{
	int sock;
	int client;
	struct sockaddr_in addr;
	socklen_t len;
	int arg;
	struct gdb_data data;
	int i;

	sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		perror("gdb: can't create socket");
		return -1;
	}

	arg = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &arg, sizeof(arg)) < 0)
		perror("gdb: warning: can't reuse socket address");

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "gdb: can't bind to port %d: %s\n",
			port, strerror(errno));
		close(sock);
		return -1;
	}

	if (listen(sock, 1) < 0) {
		perror("gdb: can't listen on socket");
		close(sock);
		return -1;
	}

	printf("Bound to port %d. Now waiting for connection...\n", port);

	len = sizeof(addr);
	client = accept(sock, (struct sockaddr *)&addr, &len);
	if (client < 0) {
		perror("gdb: failed to accept connection");
		close(sock);
		return -1;
	}

	close(sock);
	printf("Client connected from %s:%d\n",
	       inet_ntoa(addr.sin_addr), htons(addr.sin_port));

	data.sock = client;
	data.error = 0;
	data.head = 0;
	data.tail = 0;
	data.outlen = 0;
	data.device = device;

	/* Put the hardware breakpoint setting into a known state. */
	printf("Clearing all breakpoints...\n");
	for (i = 0; i < device->max_breakpoints; i++)
		device_setbrk(device, i, 0, 0);

	gdb_reader_loop(&data);

	return data.error ? -1 : 0;
}

static int cmd_gdb(cproc_t cp, char **arg)
{
	char *port_text = get_arg(arg);
	int port = 2000;
	int want_loop = 0;

	cproc_get_int(cp, "gdb_loop", &want_loop);

	if (port_text)
		port = atoi(port_text);

	if (port <= 0 || port > 65535) {
		fprintf(stderr, "gdb: invalid port: %d\n", port);
		return -1;
	}

	do {
		if (gdb_server(cproc_device(cp), port) < 0)
			return -1;
	} while (want_loop);

	return 0;
}

static const struct cproc_command command_gdb = {
	.name = "gdb",
	.func = cmd_gdb,
	.help =
	"gdb [port]\n"
	"    Run a GDB remote stub on the given TCP/IP port.\n"
};

static const struct cproc_option option_gdb = {
	.name = "gdb_loop",
	.type = CPROC_OPTION_BOOL,
	.help =
"Automatically restart the GDB server after disconnection. If this\n"
"option is set, then the GDB server keeps running until an error occurs,\n"
"or the user interrupts with Ctrl+C.\n"
};

int gdb_register(cproc_t cp)
{
	if (cproc_register_options(cp, &option_gdb, 1) < 0)
		return -1;

	return cproc_register_commands(cp, &command_gdb, 1);
}
