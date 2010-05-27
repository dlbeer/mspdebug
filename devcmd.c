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
#include "expr.h"
#include "cproc.h"
#include "cproc_util.h"
#include "util.h"
#include "dis.h"

static int cmd_regs(cproc_t cp, char **arg)
{
	device_t dev = cproc_device(cp);
	uint16_t regs[DEVICE_NUM_REGS];
	uint8_t code[16];
	int len = sizeof(code);

	if (dev->getregs(dev, regs) < 0)
		return -1;
	cproc_regs(cp, regs);

	/* Try to disassemble the instruction at PC */
	if (len > 0x10000 - regs[0])
		len = 0x10000 - regs[0];
	if (dev->readmem(dev, regs[0], code, len) < 0)
		return 0;

	cproc_disassemble(cp, regs[0], (uint8_t *)code, len);
	return 0;
}

static int cmd_md(cproc_t cp, char **arg)
{
	stab_t stab = cproc_stab(cp);
	device_t dev = cproc_device(cp);
	char *off_text = get_arg(arg);
	char *len_text = get_arg(arg);
	int offset = 0;
	int length = 0x40;

	if (!off_text) {
		fprintf(stderr, "md: offset must be specified\n");
		return -1;
	}

	if (expr_eval(stab, off_text, &offset) < 0) {
		fprintf(stderr, "md: can't parse offset: %s\n", off_text);
		return -1;
	}

	if (len_text) {
		if (expr_eval(stab, len_text, &length) < 0) {
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
		uint8_t buf[128];
		int blen = length > sizeof(buf) ? sizeof(buf) : length;

		if (dev->readmem(dev, offset, buf, blen) < 0)
			return -1;
		cproc_hexdump(cp, offset, buf, blen);

		offset += blen;
		length -= blen;
	}

	return 0;
}

static int cmd_mw(cproc_t cp, char **arg)
{
	device_t dev = cproc_device(cp);
	stab_t stab = cproc_stab(cp);
	char *off_text = get_arg(arg);
	char *byte_text;
	int offset = 0;
	int length = 0;
	uint8_t buf[1024];

	if (!off_text) {
		fprintf(stderr, "md: offset must be specified\n");
		return -1;
	}

	if (expr_eval(stab, off_text, &offset) < 0) {
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

	if (dev->writemem(dev, offset, buf, length) < 0)
		return -1;

	return 0;
}

static int cmd_reset(cproc_t cp, char **arg)
{
	device_t dev = cproc_device(cp);

	return dev->ctl(dev, DEVICE_CTL_RESET);
}

static int cmd_erase(cproc_t cp, char **arg)
{
	device_t dev = cproc_device(cp);

	if (dev->ctl(dev, DEVICE_CTL_HALT) < 0)
		return -1;

	printf("Erasing...\n");
	return dev->ctl(dev, DEVICE_CTL_ERASE);
}

static int cmd_step(cproc_t cp, char **arg)
{
	device_t dev = cproc_device(cp);
	char *count_text = get_arg(arg);
	int count = 1;

	if (count_text)
		count = atoi(count_text);

	while (count > 0) {
		if (dev->ctl(dev, DEVICE_CTL_STEP) < 0)
			return -1;
		count--;
	}

	return cmd_regs(cp, NULL);
}

static int cmd_run(cproc_t cp, char **arg)
{
	device_t dev = cproc_device(cp);
	stab_t stab = cproc_stab(cp);
	char *bp_text = get_arg(arg);
	int bp_addr;
	device_status_t status;

	if (bp_text) {
		if (expr_eval(stab, bp_text, &bp_addr) < 0) {
			fprintf(stderr, "run: can't parse breakpoint: %s\n",
				bp_text);
			return -1;
		}

		dev->breakpoint(dev, 1, bp_addr);
	} else {
		dev->breakpoint(dev, 0, 0);
	}

	if (dev->ctl(dev, DEVICE_CTL_RUN) < 0)
		return -1;

	if (bp_text)
		printf("Running to 0x%04x.", bp_addr);
	else
		printf("Running.");
	printf(" Press Ctrl+C to interrupt...\n");

	do {
		status = dev->poll(dev);
	} while (status == DEVICE_STATUS_RUNNING);

	if (status == DEVICE_STATUS_INTR)
		printf("\n");

	if (status == DEVICE_STATUS_ERROR)
		return -1;

	if (dev->ctl(dev, DEVICE_CTL_HALT) < 0)
		return -1;

	return cmd_regs(cp, NULL);
}

static int cmd_set(cproc_t cp, char **arg)
{
	device_t dev = cproc_device(cp);
	stab_t stab = cproc_stab(cp);
	char *reg_text = get_arg(arg);
	char *val_text = get_arg(arg);
	int reg;
	int value = 0;
	uint16_t regs[DEVICE_NUM_REGS];

	if (!(reg_text && val_text)) {
		fprintf(stderr, "set: must specify a register and a value\n");
		return -1;
	}

	reg = dis_reg_from_name(reg_text);
	if (reg < 0) {
		fprintf(stderr, "set: unknown register: %s\n", reg_text);
		return -1;
	}

	if (expr_eval(stab, val_text, &value) < 0) {
		fprintf(stderr, "set: can't parse value: %s\n", val_text);
		return -1;
	}

	if (dev->getregs(dev, regs) < 0)
		return -1;
	regs[reg] = value;
	if (dev->setregs(dev, regs) < 0)
		return -1;

	cproc_regs(cp, regs);
	return 0;
}

static int cmd_dis(cproc_t cp, char **arg)
{
	device_t dev = cproc_device(cp);
	stab_t stab = cproc_stab(cp);
	char *off_text = get_arg(arg);
	char *len_text = get_arg(arg);
	int offset = 0;
	int length = 0x40;
	uint8_t buf[4096];

	if (!off_text) {
		fprintf(stderr, "dis: offset must be specified\n");
		return -1;
	}

	if (expr_eval(stab, off_text, &offset) < 0) {
		fprintf(stderr, "dis: can't parse offset: %s\n", off_text);
		return -1;
	}

	if (len_text) {
		if (expr_eval(stab, len_text, &length) < 0) {
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

	if (dev->readmem(dev, offset, buf, length) < 0)
		return -1;

	cproc_disassemble(cp, offset, (uint8_t *)buf, length);
	return 0;
}

struct hexout_data {
	FILE            *file;
	uint16_t        addr;
	uint8_t         buf[16];
	int             len;
};

static int hexout_start(struct hexout_data *hexout, const char *filename)
{
	hexout->file = fopen(filename, "w");
	if (!hexout->file) {
		perror("hexout: couldn't open output file");
		return -1;
	}

	hexout->addr = 0;
	hexout->len = 0;

	return 0;
}

static int hexout_flush(struct hexout_data *hexout)
{
	int i;
	int cksum = 0;

	if (!hexout->len)
		return 0;

	if (fprintf(hexout->file, ":%02X%04X00",
		    hexout->len, hexout->addr) < 0)
		goto fail;
	cksum += hexout->len;
	cksum += hexout->addr & 0xff;
	cksum += hexout->addr >> 8;

	for (i = 0; i < hexout->len; i++) {
		if (fprintf(hexout->file, "%02X", hexout->buf[i]) < 0)
			goto fail;
		cksum += hexout->buf[i];
	}

	if (fprintf(hexout->file, "%02X\n", ~(cksum - 1) & 0xff) < 0)
		goto fail;

	hexout->len = 0;
	return 0;

fail:
	perror("hexout: can't write HEX data");
	return -1;
}

static int hexout_feed(struct hexout_data *hexout,
		       uint16_t addr, const uint8_t *buf, int len)
{
	while (len) {
		int count;

		if ((hexout->addr + hexout->len != addr ||
		     hexout->len >= sizeof(hexout->buf)) &&
		    hexout_flush(hexout) < 0)
			return -1;

		if (!hexout->len)
			hexout->addr = addr;

		count = sizeof(hexout->buf) - hexout->len;
		if (count > len)
			count = len;

		memcpy(hexout->buf + hexout->len, buf, count);
		hexout->len += count;

		addr += count;
		buf += count;
		len -= count;
	}

	return 0;
}

static int cmd_hexout(cproc_t cp, char **arg)
{
	device_t dev = cproc_device(cp);
	stab_t stab = cproc_stab(cp);
	char *off_text = get_arg(arg);
	char *len_text = get_arg(arg);
	char *filename = *arg;
	int off;
	int length;
	struct hexout_data hexout;

	if (!(off_text && len_text && *filename)) {
		fprintf(stderr, "hexout: need offset, length and filename\n");
		return -1;
	}

	if (expr_eval(stab, off_text, &off) < 0 ||
	    expr_eval(stab, len_text, &length) < 0)
		return -1;

	if (hexout_start(&hexout, filename) < 0)
		return -1;

	while (length) {
		uint8_t buf[128];
		int count = length;

		if (count > sizeof(buf))
			count = sizeof(buf);

		printf("Reading %d bytes from 0x%04x...\n", count, off);
		if (dev->readmem(dev, off, buf, count) < 0) {
			perror("hexout: can't read memory");
			goto fail;
		}

		if (hexout_feed(&hexout, off, buf, count) < 0)
			goto fail;

		length -= count;
		off += count;
	}

	if (hexout_flush(&hexout) < 0)
		goto fail;
	if (fclose(hexout.file) < 0) {
		perror("hexout: error on close");
		return -1;
	}

	return 0;

fail:
	fclose(hexout.file);
	unlink(filename);
	return -1;
}

struct prog_data {
	device_t        dev;

	uint8_t         buf[128];
	uint16_t        addr;
	int             len;
	int             have_erased;
};

static void prog_init(struct prog_data *prog, device_t dev)
{
	prog->dev = dev;
	prog->len = 0;
	prog->have_erased = 0;
}

static int prog_flush(struct prog_data *prog)
{
	while (prog->len) {
		int wlen = prog->len;

		/* Writing across this address seems to cause a hang */
		if (prog->addr < 0x999a && wlen + prog->addr > 0x999a)
			wlen = 0x999a - prog->addr;

		if (!prog->have_erased) {
			printf("Erasing...\n");
			if (prog->dev->ctl(prog->dev, DEVICE_CTL_ERASE) < 0)
				return -1;
			prog->have_erased = 1;
		}

		printf("Writing %3d bytes to %04x...\n", wlen, prog->addr);
		if (prog->dev->writemem(prog->dev, prog->addr,
					prog->buf, wlen) < 0)
		        return -1;

		memmove(prog->buf, prog->buf + wlen, prog->len - wlen);
		prog->len -= wlen;
		prog->addr += wlen;
	}

        return 0;
}

static int prog_feed(void *user_data,
		     uint16_t addr, const uint8_t *data, int len)
{
	struct prog_data *prog = (struct prog_data *)user_data;

	/* Flush if this section is discontiguous */
	if (prog->len && prog->addr + prog->len != addr &&
	    prog_flush(prog) < 0)
		return -1;

	if (!prog->len)
		prog->addr = addr;

	/* Add the buffer in piece by piece, flushing when it gets
	 * full.
	 */
	while (len) {
		int count = sizeof(prog->buf) - prog->len;

		if (count > len)
			count = len;

		if (!count) {
			if (prog_flush(prog) < 0)
				return -1;
		} else {
			memcpy(prog->buf + prog->len, data, count);
			prog->len += count;
			data += count;
			len -= count;
		}
	}

	return 0;
}

static int cmd_prog(cproc_t cp, char **arg)
{
	device_t dev = cproc_device(cp);
	stab_t stab = cproc_stab(cp);
	FILE *in;
	struct prog_data prog;

	if (cproc_prompt_abort(cp, CPROC_MODIFY_SYMS))
		return 0;

	in = fopen(*arg, "r");
	if (!in) {
		fprintf(stderr, "prog: %s: %s\n", *arg, strerror(errno));
		return -1;
	}

	if (dev->ctl(dev, DEVICE_CTL_HALT) < 0) {
		fclose(in);
		return -1;
	}

	prog_init(&prog, dev);

	if (binfile_extract(in, prog_feed, &prog) < 0) {
		fclose(in);
		return -1;
	}

	if (binfile_info(in) & BINFILE_HAS_SYMS) {
		stab_clear(stab);
		binfile_syms(in, stab);
	}

	fclose(in);

	if (prog_flush(&prog) < 0)
		return -1;

	if (dev->ctl(dev, DEVICE_CTL_RESET) < 0) {
		fprintf(stderr, "prog: failed to reset after programming\n");
		return -1;
	}

	cproc_unmodify(cp, CPROC_MODIFY_SYMS);
	return 0;
}

static const struct cproc_command commands[] = {
	{
		.name = "regs",
		.func = cmd_regs,
		.help =
"regs\n"
"    Read and display the current register contents.\n"
	},
	{
		.name = "prog",
		.func = cmd_prog,
		.help =
"prog <filename>\n"
"    Erase the device and flash the data contained in a binary file.\n"
"    This command also loads symbols from the file, if available.\n"
	},
	{
		.name = "md",
		.func = cmd_md,
		.help =
"md <address> [length]\n"
"    Read the specified number of bytes from memory at the given\n"
"    address, and display a hexdump.\n"
	},
	{
		.name = "mw",
		.func = cmd_mw,
		.help =
"mw <address> bytes ...\n"
"    Write a sequence of bytes to a memory address. Byte values are\n"
"    two-digit hexadecimal numbers.\n"
	},
	{
		.name = "reset",
		.func = cmd_reset,
		.help =
 "reset\n"
 "    Reset (and halt) the CPU.\n"
	},
	{
		.name = "erase",
		.func = cmd_erase,
		.help =
"erase\n"
"    Erase the device under test.\n"
	},
	{
		.name = "step",
		.func = cmd_step,
		.help =
"step [count]\n"
"    Single-step the CPU, and display the register state.\n"
	},
	{
		.name = "run",
		.func = cmd_run,
		.help =
"run [breakpoint]\n"
"    Run the CPU until either a specified breakpoint occurs or the\n"
"    command is interrupted.\n"
	},
	{
		.name = "set",
		.func = cmd_set,
		.help =
"set <register> <value>\n"
"    Change the value of a CPU register.\n"
	},
	{
		.name = "dis",
		.func = cmd_dis,
		.help =
"dis <address> [length]\n"
"    Disassemble a section of memory.\n"
	},
	{
		.name = "hexout",
		.func = cmd_hexout,
		.help =
"hexout <address> <length> <filename.hex>\n"
"    Save a region of memory into a HEX file.\n"
	}
};

int devcmd_register(cproc_t cp)
{
	return cproc_register_commands(cp, commands, ARRAY_LEN(commands));
}
