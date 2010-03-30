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
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "dis.h"
#include "device.h"
#include "binfile.h"
#include "stab.h"
#include "util.h"
#include "gdb.h"

static const struct device *msp430_dev;

/**********************************************************************
 * Modification tracking and prompting
 */

static int syms_are_modified;

static int syms_modify_check(voiv)
{
	char buf[32];

	if (!syms_are_modified)
		return 0;

	for (;;) {
		printf("Symbols have not been saved since modification. "
		       "Continue (y/n)? ");
		fflush(stdout);

		if (!fgets(buf, sizeof(buf), stdin)) {
			printf("\n");
			return 1;
		}

		if (toupper(buf[0]) == 'Y')
			return 0;
		if (toupper(buf[0]) == 'N')
			return 1;

		printf("Please answer \"y\" or \"n\".\n");
	}

	return 0;
}

/************************************************************************
 * Command definitions
 */

#define REG_COLUMNS	4
#define REG_ROWS	((DEVICE_NUM_REGS + REG_COLUMNS - 1) / REG_COLUMNS)

static void show_regs(u_int16_t *regs)
{
	int i;

	for (i = 0; i < REG_ROWS; i++) {
		int j;

		printf("    ");
		for (j = 0; j < REG_COLUMNS; j++) {
			int k = j * REG_ROWS + i;

			if (k < DEVICE_NUM_REGS)
				printf("(r%02d: %04x)  ", k, regs[k]);
		}
		printf("\n");
	}
}

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

	if (addr_exp(off_text, &offset) < 0) {
		fprintf(stderr, "md: can't parse offset: %s\n", off_text);
		return -1;
	}

	if (len_text) {
		if (addr_exp(len_text, &length) < 0) {
			fprintf(stderr, "md: can't parse length: %s\n",
				len_text);
			return -1;
		}
	} else if (offset + length > 0x10000) {
		length = 0x10000 - offset;
	}

	if (offset < 0 || length <= 0 || (offset + length) > 0x10000) {
		fprintf(stderr, "md: memory out of range\n");
		return -1;
	}

	while (length) {
		u_int8_t buf[128];
		int blen = length > sizeof(buf) ? sizeof(buf) : length;

		if (msp430_dev->readmem(offset, buf, blen) < 0)
			return -1;
		hexdump(offset, buf, blen);

		offset += blen;
		length -= blen;
	}

	return 0;
}

static int cmd_mw(char **arg)
{
	char *off_text = get_arg(arg);
	char *byte_text;
	int offset = 0;
	int length = 0;
	u_int8_t buf[1024];

	if (!off_text) {
		fprintf(stderr, "md: offset must be specified\n");
		return -1;
	}

	if (addr_exp(off_text, &offset) < 0) {
		fprintf(stderr, "md: can't parse offset: %s\n", off_text);
		return -1;
	}

	while ((byte_text = get_arg(arg))) {
		if (length >= sizeof(buf)) {
			fprintf(stderr, "md: maximum length exceeded\n");
			return -1;
		}

		buf[length++] = strtoul(byte_text, NULL, 16);
	}

	if (!length)
		return 0;

	if (offset < 0 || (offset + length) > 0x10000) {
		fprintf(stderr, "md: memory out of range\n");
		return -1;
	}

	if (msp430_dev->writemem(offset, buf, length) < 0)
		return -1;

	return 0;
}

static int cmd_dis(char **arg)
{
	char *off_text = get_arg(arg);
	char *len_text = get_arg(arg);
	int offset = 0;
	int length = 0x40;
	u_int8_t buf[512];

	if (!off_text) {
		fprintf(stderr, "md: offset must be specified\n");
		return -1;
	}

	if (addr_exp(off_text, &offset) < 0) {
		fprintf(stderr, "dis: can't parse offset: %s\n", off_text);
		return -1;
	}

	if (len_text) {
		if (addr_exp(len_text, &length) < 0) {
			fprintf(stderr, "dis: can't parse length: %s\n",
				len_text);
			return -1;
		}
	} else if (offset + length > 0x10000) {
		length = 0x10000 - offset;
	}

	if (offset < 0 || length <= 0 || length > sizeof(buf) ||
	    (offset + length) > 0x10000) {
		fprintf(stderr, "dis: memory out of range\n");
		return -1;
	}

	if (msp430_dev->readmem(offset, buf, length) < 0)
		return -1;

	disassemble(offset, (u_int8_t *)buf, length);
	return 0;
}

static FILE *hexout_file;
static u_int16_t hexout_addr;
static u_int8_t hexout_buf[16];
static int hexout_len;

static int hexout_start(const char *filename)
{
	hexout_file = fopen(filename, "w");
	if (!hexout_file) {
		perror("hexout: couldn't open output file");
		return -1;
	}

	return 0;
}

static int hexout_flush(void)
{
	int i;
	int cksum = 0;

	if (!hexout_len)
		return 0;

	if (fprintf(hexout_file, ":%02X%04X00", hexout_len, hexout_addr) < 0)
		goto fail;
	cksum += hexout_len;
	cksum += hexout_addr & 0xff;
	cksum += hexout_addr >> 8;

	for (i = 0; i < hexout_len; i++) {
		if (fprintf(hexout_file, "%02X", hexout_buf[i]) < 0)
			goto fail;
		cksum += hexout_buf[i];
	}

	if (fprintf(hexout_file, "%02X\n", ~(cksum - 1) & 0xff) < 0)
		goto fail;

	hexout_len = 0;
	return 0;

fail:
	perror("hexout: can't write HEX data");
	return -1;
}

static int hexout_feed(u_int16_t addr, const u_int8_t *buf, int len)
{
	while (len) {
		int count;

		if ((hexout_addr + hexout_len != addr ||
		     hexout_len >= sizeof(hexout_buf)) &&
		    hexout_flush() < 0)
			return -1;

		if (!hexout_len)
			hexout_addr = addr;

		count = sizeof(hexout_buf) - hexout_len;
		if (count > len)
			count = len;

		memcpy(hexout_buf + hexout_len, buf, count);
		hexout_len += count;

		addr += count;
		buf += count;
		len -= count;
	}

	return 0;
}

static int cmd_hexout(char **arg)
{
	char *off_text = get_arg(arg);
	char *len_text = get_arg(arg);
	char *filename = *arg;
	int off;
	int length;

	if (!(off_text && len_text && *filename)) {
		fprintf(stderr, "hexout: need offset, length and filename\n");
		return -1;
	}

	if (addr_exp(off_text, &off) < 0 ||
	    addr_exp(len_text, &length) < 0)
		return -1;

	if (hexout_start(filename) < 0)
		return -1;

	while (length) {
		u_int8_t buf[128];
		int count = length;

		if (count > sizeof(buf))
			count = sizeof(buf);

		printf("Reading %d bytes from 0x%04x...\n", count, off);
		if (msp430_dev->readmem(off, buf, count) < 0) {
			perror("hexout: can't read memory");
			goto fail;
		}

		if (hexout_feed(off, buf, count) < 0)
			goto fail;

		length -= count;
		off += count;
	}

	if (hexout_flush() < 0)
		goto fail;
	if (fclose(hexout_file) < 0) {
		perror("hexout: error on close");
		return -1;
	}

	return 0;

fail:
	fclose(hexout_file);
	unlink(filename);
	return -1;
}

static int cmd_reset(char **arg)
{
	return msp430_dev->control(DEVICE_CTL_RESET);
}

static int cmd_regs(char **arg)
{
	u_int16_t regs[DEVICE_NUM_REGS];
	u_int8_t code[16];

	if (msp430_dev->getregs(regs) < 0)
		return -1;
	show_regs(regs);

	/* Try to disassemble the instruction at PC */
	if (msp430_dev->readmem(regs[0], code, sizeof(code)) < 0)
		return 0;

	disassemble(regs[0], (u_int8_t *)code, sizeof(code));
	return 0;
}

static int cmd_run(char **arg)
{
	char *bp_text = get_arg(arg);
	int bp_addr;
	device_status_t status;

	if (bp_text) {
		if (addr_exp(bp_text, &bp_addr) < 0) {
			fprintf(stderr, "run: can't parse breakpoint: %s\n",
				bp_text);
			return -1;
		}

		msp430_dev->breakpoint(bp_addr);
	}

	if (msp430_dev->control(bp_text ?
		DEVICE_CTL_RUN_BP : DEVICE_CTL_RUN) < 0)
		return -1;

	if (bp_text)
		printf("Running to 0x%04x.", bp_addr);
	else
		printf("Running.");
	printf(" Press Ctrl+C to interrupt...\n");

	status = msp430_dev->wait(1);
	if (status == DEVICE_STATUS_INTR)
		printf("\n");

	if (msp430_dev->control(DEVICE_CTL_HALT) < 0)
		return -1;

	return cmd_regs(NULL);
}

static int cmd_set(char **arg)
{
	char *reg_text = get_arg(arg);
	char *val_text = get_arg(arg);
	int reg;
	int value = 0;
	u_int16_t regs[DEVICE_NUM_REGS];

	if (!(reg_text && val_text)) {
		fprintf(stderr, "set: must specify a register and a value\n");
		return -1;
	}

	while (*reg_text && !isdigit(*reg_text))
		reg_text++;
	reg = atoi(reg_text);

	if (addr_exp(val_text, &value) < 0) {
		fprintf(stderr, "set: can't parse value: %s\n", val_text);
		return -1;
	}

	if (reg < 0 || reg >= DEVICE_NUM_REGS) {
		fprintf(stderr, "set: register out of range: %d\n", reg);
		return -1;
	}

	if (msp430_dev->getregs(regs) < 0)
		return -1;
	regs[reg] = value;
	if (msp430_dev->setregs(regs) < 0)
		return -1;

	show_regs(regs);
	return 0;
}

static int cmd_step(char **arg)
{
	char *count_text = get_arg(arg);
	int count = 1;

	if (count_text)
		count = atoi(count_text);

	while (count > 0) {
		if (msp430_dev->control(DEVICE_CTL_STEP) < 0)
			return -1;
		count--;
	}

	return cmd_regs(NULL);
}

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
			if (msp430_dev->control(DEVICE_CTL_ERASE) < 0)
				return -1;
			prog_have_erased = 1;
		}

		printf("Writing %3d bytes to %04x...\n", wlen, prog_addr);
		if (msp430_dev->writemem(prog_addr, prog_buf, wlen) < 0)
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

static int cmd_erase(char **arg)
{
	if (msp430_dev->control(DEVICE_CTL_HALT) < 0)
		return -1;

	printf("Erasing...\n");
	return msp430_dev->control(DEVICE_CTL_ERASE);
}

static int cmd_prog(char **arg)
{
	FILE *in;
	int result = 0;

	if (is_interactive() && syms_modify_check())
		return 0;

	in = fopen(*arg, "r");
	if (!in) {
		fprintf(stderr, "prog: %s: %s\n", *arg, strerror(errno));
		return -1;
	}

	if (msp430_dev->control(DEVICE_CTL_HALT) < 0) {
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
		fprintf(stderr, "prog: %s: unknown file type\n", *arg);
	}

	fclose(in);

	if (prog_flush() < 0)
		return -1;

	if (msp430_dev->control(DEVICE_CTL_RESET) < 0) {
		fprintf(stderr, "prog: failed to reset after programming\n");
		return -1;
	}

	syms_are_modified = 0;
	return result;
}

static int cmd_eval(char **arg)
{
	int addr;
	u_int16_t offset;
	char name[64];

	if (addr_exp(*arg, &addr) < 0) {
		fprintf(stderr, "=: can't parse: %s\n", *arg);
		return -1;
	}

	printf("0x%04x", addr);
	if (!stab_nearest(addr, name, sizeof(name), &offset)) {
		printf(" = %s", name);
		if (offset)
			printf("+0x%x", offset);
	}
	printf("\n");

	return 0;
}

static int cmd_sym_load_add(int clear, char **arg)
{
	FILE *in;
	int result = 0;

	if (clear && is_interactive() && syms_modify_check())
		return 0;

	in = fopen(*arg, "r");
	if (!in) {
		fprintf(stderr, "sym: %s: %s\n", *arg, strerror(errno));
		return -1;
	}

	if (clear)
		stab_clear();

	if (elf32_check(in))
		result = elf32_syms(in);
	else if (symmap_check(in))
		result = symmap_syms(in);
	else
		fprintf(stderr, "sym: %s: unknown file type\n", *arg);

	fclose(in);

	syms_are_modified = !clear;
	return result;
}

static FILE *savemap_out;

static int savemap_write(const char *name, u_int16_t value)
{
	if (fprintf(savemap_out, "%08x t %s\n", value, name) < 0) {
		fprintf(stderr, "sym: error writing symbols: %s\n",
			strerror(errno));
		return -1;
	}

	return 0;
}

static int cmd_sym_savemap(char **arg)
{
	char *fname = get_arg(arg);

	if (!fname) {
		fprintf(stderr, "sym: filename required to save map\n");
		return -1;
	}

	savemap_out = fopen(fname, "w");
	if (!savemap_out) {
		fprintf(stderr, "sym: couldn't write to %s: %s\n", fname,
			strerror(errno));
		return -1;
	}

	if (stab_enum(savemap_write) < 0)
		return -1;

	if (fclose(savemap_out) < 0) {
		fprintf(stderr, "sym: error closing %s: %s\n", fname,
			strerror(errno));
		return -1;
	}

	syms_are_modified = 0;
	return 0;
}

static int printsym(const char *name, u_int16_t value)
{
	printf("0x%04x: %s\n", value, name);
	return 0;
}

static int cmd_sym(char **arg)
{
	char *subcmd = get_arg(arg);

	if (!subcmd) {
		fprintf(stderr, "sym: need to specify a subcommand "
			"(try \"help sym\")\n");
		return -1;
	}

	if (!strcasecmp(subcmd, "clear")) {
		if (is_interactive() && syms_modify_check())
			return 0;
		stab_clear();
		syms_are_modified = 0;
		return 0;
	}

	if (!strcasecmp(subcmd, "set")) {
		char *name = get_arg(arg);
		char *val_text = get_arg(arg);
		int value;

		if (!(name && val_text)) {
			fprintf(stderr, "sym: need a name and value to set "
				"symbol table entries\n");
			return -1;
		}

		if (addr_exp(val_text, &value) < 0) {
			fprintf(stderr, "sym: can't parse value: %s\n",
				val_text);
			return -1;
		}

		if (stab_set(name, value) < 0)
			return -1;

		syms_are_modified = 1;
		return 0;
	}

	if (!strcasecmp(subcmd, "del")) {
		char *name = get_arg(arg);

		if (!name) {
			fprintf(stderr, "sym: need a name to delete "
				"symbol table entries\n");
			return -1;
		}

		if (stab_del(name) < 0)
			return -1;

		syms_are_modified = 1;
		return 0;
	}

	if (!strcasecmp(subcmd, "load"))
		return cmd_sym_load_add(1, arg);
	if (!strcasecmp(subcmd, "add"))
		return cmd_sym_load_add(0, arg);

	if (!strcasecmp(subcmd, "savemap"))
		return cmd_sym_savemap(arg);

	if (!strcasecmp(subcmd, "find")) {
		char *expr = get_arg(arg);

		if (!expr) {
			stab_enum(printsym);
			return 0;
		}

		return stab_re_search(expr, printsym) < 0 ? -1 : 0;
	}

	fprintf(stderr, "sym: unknown subcommand: %s\n", subcmd);
	return -1;
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

	return gdb_server(msp430_dev, port);
}

static int cmd_read(char **arg)
{
	char *filename = get_arg(arg);
	FILE *in;
	char buf[1024];

	if (!filename) {
		fprintf(stderr, "read: filename must be specified\n");
		return -1;
	}

	in = fopen(filename, "r");
	if (!in) {
		fprintf(stderr, "read: can't open %s: %s\n",
			filename, strerror(errno));
		return -1;
	}

	while (fgets(buf, sizeof(buf), in)) {
		char *cmd = buf;

		while (*cmd && isspace(*cmd))
			cmd++;

		if (*cmd == '#')
			continue;

		if (process_command(cmd, 0) < 0) {
			fprintf(stderr, "read: error processing %s\n",
				filename);
			fclose(in);
			return -1;
		}
	}

	fclose(in);
	return 0;
}

const struct command all_commands[] = {
	{"=",		cmd_eval,
	 "= <expression>\n"
	 "    Evaluate an expression using the symbol table.\n"},
	{"dis",		cmd_dis,
	 "dis <address> [length]\n"
	 "    Disassemble a section of memory.\n"},
	{"erase",       cmd_erase,
	 "erase\n"
	 "    Erase the device under test.\n"},
	{"gdb",         cmd_gdb,
	 "gdb [port]\n"
	 "    Run a GDB remote stub on the given TCP/IP port.\n"},
	{"help",	cmd_help,
	 "help [command]\n"
	 "    Without arguments, displays a list of commands. With a command\n"
	 "    name as an argument, displays help for that command.\n"},
	{"hexout",	cmd_hexout,
	 "hexout <address> <length> <filename.hex>\n"
	 "    Save a region of memory into a HEX file.\n"},
	{"md",		cmd_md,
	 "md <address> [length]\n"
	 "    Read the specified number of bytes from memory at the given\n"
	 "    address, and display a hexdump.\n"},
	{"mw",          cmd_mw,
	 "mw <address> bytes ...\n"
	 "    Write a sequence of bytes to a memory address. Byte values are\n"
	 "    two-digit hexadecimal numbers.\n"},
	{"opt",         cmd_opt,
	 "opt [name] [value]\n"
	 "    Query or set option variables. With no arguments, displays all\n"
	 "    available options.\n"},
	{"prog",	cmd_prog,
	 "prog <filename>\n"
	 "    Erase the device and flash the data contained in a binary file.\n"
	 "    This command also loads symbols from the file, if available.\n"},
	{"read",        cmd_read,
	 "read <filename>\n"
	 "    Read commands from a file and evaluate them.\n"},
	{"regs",	cmd_regs,
	 "regs\n"
	 "    Read and display the current register contents.\n"},
	{"reset",	cmd_reset,
	 "reset\n"
	 "    Reset (and halt) the CPU.\n"},
	{"run",		cmd_run,
	 "run [breakpoint]\n"
	 "    Run the CPU until either a specified breakpoint occurs or the\n"
	 "    command is interrupted.\n"},
	{"set",		cmd_set,
	 "set <register> <value>\n"
	 "    Change the value of a CPU register.\n"},
	{"step",	cmd_step,
	 "step [count]\n"
	 "    Single-step the CPU, and display the register state.\n"},
	{"sym",         cmd_sym,
	 "sym clear\n"
	 "    Clear the symbol table.\n"
	 "sym set <name> <value>\n"
	 "    Set or overwrite the value of a symbol.\n"
	 "sym del <name>\n"
	 "    Delete a symbol from the symbol table.\n"
	 "sym load <filename>\n"
	 "    Load symbols from the given file.\n"
	 "sym add <filename>\n"
	 "    Load additional symbols from the given file.\n"
	 "sym savemap <filename>\n"
	 "    Save the current symbols to a BSD-style symbol file.\n"
	 "sym find <regex>\n"
	 "    Search for symbols by regular expression.\n"},
	{NULL, NULL, NULL}
};

static void usage(const char *progname)
{
	fprintf(stderr,
"Usage: %s -R [-v voltage] [command ...]\n"
"       %s -u <device> [-j] [-v voltage] [command ...]\n"
"       %s -B <device> [command ...]\n"
"       %s -s [command ...]\n"
"\n"
"    -R\n"
"        Open the first available RF2500 device on the USB bus.\n"
"    -u device\n"
"        Open the given tty device (MSP430 UIF compatible devices).\n"
"    -j\n"
"        Use JTAG, rather than spy-bi-wire (UIF devices only).\n"
"    -v voltage\n"
"        Set the supply voltage, in millivolts.\n"
"    -B device\n"
"        Debug the FET itself through the bootloader.\n"
"    -s\n"
"        Start in simulation mode.\n"
"\n"
"By default, the first RF2500 device on the USB bus is opened.\n"
"\n"
"If commands are given, they will be executed. Otherwise, an interactive\n"
"command reader is started.\n",
		progname, progname, progname, progname);
}

#define MODE_RF2500             0x01
#define MODE_UIF                0x02
#define MODE_UIF_BSL            0x04
#define MODE_SIM                0x08

int main(int argc, char **argv)
{
	const struct fet_transport *trans;
	const char *uif_device = NULL;
	const char *bsl_device = NULL;
	int opt;
	int flags = 0;
	int want_jtag = 0;
	int vcc_mv = 3000;
	int mode = 0;

	puts(
"MSPDebug version 0.6 - debugging tool for MSP430 MCUs\n"
"Copyright (C) 2009, 2010 Daniel Beer <daniel@tortek.co.nz>\n"
"This is free software; see the source for copying conditions.  There is NO\n"
"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n");

	/* Parse arguments */
	while ((opt = getopt(argc, argv, "u:jv:B:sR?")) >= 0)
		switch (opt) {
		case 'R':
			mode |= MODE_RF2500;
			break;

		case 'u':
			uif_device = optarg;
			mode |= MODE_UIF;
			break;

		case 'v':
			vcc_mv = atoi(optarg);
			break;

		case 'j':
			want_jtag = 1;
			break;

		case 'B':
			bsl_device = optarg;
			mode |= MODE_UIF_BSL;
			break;

		case 's':
			mode |= MODE_SIM;
			break;

		case '?':
			usage(argv[0]);
			return 0;

		default:
			fprintf(stderr, "Invalid argument: %c\n"
				"Try -? for help.\n", opt);
			return -1;
		}

	/* Check for incompatible arguments */
	if (mode & (mode - 1)) {
		fprintf(stderr, "Multiple incompatible options specified.\n"
			"Try -? for help.\n");
		return -1;
	}

	if (!mode) {
		fprintf(stderr, "You need to specify an operating mode.\n"
			"Try -? for help.\n");
		return -1;
	}

	parse_init();
	if (stab_init() < 0)
		return -1;

	/* Open a device */
	if (mode == MODE_SIM) {
		msp430_dev = sim_open();
	} else if (mode == MODE_UIF_BSL) {
		msp430_dev = bsl_open(bsl_device);
	} else if (mode == MODE_RF2500 || mode == MODE_UIF) {
		/* Open the appropriate transport */
		if (mode == MODE_UIF) {
			trans = uif_open(uif_device);
		} else {
			trans = rf2500_open();
			flags |= FET_PROTO_RF2500;
		}

		if (!trans)
			return -1;

		/* Then initialize the device */
		if (!want_jtag)
			flags |= FET_PROTO_SPYBIWIRE;

		msp430_dev = fet_open(trans, flags, vcc_mv);
	}

	if (!msp430_dev)
		return -1;

	/* Process commands */
	if (optind < argc) {
		while (optind < argc)
			process_command(argv[optind++], 0);
	} else {
		do {
			reader_loop();
		} while (syms_modify_check());
	}

	msp430_dev->close();
	stab_exit();

	return 0;
}
