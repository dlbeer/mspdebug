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
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include "dis.h"
#include "fet.h"
#include "rf2500.h"
#include "uif.h"

void hexdump(int addr, const char *data, int len)
{
	int offset = 0;

	while (offset < len) {
		int i, j;

		/* Address label */
		printf("    %04x:", offset + addr);

		/* Hex portion */
		for (i = 0; i < 16 && offset + i < len; i++)
			printf(" %02x",
				((const unsigned char *)data)[offset + i]);
		for (j = i; j < 16; j++)
			printf("   ");

		/* Printable characters */
		printf(" |");
		for (j = 0; j < i; j++) {
			int c = ((const unsigned char *)data)[offset + j];

			printf("%c", (c >= 32 && c <= 126) ? c : '.');
		}
		for (; j < 16; j++)
			printf(" ");
		printf("|\n");

		offset += i;
	}
}

/**********************************************************************
 * Command-line interface
 */

char *get_arg(char **text)
{
	char *start;
	char *end;

	if (!text)
		return NULL;

	start = *text;
	while (*start && isspace(*start))
		start++;

	if (!*start)
		return NULL;

	end = start;
	while (*end && !isspace(*end))
		end++;

	if (*end)
	    while (*end && isspace(*end))
		    *(end++) = 0;

	*text = end;
	return start;
}

#define REG_COLUMNS	4
#define REG_ROWS	((FET_NUM_REGS + REG_COLUMNS - 1) / REG_COLUMNS)

static void show_regs(u_int16_t *regs)
{
	int i;

	for (i = 0; i < REG_ROWS; i++) {
		int j;

		printf("    ");
		for (j = 0; j < REG_COLUMNS; j++) {
			int k = j * REG_ROWS + i;

			if (k < FET_NUM_REGS)
				printf("(r%02d: %04x)  ", k, regs[k]);
		}
		printf("\n");
	}
}

struct command {
	const char	*name;
	int		(*func)(char **arg);
	const char	*help;
};

static const struct command all_commands[];

static int cmd_help(char **arg);

static int cmd_md(char **arg)
{
	char *off_text = get_arg(arg);
	char *len_text = get_arg(arg);
	unsigned int offset = 0;
	unsigned int length = 0;

	if (!off_text) {
		fprintf(stderr, "md: offset must be specified\n");
		return -1;
	}

	sscanf(off_text, "%x", &offset);
	if (len_text)
		sscanf(len_text, "%x", &length);
	else
		length = 0x80;
	if (offset >= 0x10000 || length > 0x10000 ||
	    (offset + length) > 0x10000) {
		fprintf(stderr, "md: memory out of range\n");
		return -1;
	}

	while (length) {
		char buf[128];
		int blen = length > sizeof(buf) ? sizeof(buf) : length;

		if (fet_read_mem(offset, buf, blen) < 0)
			return -1;
		hexdump(offset, buf, blen);

		offset += blen;
		length -= blen;
	}

	return 0;
}

static void disassemble(u_int16_t offset, u_int8_t *data, int length)
{
	while (length) {
		struct msp430_instruction insn;
		int retval;
		int count;
		int i;

		retval = dis_decode(data, offset, length, &insn);
		count = retval > 0 ? retval : 2;
		if (count > length)
			count = length;
		printf("    %04x:", offset);

		for (i = 0; i < count; i++)
			printf(" %02x", data[i]);

		while (i < 8) {
			printf("   ");
			i++;
		}

		if (retval >= 0) {
			char buf[32];

			dis_format(buf, sizeof(buf), &insn);
			printf("%s", buf);
		}

		printf("\n");

		offset += count;
		length -= count;
		data += count;
	}
}

static int cmd_dis(char **arg)
{
	char *off_text = get_arg(arg);
	char *len_text = get_arg(arg);
	unsigned int offset = 0;
	unsigned int length = 0;
	char buf[128];

	if (!off_text) {
		fprintf(stderr, "md: offset must be specified\n");
		return -1;
	}

	sscanf(off_text, "%x", &offset);
	if (len_text)
		sscanf(len_text, "%x", &length);
	else
		length = 0x40;
	if (offset >= 0x10000 || length > sizeof(buf) ||
	    (offset + length) > 0x10000) {
		fprintf(stderr, "dis: memory out of range\n");
		return -1;
	}

	if (fet_read_mem(offset, buf, length) < 0)
		return -1;

	disassemble(offset, (u_int8_t *)buf, length);
	return 0;
}

static int cmd_reset(char **arg)
{
	return fet_reset(FET_RESET_ALL | FET_RESET_HALT);
}

static int cmd_regs(char **arg)
{
	u_int16_t regs[FET_NUM_REGS];
	char code[16];

	if (fet_get_context(regs) < 0)
		return -1;
	show_regs(regs);

	/* Try to disassemble the instruction at PC */
	if (fet_read_mem(regs[0], code, sizeof(code)) < 0)
		return 0;

	disassemble(regs[0], (u_int8_t *)code, sizeof(code));
	return 0;
}

static int cmd_run(char **arg)
{
	char *bp_text = get_arg(arg);

	if (bp_text) {
		unsigned int addr = 0;

		sscanf(bp_text, "%x", &addr);
		fet_break(1, addr);
	} else {
		fet_break(0, 0);
	}

	if (fet_run() < 0)
		return -1;

	printf("Running. Press Ctrl+C to interrupt...");
	fflush(stdout);

	for (;;) {
		int r = fet_poll();

		if (r < 0 || !(r & FET_POLL_RUNNING))
			break;
	}

	printf("\n");
	if (fet_stop() < 0)
		return -1;

	return cmd_regs(NULL);
}

static int cmd_set(char **arg)
{
	char *reg_text = get_arg(arg);
	char *val_text = get_arg(arg);
	int reg;
	unsigned int value = 0;
	u_int16_t regs[FET_NUM_REGS];

	if (!(reg_text && val_text)) {
		fprintf(stderr, "set: must specify a register and a value\n");
		return -1;
	}

	while (*reg_text && !isdigit(*reg_text))
		reg_text++;
	reg = atoi(reg_text);
	sscanf(val_text, "%x", &value);

	if (reg < 0 || reg >= FET_NUM_REGS) {
		fprintf(stderr, "set: register out of range: %d\n", reg);
		return -1;
	}

	if (fet_get_context(regs) < 0)
		return -1;
	regs[reg] = value;
	if (fet_set_context(regs) < 0)
		return -1;

	show_regs(regs);
	return 0;
}

static int cmd_step(char **arg)
{
	if (fet_step() < 0)
		return -1;
	if (fet_poll() < 0)
		return -1;

	return cmd_regs(NULL);
}

static int hexval(const char *text, int len)
{
	int value = 0;

	while (len && *text) {
		value <<= 4;

		if (*text >= 'A' && *text <= 'F')
			value += *text - 'A' + 10;
		else if (*text >= 'a' && *text <= 'f')
			value += *text - 'a' + 10;
		else if (isdigit(*text))
			value += *text - '0';

		text++;
		len--;
	}

	return value;
}

static char prog_buf[128];
static u_int16_t prog_addr;
static int prog_len;

static int prog_flush(void)
{
	int wlen = prog_len;

	if (!prog_len)
		return 0;

	/* Writing across this address seems to cause a hang */
	if (prog_addr < 0x999a && wlen + prog_addr > 0x999a)
		wlen = 0x999a - prog_addr;

	printf("Writing %3d bytes to %04x...\n", wlen, prog_addr);

	if (fet_write_mem(prog_addr, prog_buf, wlen) < 0)
		return -1;

	memmove(prog_buf, prog_buf + wlen, prog_len - wlen);
	prog_len -= wlen;
	prog_addr += wlen;

	return 0;
}

static int prog_hex(int lno, const char *hex)
{
	int len = strlen(hex);
	int count, address, type, cksum = 0;
	int i;

	if (*hex != ':')
		return 0;

	hex++;
	len--;

	while (len && isspace(hex[len - 1]))
		len--;

	if (len < 10)
		return 0;

	count = hexval(hex, 2);
	address = hexval(hex + 2, 4);
	type = hexval(hex + 6, 2);

	if (type)
		return 0;

	for (i = 0; i + 2 < len; i += 2)
		cksum = (cksum + hexval(hex + i, 2))
			& 0xff;
	cksum = ~(cksum - 1) & 0xff;

	if (count * 2 + 10 != len) {
		fprintf(stderr, "warning: length mismatch at line %d\n", lno);
		count = (len - 10) / 2;
	}

	if (cksum != hexval(hex + len - 2, 2))
		fprintf(stderr, "warning: invalid checksum at line %d\n", lno);

	for (i = 0; i < count; i++) {
		int offset;

		offset = address + i - prog_addr;
		if (offset < 0 || offset >= sizeof(prog_buf))
			if (prog_flush() < 0)
				return -1;

		if (!prog_len)
			prog_addr = address + i;

		offset = address + i - prog_addr;
		prog_buf[offset] = hexval(hex + 8 + i * 2, 2);
		if (offset + 1 > prog_len)
			prog_len = offset + 1;
	}

	return 0;
}

static int cmd_prog(char **arg)
{
	FILE *in = fopen(*arg, "r");
	char text[256];
	int lno = 1;

	if (!in) {
		fprintf(stderr, "prog: %s: %s\n", *arg, strerror(errno));
		return -1;
	}

	printf("Erasing...\n");
	if (fet_erase(FET_ERASE_ALL, 0) < 0) {
		fclose(in);
		return -1;
	}

	if (fet_reset(FET_RESET_ALL | FET_RESET_HALT) < 0)
		return -1;

	prog_len = 0;
	while (fgets(text, sizeof(text), in))
		if (prog_hex(lno++, text) < 0) {
			fclose(in);
			return -1;
		}
	fclose(in);

	if (prog_flush() < 0)
		return -1;

	return fet_reset(FET_RESET_ALL | FET_RESET_HALT);
}

static const struct command all_commands[] = {
	{"dis",		cmd_dis,
"dis <address> <range>\n"
"    Disassemble a section of memory.\n"},
	{"help",	cmd_help,
"help [command]\n"
"    Without arguments, displays a list of commands. With a command name as\n"
"    an argument, displays help for that command.\n"},
	{"md",		cmd_md,
"md <address> <length>\n"
"    Read the specified number of bytes from memory at the given address,\n"
"    and display a hexdump.\n"},
	{"prog",	cmd_prog,
"prog <filename.hex>\n"
"    Erase the device and flash the data contained in an Intel HEX file.\n"},
	{"regs",	cmd_regs,
"regs\n"
"    Read and display the current register contents.\n"},
	{"reset",	cmd_reset,
"reset\n"
"    Reset (and halt) the CPU.\n"},
	{"run",		cmd_run,
"run [breakpoint]\n"
"    Run the CPU until either a specified breakpoint occurs or the command\n"
"    is interrupted.\n"},
	{"set",		cmd_set,
"set <register> <value>\n"
"    Change the value of a CPU register.\n"},
	{"step",	cmd_step,
"step\n"
"    Single-step the CPU, and display the register state.\n"},
};

#define NUM_COMMANDS (sizeof(all_commands) / sizeof(all_commands[0]))

const struct command *find_command(const char *name)
{
	int i;

	for (i = 0; i < NUM_COMMANDS; i++)
		if (!strcasecmp(name, all_commands[i].name))
			return &all_commands[i];

	return NULL;
}

static int cmd_help(char **arg)
{
	char *topic = get_arg(arg);

	if (topic) {
		const struct command *cmd = find_command(topic);

		if (!cmd) {
			fprintf(stderr, "help: unknown command: %s\n", topic);
			return -1;
		}

		fputs(cmd->help, stdout);
	} else {
		int i;

		printf("Available commands:");
		for (i = 0; i < NUM_COMMANDS; i++)
			printf(" %s", all_commands[i].name);
		printf("\n");
		printf("Type \"help <command>\" for more information.\n");
		printf("Press Ctrl+D to quit.\n");
	}

	return 0;
}

static void sigint_handler(int signum)
{
}

static void reader_loop(void)
{
	const static struct sigaction siga = {
		.sa_handler = sigint_handler,
		.sa_flags = 0
	};

	cmd_help(NULL);
	sigaction(SIGINT, &siga, NULL);

	for (;;) {
		char buf[128];
		int len;
		char *arg = buf;
		char *cmd_text;

		printf("(mspdebug) ");
		fflush(stdout);
		if (!fgets(buf, sizeof(buf), stdin)) {
			if (feof(stdin))
				break;
			printf("\n");
			continue;
		}

		len = strlen(buf);
		while (len && isspace(buf[len - 1]))
			len--;
		buf[len] = 0;

		cmd_text = get_arg(&arg);
		if (cmd_text) {
			const struct command *cmd = find_command(cmd_text);

			if (cmd)
				cmd->func(&arg);
			else
				fprintf(stderr, "unknown command: %s "
						"(try \"help\")\n",
					cmd_text);
		}
	}

	printf("\n");
}

static void usage(const char *progname)
{
	fprintf(stderr, "Usage: %s [-u device]\n"
"By default, the first RF2500 device on the USB bus is opened. If -u is\n"
"given, then a UIF device attached to the specified serial port is\n"
"opened.\n", progname);
}

int main(int argc, char **argv)
{
	const char *uif_device = NULL;
	int opt;
	int result;

	puts(
"MSPDebug version 0.1+ - debugging tool for the eZ430\n"
"Copyright (C) 2009 Daniel Beer <dlbeer@gmail.com>\n"
"This is free software; see the source for copying conditions.  There is NO\n"
"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n");

	while ((opt = getopt(argc, argv, "u:")) >= 0)
		switch (opt) {
		case 'u':
			uif_device = optarg;
			break;

		default:
			usage(argv[0]);
			return -1;
		}

	/* Open the appropriate device */
	if (uif_device)
		result = uif_open(uif_device);
	else
		result = rf2500_open();

	if (result < 0)
		return -1;

        reader_loop();
	fet_run();
	fet_close();

	return 0;
}
