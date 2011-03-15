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
#include <assert.h>

#include "dis.h"
#include "output_util.h"
#include "stab.h"
#include "util.h"

static int format_addr(msp430_amode_t amode, uint16_t addr)
{
	char name[64];
	int numeric = 0;
	const char *prefix = "";

	switch (amode) {
	case MSP430_AMODE_REGISTER:
	case MSP430_AMODE_INDIRECT:
	case MSP430_AMODE_INDIRECT_INC:
		return 0;

	case MSP430_AMODE_IMMEDIATE:
		prefix = "#";
	case MSP430_AMODE_INDEXED:
		numeric = 1;
		break;

	case MSP430_AMODE_ABSOLUTE:
		prefix = "&";
		break;

	case MSP430_AMODE_SYMBOLIC:
		break;
	}

	print_address(addr, name, sizeof(name));
	return printc("%s\x1b[1m%s\x1b[0m", prefix, name);
}

static int format_reg(msp430_amode_t amode, msp430_reg_t reg)
{
	const char *prefix = "";
	const char *suffix = "";
	const char *name;

	switch (amode) {
	case MSP430_AMODE_REGISTER:
		break;

	case MSP430_AMODE_INDEXED:
		prefix = "(";
		suffix = ")";
		break;

	case MSP430_AMODE_IMMEDIATE:
	case MSP430_AMODE_SYMBOLIC:
	case MSP430_AMODE_ABSOLUTE:
		return 0;

	case MSP430_AMODE_INDIRECT_INC:
		suffix = "+";
	case MSP430_AMODE_INDIRECT:
		prefix = "@";
		break;
	}

	name = dis_reg_name(reg);
	if (!name)
		name = "???";

	return printc("%s\x1b[33m%s\x1b[0m%s", prefix, name, suffix);
}

/* Given an operands addressing mode, value and associated register,
 * print the canonical representation of it to stdout.
 *
 * Returns the number of characters printed.
 */
static int format_operand(msp430_amode_t amode, uint16_t addr,
			  msp430_reg_t reg)
{
	int len = 0;

	len += format_addr(amode, addr);
	len += format_reg(amode, reg);

	return len;
}

/* Write assembly language for the instruction to this buffer */
static int dis_format(const struct msp430_instruction *insn)
{
	int len = 0;
	const char *opname = dis_opcode_name(insn->op);
	const char *suffix = "";

	if (!opname)
		opname = "???";

	if (insn->dsize == MSP430_DSIZE_BYTE)
		suffix = ".B";
	else if (insn->dsize == MSP430_DSIZE_AWORD)
		suffix = ".A";
	else if (insn->dsize == MSP430_DSIZE_UNKNOWN)
		suffix = ".?";

	/* Don't show the .A suffix for these instructions */
	if (insn->op == MSP430_OP_MOVA || insn->op == MSP430_OP_CMPA ||
	    insn->op == MSP430_OP_SUBA || insn->op == MSP430_OP_ADDA ||
	    insn->op == MSP430_OP_BRA || insn->op == MSP430_OP_RETA)
		suffix = "";

	len += printc("\x1b[36m%s%s\x1b[0m", opname, suffix);
	while (len < 8)
		len += printc(" ");

	/* Source operand */
	if (insn->itype == MSP430_ITYPE_DOUBLE) {
		len += format_operand(insn->src_mode,
				      insn->src_addr,
				      insn->src_reg);

		printc(",");
		while (len < 15)
			len += printc(" ");
		printc(" ");
	}

	/* Destination operand */
	if (insn->itype != MSP430_ITYPE_NOARG)
		len += format_operand(insn->dst_mode,
					insn->dst_addr,
					insn->dst_reg);

	/* Repetition count */
	if (insn->rep_register)
		len += printc(" [repeat %s]", dis_reg_name(insn->rep_index));
	else if (insn->rep_index)
		len += printc(" [repeat %d]", insn->rep_index + 1);

	return len;
}

void disassemble(address_t offset, const uint8_t *data, int length)
{
	int first_line = 1;

	while (length) {
		struct msp430_instruction insn = {0};
		int retval;
		int count;
		int i;
		address_t oboff;
		char obname[64];

		if (!stab_nearest(offset, obname, sizeof(obname), &oboff) &&
		    !oboff) {
			printc("\x1b[m%s\x1b[0m:\n", obname);
		} else if (first_line) {
			print_address(offset, obname, sizeof(obname));
			printc("\x1b[m%s\x1b[0m:\n", obname);
		}
		first_line = 0;

		retval = dis_decode(data, offset, length, &insn);
		count = retval > 0 ? retval : 2;
		if (count > length)
			count = length;

		printc("    \x1b[36m%05x\x1b[0m:", offset);

		for (i = 0; i < count; i++)
			printc(" %02x", data[i]);

		while (i < 9) {
			printc("   ");
			i++;
		}

		if (retval >= 0)
			dis_format(&insn);
		printc("\n");

		offset += count;
		length -= count;
		data += count;
	}
}

void hexdump(address_t addr, const uint8_t *data, int data_len)
{
	int offset = 0;

	while (offset < data_len) {
		int i, j;

		/* Address label */
		printc("    \x1b[36m%05x:\x1b[0m", offset + addr);

		/* Hex portion */
		for (i = 0; i < 16 && offset + i < data_len; i++)
			printc(" %02x", data[offset + i]);
		for (j = i; j < 16; j++)
			printc("   ");

		/* Printable characters */
		printc(" \x1b[32m|");
		for (j = 0; j < i; j++) {
			int c = data[offset + j];

			printc("%c", (c >= 32 && c <= 126) ? c : '.');
		}
		for (; j < 16; j++)
			printc(" ");
		printc("|\x1b[0m\n");

		offset += i;
	}
}

void show_regs(const address_t *regs)
{
	int i;

	for (i = 0; i < 4; i++) {
		int j;

		printc("    ");
		for (j = 0; j < 4; j++) {
			int k = j * 4 + i;

			printc("(\x1b[1m%3s:\x1b[0m %05x)  ",
			       dis_reg_name(k), regs[k]);
		}

		printc("\n");
	}
}

int print_address(address_t addr, char *out, int max_len)
{
	char name[128];
        address_t offset;

        if (!stab_nearest(addr, name, sizeof(name), &offset)) {
		if (offset)
			snprintf(out, max_len, "%s+0x%x", name, offset);
		else
			snprintf(out, max_len, "%s", name);

		return 1;
        }

	snprintf(out, max_len, "0x%04x", addr);
	return 0;
}
