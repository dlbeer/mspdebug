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
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include "device.h"
#include "binfile.h"
#include "stab.h"
#include "parse.h"
#include "util.h"
#include "dis.h"

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

static int cmd_regs(char **arg)
{
	u_int16_t regs[DEVICE_NUM_REGS];
	u_int8_t code[16];

	if (device_get()->getregs(regs) < 0)
		return -1;
	show_regs(regs);

	/* Try to disassemble the instruction at PC */
	if (device_get()->readmem(regs[0], code, sizeof(code)) < 0)
		return 0;

	disassemble(regs[0], (u_int8_t *)code, sizeof(code));
	return 0;
}

static struct command command_regs = {
	.name = "regs",
	.func = cmd_regs,
	.help =
	"regs\n"
	"    Read and display the current register contents.\n"
};

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

		if (device_get()->readmem(offset, buf, blen) < 0)
			return -1;
		hexdump(offset, buf, blen);

		offset += blen;
		length -= blen;
	}

	return 0;
}

static struct command command_md = {
	.name = "md",
	.func = cmd_md,
	.help =
	"md <address> [length]\n"
	"    Read the specified number of bytes from memory at the given\n"
	"    address, and display a hexdump.\n"
};

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

	if (device_get()->writemem(offset, buf, length) < 0)
		return -1;

	return 0;
}

static struct command command_mw = {
	.name = "mw",
	.func = cmd_mw,
	.help =
	"mw <address> bytes ...\n"
	"    Write a sequence of bytes to a memory address. Byte values are\n"
	"    two-digit hexadecimal numbers.\n"
};

static int cmd_reset(char **arg)
{
	return device_get()->control(DEVICE_CTL_RESET);
}

static struct command command_reset = {
	.name = "reset",
	.func = cmd_reset,
	.help =
	 "reset\n"
	 "    Reset (and halt) the CPU.\n"
};

static int cmd_erase(char **arg)
{
	if (device_get()->control(DEVICE_CTL_HALT) < 0)
		return -1;

	printf("Erasing...\n");
	return device_get()->control(DEVICE_CTL_ERASE);
}

static struct command command_erase = {
	.name = "erase",
	.func = cmd_erase,
	.help =
	"erase\n"
	"    Erase the device under test.\n"
};

static int cmd_step(char **arg)
{
	char *count_text = get_arg(arg);
	int count = 1;

	if (count_text)
		count = atoi(count_text);

	while (count > 0) {
		if (device_get()->control(DEVICE_CTL_STEP) < 0)
			return -1;
		count--;
	}

	return cmd_regs(NULL);
}

static struct command command_step = {
	.name = "step",
	.func = cmd_step,
	.help =
	"step [count]\n"
	"    Single-step the CPU, and display the register state.\n"
};

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

		device_get()->breakpoint(bp_addr);
	}

	if (device_get()->control(bp_text ?
		DEVICE_CTL_RUN_BP : DEVICE_CTL_RUN) < 0)
		return -1;

	if (bp_text)
		printf("Running to 0x%04x.", bp_addr);
	else
		printf("Running.");
	printf(" Press Ctrl+C to interrupt...\n");

	status = device_get()->wait(1);
	if (status == DEVICE_STATUS_INTR)
		printf("\n");

	if (device_get()->control(DEVICE_CTL_HALT) < 0)
		return -1;

	return cmd_regs(NULL);
}

static struct command command_run = {
	.name = "run",
	.func = cmd_run,
	.help =
	"run [breakpoint]\n"
	"    Run the CPU until either a specified breakpoint occurs or the\n"
	"    command is interrupted.\n"
};

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

	if (device_get()->getregs(regs) < 0)
		return -1;
	regs[reg] = value;
	if (device_get()->setregs(regs) < 0)
		return -1;

	show_regs(regs);
	return 0;
}

static struct command command_set = {
	.name = "set",
	.func = cmd_set,
	.help =
	"set <register> <value>\n"
	"    Change the value of a CPU register.\n"
};

static int cmd_dis(char **arg)
{
	char *off_text = get_arg(arg);
	char *len_text = get_arg(arg);
	int offset = 0;
	int length = 0x40;
	u_int8_t buf[4096];

	if (!off_text) {
		fprintf(stderr, "dis: offset must be specified\n");
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

	if (device_get()->readmem(offset, buf, length) < 0)
		return -1;

	disassemble(offset, (u_int8_t *)buf, length);
	return 0;
}

static struct command command_dis = {
	.name = "dis",
	.func = cmd_dis,
	.help =
	"dis <address> [length]\n"
	"    Disassemble a section of memory.\n"
};

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
		if (device_get()->readmem(off, buf, count) < 0) {
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

static struct command command_hexout = {
	.name = "hexout",
	.func = cmd_hexout,
	.help =
	"hexout <address> <length> <filename.hex>\n"
	"    Save a region of memory into a HEX file.\n"
};

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
			if (device_get()->control(DEVICE_CTL_ERASE) < 0)
				return -1;
			prog_have_erased = 1;
		}

		printf("Writing %3d bytes to %04x...\n", wlen, prog_addr);
		if (device_get()->writemem(prog_addr, prog_buf, wlen) < 0)
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
	FILE *in;
	int result = 0;

	if (modify_prompt(MODIFY_SYMS))
		return 0;

	in = fopen(*arg, "r");
	if (!in) {
		fprintf(stderr, "prog: %s: %s\n", *arg, strerror(errno));
		return -1;
	}

	if (device_get()->control(DEVICE_CTL_HALT) < 0) {
		fclose(in);
		return -1;
	}

	prog_init();

	if (elf32_check(in)) {
		result = elf32_extract(in, prog_feed);
		stab_clear();
		elf32_syms(in, stab_set);
	} else if (ihex_check(in)) {
		result = ihex_extract(in, prog_feed);
	} else {
		fprintf(stderr, "prog: %s: unknown file type\n", *arg);
	}

	fclose(in);

	if (prog_flush() < 0)
		return -1;

	if (device_get()->control(DEVICE_CTL_RESET) < 0) {
		fprintf(stderr, "prog: failed to reset after programming\n");
		return -1;
	}

	modify_clear(MODIFY_SYMS);
	return result;
}

static struct command command_prog = {
	.name = "prog",
	.func = cmd_prog,
	.help =
	"prog <filename>\n"
	"    Erase the device and flash the data contained in a binary file.\n"
	"    This command also loads symbols from the file, if available.\n"
};

void devcmd_init(void)
{
	register_command(&command_md);
	register_command(&command_mw);
	register_command(&command_reset);
	register_command(&command_erase);
	register_command(&command_regs);
	register_command(&command_run);
	register_command(&command_step);
	register_command(&command_set);
	register_command(&command_dis);
	register_command(&command_hexout);
	register_command(&command_prog);
}
