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
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "device.h"
#include "util.h"
#include "gdb.h"

/************************************************************************
 * GDB IO routines
 */

static int gdb_socket;
static int gdb_errno;

static char gdb_xbuf[1024];
static int gdb_head;
static int gdb_tail;

static char gdb_outbuf[1024];
static int gdb_outlen;

static void gdb_printf(const char *fmt, ...)
{
	va_list ap;
	int len;

	va_start(ap, fmt);
	len = vsnprintf(gdb_outbuf + gdb_outlen,
			sizeof(gdb_outbuf) - gdb_outlen,
			fmt, ap);
	va_end(ap);

	gdb_outlen += len;
}

static int gdb_flush(void)
{
	if (send(gdb_socket, gdb_outbuf, gdb_outlen, 0) < 0) {
		gdb_errno = errno;
		perror("gdb: send");
		return -1;
	}

	gdb_outlen = 0;
	return 0;
}

static int gdb_read(int blocking)
{
	fd_set r;
	int len;
	struct timeval to = {
		.tv_sec = 0,
		.tv_usec = 0
	};

	FD_ZERO(&r);
	FD_SET(gdb_socket, &r);

	if (select(gdb_socket + 1, &r, NULL, NULL,
		   blocking ? NULL : &to) < 0) {
		perror("gdb: select");
		return -1;
	}

	if (!FD_ISSET(gdb_socket, &r))
		return 0;

	len = recv(gdb_socket, gdb_xbuf, sizeof(gdb_xbuf), 0);

	if (len < 0) {
		gdb_errno = errno;
		perror("gdb: recv");
		return -1;
	}

	if (!len) {
		printf("Connection closed\n");
		return -1;
	}

	gdb_head = 0;
	gdb_tail = len;
	return len;
}

static int gdb_peek(void)
{
	if (gdb_head == gdb_tail && gdb_read(0) < 0)
		return -1;

	return gdb_head != gdb_tail;
}

static int gdb_getc(void)
{
	int c;

	/* If the buffer is empty, receive some more data */
	if (gdb_head == gdb_tail && gdb_read(1) < 0)
		return -1;

	c = gdb_xbuf[gdb_head];
	gdb_head++;

	return c;
}

static int gdb_flush_ack(void)
{
	int c;

	do {
		gdb_outbuf[gdb_outlen] = 0;
#ifdef DEBUG_GDB
		printf("-> %s\n", gdb_outbuf);
#endif
		if (gdb_flush() < 0)
			return -1;

		c = gdb_getc();
		if (c < 0)
			return -1;
	} while (c != '+');

	return 0;
}

static void gdb_packet_start(void)
{
	gdb_printf("$");
}

static void gdb_packet_end(void)
{
	int i;
	int c = 0;

	for (i = 1; i < gdb_outlen; i++)
		c = (c + gdb_outbuf[i]) & 0xff;
	gdb_printf("#%02x", c);
}

static int gdb_send_hex(const char *text)
{
	gdb_packet_start();
	while (*text)
		gdb_printf("%02x", *(text++));
	gdb_packet_end();

	return gdb_flush_ack();
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

static int gdb_send(const char *msg)
{
	gdb_packet_start();
	gdb_printf("%s", msg);
	gdb_packet_end();
	return gdb_flush_ack();
}

/************************************************************************
 * GDB server
 */

static int read_registers(void)
{
	u_int16_t regs[DEVICE_NUM_REGS];
	int i;

	printf("Reading registers\n");
	if (device_active()->getregs(regs) < 0)
		return gdb_send("E00");

	gdb_packet_start();
	for (i = 0; i < DEVICE_NUM_REGS; i++)
		gdb_printf("%02x%02x", regs[i] & 0xff, regs[i] >> 8);
	gdb_packet_end();
	return gdb_flush_ack();
}

static int monitor_command(char *buf)
{
	char cmd[128];
	int len = 0;

	while (len + 1 < sizeof(cmd) && *buf && buf[1]) {
		cmd[len++] = (hexval(buf[0]) << 4) | hexval(buf[1]);
		buf += 2;
	}

	if (!strcasecmp(cmd, "reset")) {
		printf("Resetting device\n");
		if (device_active()->control(DEVICE_CTL_RESET) < 0)
			return gdb_send_hex("Reset failed\n");
	} else if (!strcasecmp(cmd, "erase")) {
		printf("Erasing device\n");
		if (device_active()->control(DEVICE_CTL_ERASE) < 0)
			return gdb_send_hex("Erase failed\n");
	}

	return gdb_send("OK");
}

static int write_registers(char *buf)
{
	u_int16_t regs[DEVICE_NUM_REGS];
	int i;

	if (strlen(buf) < DEVICE_NUM_REGS * 4)
		return gdb_send("E00");

	printf("Writing registers\n");
	for (i = 0; i < DEVICE_NUM_REGS; i++) {
		regs[i] = (hexval(buf[2]) << 12) |
			(hexval(buf[3]) << 8) |
			(hexval(buf[0]) << 4) |
			hexval(buf[1]);
		buf += 4;
	}

	if (device_active()->setregs(regs) < 0)
		return gdb_send("E00");

	return gdb_send("OK");
}

static int read_memory(char *text)
{
	char *length_text = strchr(text, ',');
	int length, addr;
	u_int8_t buf[128];
	int i;

	if (!length_text) {
		fprintf(stderr, "gdb: malformed memory read request\n");
		return gdb_send("E00");
	}

	*(length_text++) = 0;

	length = strtoul(length_text, NULL, 16);
	addr = strtoul(text, NULL, 16);

	if (length > sizeof(buf))
		length = sizeof(buf);

	printf("Reading %d bytes from 0x%04x\n", length, addr);

	if (device_active()->readmem(addr, buf, length) < 0)
		return gdb_send("E00");

	gdb_packet_start();
	for (i = 0; i < length; i++)
		gdb_printf("%02x", buf[i]);
	gdb_packet_end();

	return gdb_flush_ack();
}

static int write_memory(char *text)
{
	char *data_text = strchr(text, ':');
	char *length_text = strchr(text, ',');
	int length, addr;
	u_int8_t buf[128];
	int buflen = 0;

	if (!(data_text && length_text)) {
		fprintf(stderr, "gdb: malformed memory write request\n");
		return gdb_send("E00");
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
		return gdb_send("E00");
	}

	printf("Writing %d bytes to 0x%04x\n", buflen, addr);

	if (device_active()->writemem(addr, buf, buflen) < 0)
		return gdb_send("E00");

	return gdb_send("OK");
}

static int run_set_pc(char *buf)
{
	u_int16_t regs[DEVICE_NUM_REGS];

	if (!*buf)
		return 0;

	if (device_active()->getregs(regs) < 0)
		return -1;

	regs[0] = strtoul(buf, NULL, 16);
	return device_active()->setregs(regs);
}

static int run_final_status(void)
{
	u_int16_t regs[DEVICE_NUM_REGS];
	int i;

	if (device_active()->getregs(regs) < 0)
		return gdb_send("E00");

	gdb_packet_start();
	gdb_printf("T00");
	for (i = 0; i < 16; i++)
		gdb_printf("%02x:%02x%02x;", i, regs[i] & 0xff, regs[i] >> 8);
	gdb_packet_end();

	return gdb_flush_ack();
}

static int single_step(char *buf)
{
	printf("Single stepping\n");

	if (run_set_pc(buf) < 0 ||
	    device_active()->control(DEVICE_CTL_STEP) < 0)
		gdb_send("E00");

	return run_final_status();
}

static int run(char *buf)
{
	printf("Running\n");

	if (run_set_pc(buf) < 0 ||
	    device_active()->control(DEVICE_CTL_RUN) < 0) {
		gdb_send("E00");
		return run_final_status();
	}

	for (;;) {
		device_status_t status = device_active()->wait(0);

		if (status == DEVICE_STATUS_ERROR) {
			gdb_send("E00");
			return run_final_status();
		}

		if (status == DEVICE_STATUS_HALTED) {
			printf("Target halted\n");
			goto out;
		}

		if (status == DEVICE_STATUS_INTR)
			goto out;

		while (gdb_peek()) {
			int c = gdb_getc();

			if (c < 0)
				return -1;

			if (c == 3) {
				printf("Interrupted by gdb\n");
				goto out;
			}
		}
	}

 out:
	if (device_active()->control(DEVICE_CTL_HALT) < 0)
		gdb_send("E00");

	return run_final_status();
}

static int process_gdb_command(char *buf, int len)
{
	switch (buf[0]) {
	case '?': /* Return target halt reason */
		return gdb_send("T00");

	case 'g': /* Read registers */
		return read_registers();

	case 'G': /* Write registers */
		return write_registers(buf + 1);

	case 'q': /* Query */
		if (!strncmp(buf, "qRcmd,", 6))
			return monitor_command(buf + 6);
		break;

	case 'm': /* Read memory */
		return read_memory(buf + 1);

	case 'M': /* Write memory */
		return write_memory(buf + 1);

	case 'c': /* Continue */
		return run(buf + 1);

	case 's': /* Single step */
		return single_step(buf + 1);
	}

	/* For unknown/unsupported packets, return an empty reply */
	return gdb_send("");
}

static void gdb_reader_loop(void)
{
	for (;;) {
		char buf[1024];
		int len = 0;
		int cksum_calc = 0;
		int cksum_recv = 0;
		int c;

		/* Wait for packet start */
		do {
			c = gdb_getc();
			if (c < 0)
				return;
		} while (c != '$');

		/* Read packet payload */
		while (len + 1 < sizeof(buf)) {
			c = gdb_getc();
			if (c < 0)
				return;
			if (c == '#')
				break;

			buf[len++] = c;
			cksum_calc = (cksum_calc + c) & 0xff;
		}
		buf[len] = 0;

		/* Read packet checksum */
		c = gdb_getc();
		if (c < 0)
			return;
		cksum_recv = hexval(c);
		c = gdb_getc();
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
			gdb_printf("-");
			if (gdb_flush() < 0)
				return;
			continue;
		}

		/* Send acknowledgement */
		gdb_printf("+");
		if (gdb_flush() < 0)
			return;

		if (len && process_gdb_command(buf, len) < 0)
			return;
	}
}

static int gdb_server(int port)
{
	int sock;
	int client;
	struct sockaddr_in addr;
	socklen_t len;
	int arg;

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

	gdb_socket = client;
	gdb_errno = 0;
	gdb_head = 0;
	gdb_tail = 0;
	gdb_outlen = 0;

	gdb_reader_loop();

	return gdb_errno ? -1 : 0;
}

static int cmd_gdb(char **arg)
{
	char *port_text = get_arg(arg);
	int port = 2000;

	if (port_text)
		port = atoi(port_text);

	if (port <= 0 || port > 65535) {
		fprintf(stderr, "gdb: invalid port: %d\n", port);
		return -1;
	}

	return gdb_server(port);
}

static struct command command_gdb = {
	.name = "gdb",
	.func = cmd_gdb,
	.help =
	"gdb [port]\n"
	"    Run a GDB remote stub on the given TCP/IP port.\n"
};

void gdb_init(void)
{
	register_command(&command_gdb);
}
