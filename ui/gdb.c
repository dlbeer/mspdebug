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
#include <unistd.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>

#include "sockets.h"
#include "device.h"
#include "util.h"
#include "opdb.h"
#include "gdb.h"
#include "output.h"
#include "reader.h"
#include "expr.h"
#include "gdb_proto.h"
#include "ctrlc.h"

static int register_bytes;

/************************************************************************
 * GDB server
 */

static int read_registers(struct gdb_data *data)
{
	address_t regs[DEVICE_NUM_REGS];
	int i;

	printc("Reading registers\n");
	if (device_getregs(regs) < 0)
		return gdb_send(data, "E00");

	gdb_packet_start(data);

	for (i = 0; i < DEVICE_NUM_REGS; i++) {
		address_t value = regs[i];
		int j;

		for (j = 0; j < register_bytes; j++) {
			gdb_printf(data, "%02x", value & 0xff);
			value >>= 8;
		}
	}

	gdb_packet_end(data);
	return gdb_flush_ack(data);
}

struct monitor_buf {
	char    buf[GDB_MAX_XFER];
	int     len;
	int	trunc;
};

static void monitor_capture(void *user_data, const char *text)
{
	struct monitor_buf *mb = (struct monitor_buf *)user_data;
	int len = strlen(text);

	if (mb->trunc)
		return;

	if (mb->len + len + 64 > sizeof(mb->buf)) {
		text = "...<truncated>";
		len = strlen(text);
		mb->trunc = 1;
	}

	memcpy(mb->buf + mb->len, text, len);
	mb->len += len;
	mb->buf[mb->len++] = '\n';
}

static int monitor_command(struct gdb_data *data, char *buf)
{
	char cmd[128];
	int len = 0;
	int i;
	struct monitor_buf mbuf;

	while (len + 1 < sizeof(cmd) && *buf && buf[1]) {
		if (len + 1 >= sizeof(cmd))
			break;

		cmd[len++] = (hexval(buf[0]) << 4) | hexval(buf[1]);
		buf += 2;
	}
	cmd[len] = 0;

	printc("Monitor command received: %s\n", cmd);

	mbuf.len = 0;
	mbuf.trunc = 0;
	capture_start(monitor_capture, &mbuf);
	process_command(cmd);
	capture_end();

	if (!mbuf.len)
		return gdb_send(data, "OK");

	gdb_packet_start(data);
	for (i = 0; i < mbuf.len; i++)
		gdb_printf(data, "%02x", mbuf.buf[i]);
	gdb_packet_end(data);

	return gdb_flush_ack(data);
}

static int write_registers(struct gdb_data *data, char *buf)
{
	address_t regs[DEVICE_NUM_REGS];
	int i;

	if (strlen(buf) < DEVICE_NUM_REGS * 4)
		return gdb_send(data, "E00");

	printc("Writing registers\n");
	for (i = 0; i < DEVICE_NUM_REGS; i++) {
		regs[i] = (hexval(buf[2]) << 12) |
			(hexval(buf[3]) << 8) |
			(hexval(buf[0]) << 4) |
			hexval(buf[1]);
		buf += 4;
	}

	if (device_setregs(regs) < 0)
		return gdb_send(data, "E00");

	return gdb_send(data, "OK");
}

static int read_memory(struct gdb_data *data, char *text)
{
	char *length_text = strchr(text, ',');
	address_t length, addr;
	uint8_t buf[GDB_MAX_XFER];
	int i;

	if (!length_text) {
		printc_err("gdb: malformed memory read request\n");
		return gdb_send(data, "E00");
	}

	*(length_text++) = 0;

	length = strtoul(length_text, NULL, 16);
	addr = strtoul(text, NULL, 16);

	if (length > sizeof(buf))
		length = sizeof(buf);

	printc("Reading %4d bytes from 0x%04x\n", length, addr);

	if (device_readmem(addr, buf, length) < 0)
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
	address_t length, addr;
	uint8_t buf[GDB_MAX_XFER];
	int buflen = 0;

	if (!(data_text && length_text)) {
		printc_err("gdb: malformed memory write request\n");
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
		printc_err("gdb: length mismatch\n");
		return gdb_send(data, "E00");
	}

	printc("Writing %4d bytes to 0x%04x\n", length, addr);

	if (device_writemem(addr, buf, buflen) < 0)
		return gdb_send(data, "E00");

	return gdb_send(data, "OK");
}

static int run_set_pc(char *buf)
{
	address_t regs[DEVICE_NUM_REGS];

	if (!*buf)
		return 0;

	if (device_getregs(regs) < 0)
		return -1;

	regs[0] = strtoul(buf, NULL, 16);
	return device_setregs(regs);
}

static int run_final_status(struct gdb_data *data)
{
	address_t regs[DEVICE_NUM_REGS];
	int i;

	if (device_getregs(regs) < 0)
		return gdb_send(data, "E00");

	gdb_packet_start(data);
	gdb_printf(data, "T05");
	for (i = 0; i < 16; i++) {
		address_t value = regs[i];
		int j;

		/* NOTE: this only gives GDB the lower 16 bits of each
		 *       register. It complains if we give the full data.
		 */
		gdb_printf(data, "%02x:", i);
		for (j = 0; j < register_bytes; j++) {
			gdb_printf(data, "%02x", value & 0xff);
			value >>= 8;
		}
		gdb_printf(data, ";");
	}
	gdb_packet_end(data);

	return gdb_flush_ack(data);
}

static int single_step(struct gdb_data *data, char *buf)
{
	printc("Single stepping\n");

	if (run_set_pc(buf) < 0 ||
	    device_ctl(DEVICE_CTL_STEP) < 0)
		gdb_send(data, "E00");

	return run_final_status(data);
}

static int run(struct gdb_data *data, char *buf)
{
	printc("Running\n");

	if (run_set_pc(buf) < 0 ||
	    device_ctl(DEVICE_CTL_RUN) < 0)
		return gdb_send(data, "E00");

	for (;;) {
		device_status_t status = device_poll();

		if (status == DEVICE_STATUS_ERROR)
			return gdb_send(data, "E00");

		if (status == DEVICE_STATUS_HALTED) {
			printc("Target halted\n");
			goto out;
		}

		if (status == DEVICE_STATUS_INTR)
			goto out;

		while (gdb_peek(data, 0)) {
			int c = gdb_getc(data);

			if (c < 0)
				return -1;

			if (c == 3) {
				printc("Interrupted by gdb\n");
				goto out;
			}
		}
	}

 out:
	if (device_ctl(DEVICE_CTL_HALT) < 0)
		return gdb_send(data, "E00");

	return run_final_status(data);
}

static int set_breakpoint(struct gdb_data *data, int enable, char *buf)
{
	char *parts[2];
	address_t addr;
	device_bptype_t type;
	int i;

	/* Break up the arguments */
	for (i = 0; i < 2; i++)
		parts[i] = strsep(&buf, ",");

	/* Make sure there's a type argument */
	if (!parts[0]) {
		printc_err("gdb: breakpoint requested with no type\n");
		return gdb_send(data, "E00");
	}

	switch (atoi(parts[0])) {
	case 0:
	case 1:
		type = DEVICE_BPTYPE_BREAK;
		break;

	case 2:
		type = DEVICE_BPTYPE_WRITE;
		break;

	case 3:
		type = DEVICE_BPTYPE_READ;
		break;

	case 4:
		type = DEVICE_BPTYPE_WATCH;
		break;

	default:
		printc_err("gdb: unsupported breakpoint type: %s\n",
			parts[0]);
		return gdb_send(data, "");
	}

	/* There needs to be an address specified */
	if (!parts[1]) {
		printc_err("gdb: breakpoint address missing\n");
		return gdb_send(data, "E00");
	}

	/* Parse the breakpoint address */
	addr = strtoul(parts[1], NULL, 16);

	if (enable) {
		if (device_setbrk(device_default, -1, 1, addr, type) < 0) {
			printc_err("gdb: can't add breakpoint at "
				"0x%04x\n", addr);
			return gdb_send(data, "E00");
		}

		printc("Breakpoint set at 0x%04x\n", addr);
	} else {
		device_setbrk(device_default, -1, 0, addr, type);
		printc("Breakpoint cleared at 0x%04x\n", addr);
	}

	return gdb_send(data, "OK");
}

static int restart_program(struct gdb_data *data)
{
	if (device_ctl(DEVICE_CTL_RESET) < 0)
		return gdb_send(data, "E00");

	return gdb_send(data, "OK");
}

static int gdb_send_empty_threadlist(struct gdb_data *data)
{
	return gdb_send(data, "<?xml version=\"1.0\"?><threads></threads>");
}

static int gdb_send_supported(struct gdb_data *data)
{
	gdb_packet_start(data);
	gdb_printf(data, "PacketSize=%x", GDB_MAX_XFER * 2);
	gdb_packet_end(data);
	return gdb_flush_ack(data);
}

static int process_gdb_command(struct gdb_data *data, char *buf)
{
#ifdef DEBUG_GDB
	printc("process_gdb_command: %s\n", buf);
#endif
	switch (buf[0]) {
	case '?': /* Return target halt reason */
		return run_final_status(data);

	case 'z':
	case 'Z':
		return set_breakpoint(data, buf[0] == 'Z', buf + 1);

	case 'r': /* Restart */
	case 'R':
		return restart_program(data);

	case 'g': /* Read registers */
		return read_registers(data);

	case 'G': /* Write registers */
		return write_registers(data, buf + 1);

	case 'q': /* Query */
		if (!strncmp(buf, "qRcmd,", 6))
			return monitor_command(data, buf + 6);
		if (!strncmp(buf, "qSupported", 10)) {
			/* This is a hack to distinguish msp430-elf-gdb
			 * from msp430-gdb. The former expects 32-bit
			 * register fields.
			 */
			if (strstr(buf, "multiprocess+"))
				register_bytes = 4;

			return gdb_send_supported(data);
		}
		if (!strncmp(buf, "qfThreadInfo", 12))
			return gdb_send_empty_threadlist(data);
		break;

	case 'm': /* Read memory */
		return read_memory(data, buf + 1);

	case 'M': /* Write memory */
		return write_memory(data, buf + 1);

	case 'c': /* Continue */
		return run(data, buf + 1);

	case 's': /* Single step */
		return single_step(data, buf + 1);
	case 'k': /* kill */
		return -1;
	}

#ifdef DEBUG_GDB
	printc("process_gdb_command: unknown command %s\n", buf);
#endif

	/* For unknown/unsupported packets, return an empty reply */
	return gdb_send(data, "");
}

static void gdb_reader_loop(struct gdb_data *data)
{
	while (!ctrlc_check()) {
		char buf[GDB_BUF_SIZE];
		int len = 0;

		len = gdb_read_packet(data, buf);
		if (len < 0)
			return;
		if (len && process_gdb_command(data, buf) < 0)
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
	struct gdb_data data;
	int i;

	sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (SOCKET_ISERR(sock)) {
		pr_error("gdb: can't create socket");
		return -1;
	}

	arg = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
		       (void *)&arg, sizeof(arg)) < 0)
		pr_error("gdb: warning: can't reuse socket address");

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		printc_err("gdb: can't bind to port %d: %s\n",
			port, last_error());
		closesocket(sock);
		return -1;
	}

	if (listen(sock, 1) < 0) {
		pr_error("gdb: can't listen on socket");
		closesocket(sock);
		return -1;
	}

	printc("Bound to port %d. Now waiting for connection...\n", port);

	len = sizeof(addr);
	client = sockets_accept(sock, (struct sockaddr *)&addr, &len);
	if (SOCKET_ISERR(client)) {
		pr_error("gdb: failed to accept connection");
		closesocket(sock);
		return -1;
	}

	closesocket(sock);
	printc("Client connected from %s:%d\n",
	       inet_ntoa(addr.sin_addr), htons(addr.sin_port));

	register_bytes = 2;
	gdb_init(&data, client);

	/* Put the hardware breakpoint setting into a known state. */
	printc("Clearing all breakpoints...\n");
	for (i = 0; i < device_default->max_breakpoints; i++)
		device_setbrk(device_default, i, 0, 0, 0);

#ifdef DEBUG_GDB
	printc("starting GDB reader loop...\n");
#endif
	gdb_reader_loop(&data);
#ifdef DEBUG_GDB
	printc("... reader loop returned\n");
#endif
	closesocket(client);

	return data.error ? -1 : 0;
}

int cmd_gdb(char **arg)
{
	char *port_text = get_arg(arg);
	address_t port = opdb_get_numeric("gdb_default_port");

	if (port_text && expr_eval(port_text, &port) < 0) {
		printc_err("gdb: can't parse port: %s\n", port_text);
		return -1;
	}

	if (port <= 0 || port > 65535) {
		printc_err("gdb: invalid port: %d\n", port);
		return -1;
	}

	do {
		if (gdb_server(port) < 0)
			return -1;
	} while (opdb_get_boolean("gdb_loop"));

	return 0;
}
