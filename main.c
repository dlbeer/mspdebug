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
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include "dis.h"
#include "fet.h"
#include "binfile.h"
#include "stab.h"

void hexdump(int addr, const u_int8_t *data, int len)
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
	int offset = 0;
	int length = 0x40;

	if (!off_text) {
		fprintf(stderr, "md: offset must be specified\n");
		return -1;
	}

	if (stab_parse(off_text, &offset) < 0) {
		fprintf(stderr, "md: can't parse offset: %s\n", off_text);
		return -1;
	}

	if (len_text && stab_parse(len_text, &length) < 0) {
		fprintf(stderr, "md: can't parse length: %s\n", len_text);
		return -1;
	}

	if (offset < 0 || length <= 0 || (offset + length) > 0x10000) {
		fprintf(stderr, "md: memory out of range\n");
		return -1;
	}

	while (length) {
		u_int8_t buf[128];
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
	int first_line = 1;

	while (length) {
		struct msp430_instruction insn;
		int retval;
		int count;
		int i;
		u_int16_t oboff = offset;
		const char *obname;

		if (stab_find(&oboff, &obname) >= 0) {
			if (!oboff)
				printf("%s:\n", obname);
			else if (first_line)
				printf("%s+0x%x:\n", obname, oboff);
		}
		first_line = 0;

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
	int offset = 0;
	int length = 0x40;
	u_int8_t buf[128];

	if (!off_text) {
		fprintf(stderr, "md: offset must be specified\n");
		return -1;
	}

	if (stab_parse(off_text, &offset) < 0) {
		fprintf(stderr, "dis: can't parse offset: %s\n", off_text);
		return -1;
	}

	if (len_text && stab_parse(len_text, &length) < 0) {
		fprintf(stderr, "dis: can't parse length: %s\n", len_text);
		return -1;
	}

	if (offset < 0 || length <= 0 || length > sizeof(buf) ||
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
	u_int8_t code[16];

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
		int addr = 0;

		if (stab_parse(bp_text, &addr) < 0) {
			fprintf(stderr, "run: can't parse breakpoint: %s\n",
				bp_text);
			return -1;
		}

		fet_break(0, addr);
	} else {
		fet_break(0, 0);
	}

	if (fet_run(bp_text ? FET_RUN_BREAKPOINT : FET_RUN_FREE) < 0)
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
	int value = 0;
	u_int16_t regs[FET_NUM_REGS];

	if (!(reg_text && val_text)) {
		fprintf(stderr, "set: must specify a register and a value\n");
		return -1;
	}

	while (*reg_text && !isdigit(*reg_text))
		reg_text++;
	reg = atoi(reg_text);

	if (stab_parse(val_text, &value) < 0) {
		fprintf(stderr, "set: can't parse value: %s\n", val_text);
		return -1;
	}

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
	if (fet_run(FET_RUN_STEP) < 0)
		return -1;
	if (fet_poll() < 0)
		return -1;

	return cmd_regs(NULL);
}

/************************************************************************
 * Flash image programming state machine.
 */

static u_int8_t prog_buf[128];
static u_int16_t prog_addr;
static int prog_len;
static int prog_have_erased;

static void prog_init(void)
{
	prog_len = 0;
	prog_have_erased = 0;
}

static int prog_flush(void)
{
	while (prog_len) {
		int wlen = prog_len;

		/* Writing across this address seems to cause a hang */
		if (prog_addr < 0x999a && wlen + prog_addr > 0x999a)
			wlen = 0x999a - prog_addr;

		if (!prog_have_erased) {
			printf("Erasing...\n");
			if (fet_erase(FET_ERASE_ALL, 0x1000, 0x100) < 0)
				return -1;
			prog_have_erased = 1;
		}

		printf("Writing %3d bytes to %04x...\n", wlen, prog_addr);
		if (fet_write_mem(prog_addr, prog_buf, wlen) < 0)
		        return -1;

		memmove(prog_buf, prog_buf + wlen, prog_len - wlen);
		prog_len -= wlen;
		prog_addr += wlen;
	}

        return 0;
}

static int prog_feed(u_int16_t addr, const u_int8_t *data, int len)
{
	/* Flush if this section is discontiguous */
	if (prog_len && prog_addr + prog_len != addr && prog_flush() < 0)
		return -1;

	if (!prog_len)
		prog_addr = addr;

	/* Add the buffer in piece by piece, flushing when it gets
	 * full.
	 */
	while (len) {
		int count = sizeof(prog_buf) - prog_len;

		if (count > len)
			count = len;

		if (!count) {
			if (prog_flush() < 0)
				return -1;
		} else {
			memcpy(prog_buf + prog_len, data, count);
			prog_len += count;
			data += count;
			len -= count;
		}
	}

	return 0;
}

static int cmd_prog(char **arg)
{
	FILE *in = fopen(*arg, "r");
	int result = 0;

	if (!in) {
		fprintf(stderr, "prog: %s: %s\n", *arg, strerror(errno));
		return -1;
	}

	if (fet_reset(FET_RESET_ALL | FET_RESET_HALT) < 0) {
		fclose(in);
		return -1;
	}

	prog_init();

	if (elf32_check(in)) {
		result = elf32_extract(in, prog_feed);
		stab_clear();
		elf32_syms(in);
	} else if (ihex_check(in)) {
		result = ihex_extract(in, prog_feed);
	} else {
		fprintf(stderr, "%s: unknown file type\n", *arg);
	}

	if (!result)
		result = prog_flush();

	fclose(in);
	return result;
}

static int cmd_nosyms(char **arg)
{
	stab_clear();
	return 0;
}

static int cmd_eval(char **arg)
{
	int addr;
	u_int16_t offset;
	const char *name;

	if (stab_parse(*arg, &addr) < 0) {
		fprintf(stderr, "eval: can't parse: %s\n", *arg);
		return -1;
	}

	printf("0x%04x", addr);
	offset = addr;
	if (!stab_find(&offset, &name)) {
		printf(" = %s", name);
		if (offset)
			printf("+0x%x", offset);
	}
	printf("\n");

	return 0;
}

static int cmd_syms(char **arg)
{
	FILE *in = fopen(*arg, "r");
	int result = 0;

	if (!in) {
		fprintf(stderr, "syms: %s: %s\n", *arg, strerror(errno));
		return -1;
	}

	stab_clear();

	if (elf32_check(in))
		result = elf32_syms(in);
	else
		fprintf(stderr, "%s: unknown file type\n", *arg);

	fclose(in);
	return result;
}

static const struct command all_commands[] = {
	{"=",		cmd_eval,
"= <expression>\n"
"    Evaluate an expression using the symbol table.\n"},
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
	{"nosyms",	cmd_nosyms,
"nosyms\n"
"    Clear the symbol table.\n"},
	{"prog",	cmd_prog,
"prog <filename.hex>\n"
"    Erase the device and flash the data contained in a binary file.\n"},
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
	{"syms",	cmd_syms,
"syms <filename>\n"
"    Load symbols from the given file.\n"},
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

static void process_command(char *arg)
{
	const char *cmd_text;

	cmd_text = get_arg(&arg);
	if (cmd_text) {
		const struct command *cmd = find_command(cmd_text);

		if (cmd)
			cmd->func(&arg);
		else
			fprintf(stderr, "unknown command: %s (try \"help\")\n",
				cmd_text);
	}
}

static void reader_loop(void)
{
	const static struct sigaction siga = {
		.sa_handler = sigint_handler,
		.sa_flags = 0
	};

	printf("\n");
	cmd_help(NULL);
	sigaction(SIGINT, &siga, NULL);

	for (;;) {
		char buf[128];
		int len;

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

		process_command(buf);
	}

	printf("\n");
}

static void usage(const char *progname)
{
	fprintf(stderr, "Usage: %s [-u device] [-j] [command ...]\n"
"\n"
"    -u device\n"
"        Open the given tty device (MSP430 UIF compatible devices).\n"
"    -j\n"
"        Use JTAG, rather than spy-bi-wire (UIF devices only).\n"
"\n"
"By default, the first RF2500 device on the USB bus is opened.\n"
"\n"
"If commands are given, they will be executed. Otherwise, an interactive\n"
"command reader is started.\n",
	progname);
}

int main(int argc, char **argv)
{
	const char *uif_device = NULL;
	int opt;
	int result;
	int want_jtag = 0;

	puts(
"MSPDebug version 0.3 - debugging tool for the eZ430\n"
"Copyright (C) 2009, 2010 Daniel Beer <dlbeer@gmail.com>\n"
"This is free software; see the source for copying conditions.  There is NO\n"
"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n");

	while ((opt = getopt(argc, argv, "u:j")) >= 0)
		switch (opt) {
		case 'u':
			uif_device = optarg;
			break;

		case 'j':
			want_jtag = 1;
			break;

		default:
			usage(argv[0]);
			return -1;
		}

	/* Open the appropriate device */
	if (uif_device)
		result = uif_open(uif_device, want_jtag);
	else
		result = rf2500_open();

	if (result < 0)
		return -1;

	if (optind < argc) {
		while (optind < argc)
			process_command(argv[optind++]);
	} else {
		reader_loop();
	}

	fet_run(FET_RUN_FREE | FET_RUN_RELEASE);
	fet_close();

	return 0;
}
