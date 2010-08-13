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
#include "output_util.h"
#include "util.h"
#include "dis.h"

static int cmd_regs(cproc_t cp, char **arg)
{
	address_t regs[DEVICE_NUM_REGS];
	uint8_t code[16];
	int len = sizeof(code);

	if (device_default->getregs(device_default, regs) < 0)
		return -1;
	show_regs(regs);

	/* Try to disassemble the instruction at PC */
	if (len > 0x10000 - regs[0])
		len = 0x10000 - regs[0];
	if (device_default->readmem(device_default, regs[0], code, len) < 0)
		return 0;

	disassemble(regs[0], (uint8_t *)code, len);
	return 0;
}

static int cmd_md(cproc_t cp, char **arg)
{
	char *off_text = get_arg(arg);
	char *len_text = get_arg(arg);
	address_t offset = 0;
	address_t length = 0x40;

	if (!off_text) {
		fprintf(stderr, "md: offset must be specified\n");
		return -1;
	}

	if (expr_eval(stab_default, off_text, &offset) < 0) {
		fprintf(stderr, "md: can't parse offset: %s\n", off_text);
		return -1;
	}

	if (len_text) {
		if (expr_eval(stab_default, len_text, &length) < 0) {
			fprintf(stderr, "md: can't parse length: %s\n",
				len_text);
			return -1;
		}
	} else if (offset + length > 0x10000) {
		length = 0x10000 - offset;
	}

	while (length) {
		uint8_t buf[128];
		int blen = length > sizeof(buf) ? sizeof(buf) : length;

		if (device_default->readmem(device_default,
					    offset, buf, blen) < 0)
			return -1;
		hexdump(offset, buf, blen);

		offset += blen;
		length -= blen;
	}

	return 0;
}

static int cmd_mw(cproc_t cp, char **arg)
{
	char *off_text = get_arg(arg);
	char *byte_text;
	address_t offset = 0;
	address_t length = 0;
	uint8_t buf[1024];

	if (!off_text) {
		fprintf(stderr, "md: offset must be specified\n");
		return -1;
	}

	if (expr_eval(stab_default, off_text, &offset) < 0) {
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

	if (device_default->writemem(device_default, offset, buf, length) < 0)
		return -1;

	return 0;
}

static int cmd_reset(cproc_t cp, char **arg)
{
	return device_default->ctl(device_default, DEVICE_CTL_RESET);
}

static int cmd_erase(cproc_t cp, char **arg)
{
	if (device_default->ctl(device_default, DEVICE_CTL_HALT) < 0)
		return -1;

	printf("Erasing...\n");
	return device_default->ctl(device_default, DEVICE_CTL_ERASE);
}

static int cmd_step(cproc_t cp, char **arg)
{
	char *count_text = get_arg(arg);
	int count = 1;

	if (count_text)
		count = atoi(count_text);

	while (count > 0) {
		if (device_default->ctl(device_default, DEVICE_CTL_STEP) < 0)
			return -1;
		count--;
	}

	return cmd_regs(cp, NULL);
}

static int cmd_run(cproc_t cp, char **arg)
{
	device_status_t status;
	address_t regs[DEVICE_NUM_REGS];

	if (device_default->getregs(device_default, regs) < 0) {
		fprintf(stderr, "warning: device: can't fetch registers\n");
	} else {
		int i;

		for (i = 0; i < device_default->max_breakpoints; i++) {
			struct device_breakpoint *bp =
				&device_default->breakpoints[i];

			if ((bp->flags & DEVICE_BP_ENABLED) &&
			    bp->addr == regs[0])
				break;
		}

		if (i < device_default->max_breakpoints) {
			printf("Stepping over breakpoint #%d at 0x%04x\n",
			       i, regs[0]);
			device_default->ctl(device_default, DEVICE_CTL_STEP);
		}
	}

	if (device_default->ctl(device_default, DEVICE_CTL_RUN) < 0) {
		fprintf(stderr, "run: failed to start CPU\n");
		return -1;
	}

	printf("Running. Press Ctrl+C to interrupt...\n");

	do {
		status = device_default->poll(device_default);
	} while (status == DEVICE_STATUS_RUNNING);

	if (status == DEVICE_STATUS_INTR)
		printf("\n");

	if (status == DEVICE_STATUS_ERROR)
		return -1;

	if (device_default->ctl(device_default, DEVICE_CTL_HALT) < 0)
		return -1;

	return cmd_regs(cp, NULL);
}

static int cmd_set(cproc_t cp, char **arg)
{
	char *reg_text = get_arg(arg);
	char *val_text = get_arg(arg);
	int reg;
	address_t value = 0;
	address_t regs[DEVICE_NUM_REGS];

	if (!(reg_text && val_text)) {
		fprintf(stderr, "set: must specify a register and a value\n");
		return -1;
	}

	reg = dis_reg_from_name(reg_text);
	if (reg < 0) {
		fprintf(stderr, "set: unknown register: %s\n", reg_text);
		return -1;
	}

	if (expr_eval(stab_default, val_text, &value) < 0) {
		fprintf(stderr, "set: can't parse value: %s\n", val_text);
		return -1;
	}

	if (device_default->getregs(device_default, regs) < 0)
		return -1;
	regs[reg] = value;
	if (device_default->setregs(device_default, regs) < 0)
		return -1;

	show_regs(regs);
	return 0;
}

static int cmd_dis(cproc_t cp, char **arg)
{
	char *off_text = get_arg(arg);
	char *len_text = get_arg(arg);
	address_t offset = 0;
	address_t length = 0x40;
	uint8_t *buf;

	if (!off_text) {
		fprintf(stderr, "dis: offset must be specified\n");
		return -1;
	}

	if (expr_eval(stab_default, off_text, &offset) < 0) {
		fprintf(stderr, "dis: can't parse offset: %s\n", off_text);
		return -1;
	}

	if (len_text) {
		if (expr_eval(stab_default, len_text, &length) < 0) {
			fprintf(stderr, "dis: can't parse length: %s\n",
				len_text);
			return -1;
		}
	} else if (offset + length > 0x10000) {
		length = 0x10000 - offset;
	}

	buf = malloc(length);
	if (!buf) {
		perror("dis: couldn't allocate memory");
		return -1;
	}

	if (device_default->readmem(device_default,
				    offset, buf, length) < 0) {
		free(buf);
		return -1;
	}

	disassemble(offset, buf, length);
	free(buf);
	return 0;
}

struct hexout_data {
	FILE            *file;
	address_t       addr;
	uint8_t         buf[16];
	int             len;

	uint16_t        segoff;
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
	hexout->segoff = 0;

	return 0;
}

static int hexout_write(FILE *out, int len, uint16_t addr, int type,
			const uint8_t *payload)
{
	int i;
	int cksum = 0;

	if (fprintf(out, ":%02X%04X00", len, addr) < 0)
		goto fail;
	cksum += len;
	cksum += addr & 0xff;
	cksum += addr >> 8;

	for (i = 0; i < len; i++) {
		if (fprintf(out, "%02X", payload[i]) < 0)
			goto fail;
		cksum += payload[i];
	}

	if (fprintf(out, "%02X\n", ~(cksum - 1) & 0xff) < 0)
		goto fail;

	return 0;

fail:
	perror("hexout: can't write HEX data");
	return -1;
}

static int hexout_flush(struct hexout_data *hexout)
{
        address_t addr_low = hexout->addr & 0xffff;
	address_t segoff = hexout->addr >> 16;

	if (!hexout->len)
		return 0;

	if (segoff != hexout->segoff) {
		uint8_t offset_data[] = {segoff >> 8, segoff & 0xff};

		if (hexout_write(hexout->file, 2, 0, 4, offset_data) < 0)
			return -1;
		hexout->segoff = segoff;
	}

	if (hexout_write(hexout->file, hexout->len, addr_low,
			 0, hexout->buf) < 0)
		return -1;
	hexout->len = 0;
	return 0;
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
	char *off_text = get_arg(arg);
	char *len_text = get_arg(arg);
	char *filename = *arg;
	address_t off;
	address_t length;
	struct hexout_data hexout;

	if (!(off_text && len_text && *filename)) {
		fprintf(stderr, "hexout: need offset, length and filename\n");
		return -1;
	}

	if (expr_eval(stab_default, off_text, &off) < 0 ||
	    expr_eval(stab_default, len_text, &length) < 0)
		return -1;

	if (hexout_start(&hexout, filename) < 0)
		return -1;

	while (length) {
		uint8_t buf[128];
		int count = length;

		if (count > sizeof(buf))
			count = sizeof(buf);

		printf("Reading %d bytes from 0x%04x...\n", count, off);
		if (device_default->readmem(device_default,
					    off, buf, count) < 0) {
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
	uint8_t         buf[128];
	address_t       addr;
	int             len;
	int             have_erased;
};

static int prog_flush(struct prog_data *prog)
{
	while (prog->len) {
		int wlen = prog->len;

		/* Writing across this address seems to cause a hang */
		if (prog->addr < 0x999a && wlen + prog->addr > 0x999a)
			wlen = 0x999a - prog->addr;

		if (!prog->have_erased) {
			printf("Erasing...\n");
			if (device_default->ctl(device_default,
						DEVICE_CTL_ERASE) < 0)
				return -1;
			prog->have_erased = 1;
		}

		printf("Writing %3d bytes to %04x...\n", wlen, prog->addr);
		if (device_default->writemem(device_default, prog->addr,
					     prog->buf, wlen) < 0)
		        return -1;

		memmove(prog->buf, prog->buf + wlen, prog->len - wlen);
		prog->len -= wlen;
		prog->addr += wlen;
	}

        return 0;
}

static int prog_feed(void *user_data,
		     address_t addr, const uint8_t *data, int len)
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
	FILE *in;
	struct prog_data prog;

	if (cproc_prompt_abort(cp, CPROC_MODIFY_SYMS))
		return 0;

	in = fopen(*arg, "r");
	if (!in) {
		fprintf(stderr, "prog: %s: %s\n", *arg, strerror(errno));
		return -1;
	}

	if (device_default->ctl(device_default, DEVICE_CTL_HALT) < 0) {
		fclose(in);
		return -1;
	}

	memset(&prog, 0, sizeof(prog));

	if (binfile_extract(in, prog_feed, &prog) < 0) {
		fclose(in);
		return -1;
	}

	if (binfile_info(in) & BINFILE_HAS_SYMS) {
		stab_clear(stab_default);
		binfile_syms(in, stab_default);
	}

	fclose(in);

	if (prog_flush(&prog) < 0)
		return -1;

	if (device_default->ctl(device_default, DEVICE_CTL_RESET) < 0) {
		fprintf(stderr, "prog: failed to reset after programming\n");
		return -1;
	}

	cproc_unmodify(cp, CPROC_MODIFY_SYMS);
	return 0;
}

static int cmd_setbreak(cproc_t cp, char **arg)
{
	char *addr_text = get_arg(arg);
	char *index_text = get_arg(arg);
	int index = -1;
	address_t addr;

	if (!addr_text) {
		fprintf(stderr, "setbreak: address required\n");
		return -1;
	}

	if (expr_eval(stab_default, addr_text, &addr) < 0) {
		fprintf(stderr, "setbreak: invalid address\n");
		return -1;
	}

	if (index_text) {
		index = atoi(index_text);

		if (index < 0 || index >= device_default->max_breakpoints) {
			fprintf(stderr, "setbreak: invalid breakpoint "
				"slot: %d\n", index);
			return -1;
		}
	}

	index = device_setbrk(device_default, index, 1, addr);
	if (index < 0) {
		fprintf(stderr, "setbreak: all breakpoint slots are "
			"occupied\n");
		return -1;
	}

	printf("Set breakpoint %d\n", index);
	return 0;
}

static int cmd_delbreak(cproc_t cp, char **arg)
{
	char *index_text = get_arg(arg);
	int ret = 0;

	if (index_text) {
		int index = atoi(index_text);

		if (index < 0 || index >= device_default->max_breakpoints) {
			fprintf(stderr, "delbreak: invalid breakpoint "
				"slot: %d\n", index);
			return -1;
		}

		printf("Clearing breakpoint %d\n", index);
		device_setbrk(device_default, index, 0, 0);
	} else {
		int i;

		printf("Clearing all breakpoints...\n");
		for (i = 0; i < device_default->max_breakpoints; i++)
			device_setbrk(device_default, i, 0, 0);
	}

	return ret;
}

static int cmd_break(cproc_t cp, char **arg)
{
	int i;

	printf("%d breakpoints available:\n", device_default->max_breakpoints);
	for (i = 0; i < device_default->max_breakpoints; i++) {
		const struct device_breakpoint *bp =
			&device_default->breakpoints[i];

		if (bp->flags & DEVICE_BP_ENABLED) {
			char name[128];
			address_t offset;

			printf("    %d. 0x%05x", i, bp->addr);
			if (!stab_nearest(stab_default, bp->addr, name,
					  sizeof(name), &offset)) {
				printf(" (%s", name);
				if (offset)
					printf("+0x%x", offset);
				printf(")");
			}
			printf("\n");
		}
	}

	return 0;
}

static const struct cproc_command commands[] = {
	{
		.name = "setbreak",
		.func = cmd_setbreak,
		.help =
"setbreak <addr> [index]\n"
"    Set a breakpoint. If no index is specified, the first available\n"
"    slot will be used.\n"
	},
	{
		.name = "delbreak",
		.func = cmd_delbreak,
		.help =
"delbreak [index]\n"
"    Delete a breakpoint. If no index is specified, then all active\n"
"    breakpoints are cleared.\n"
	},
	{
		.name = "break",
		.func = cmd_break,
		.help =
"break\n"
"    List active breakpoints.\n"
	},
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
"run\n"
"    Run the CPU to until a breakpoint is reached or the command is\n"
"    interrupted.\n"
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
