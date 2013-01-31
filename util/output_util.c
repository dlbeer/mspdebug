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
#include <string.h>
#include <stdlib.h>

#include "dis.h"
#include "output_util.h"
#include "stab.h"
#include "util.h"
#include "demangle.h"

static int format_addr(msp430_amode_t amode, address_t addr)
{
	char name[MAX_SYMBOL_LENGTH];
	const char *prefix = "";

	switch (amode) {
	case MSP430_AMODE_REGISTER:
	case MSP430_AMODE_INDIRECT:
	case MSP430_AMODE_INDIRECT_INC:
		return 0;

	case MSP430_AMODE_IMMEDIATE:
		prefix = "#";
	case MSP430_AMODE_INDEXED:
		break;

	case MSP430_AMODE_ABSOLUTE:
		prefix = "&";
		break;

	case MSP430_AMODE_SYMBOLIC:
		break;
	}

	print_address(addr, name, sizeof(name), PRINT_ADDRESS_EXACT);
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
static int format_operand(msp430_amode_t amode, address_t addr,
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

		len += printc(",");
		while (len < 15)
			len += printc(" ");
		len += printc(" ");
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

void disassemble(address_t offset, const uint8_t *data, int length,
		 powerbuf_t power)
{
	int first_line = 1;
	unsigned long long ua_total = 0;
	int samples_total = 0;

	while (length) {
		struct msp430_instruction insn = {0};
		int retval;
		int count;
		int i;
		address_t oboff;
		char obname[MAX_SYMBOL_LENGTH];

		if (first_line ||
			(!stab_nearest(offset, obname, sizeof(obname), &oboff) &&
			 !oboff)) {
			char buffer[MAX_SYMBOL_LENGTH];

			print_address(offset, buffer, sizeof(buffer), 0);
			printc("\x1b[m%s\x1b[0m:\n", buffer);
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
			i = dis_format(&insn);

		if (power) {
			unsigned long long ua;
			int samples;

			while (i < 40) {
				printc(" ");
				i++;
			}

			samples = powerbuf_get_by_mab(power, offset, &ua);
			if (samples) {
				printc(" ;; %.01f uA",
					(double)ua / (double)samples);
				ua_total += ua;
				samples_total += samples;
			}
		}

		printc("\n");

		offset += count;
		length -= count;
		data += count;
	}

	if (power && samples_total)
		printc(";; Total over this block: "
			"%.01f uAs in %.01f ms (%.01f uA avg)\n",
		       (double)(ua_total * power->interval_us) / 1000000.0,
		       (double)(samples_total * power->interval_us) / 1000.0,
		       (double)ua_total / (double)samples_total);
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

int print_address(address_t addr, char *out, int max_len,
		  print_address_flags_t f)
{
	char name[MAX_SYMBOL_LENGTH];
	address_t offset;

	if (!stab_nearest(addr, name, sizeof(name), &offset)) {
		char demangled[MAX_SYMBOL_LENGTH];
		int len;

		if (offset) {
			if (f & PRINT_ADDRESS_EXACT) {
				snprintf(out, max_len, "0x%04x", addr);
				return 0;
			}

			len = snprintf(out, max_len, "%s+0x%x", name, offset);
		} else {
			len = snprintf(out, max_len, "%s", name);
		}

		if (demangle(name, demangled, sizeof(demangled)) > 0)
			snprintf(out + len, max_len - len, " (%s)", demangled);

		return 1;
	}

	snprintf(out, max_len, "0x%04x", addr);
	return 0;
}

/************************************************************************
 * Name lists
 */

static int namelist_cmp(const void *a, const void *b)
{
	return strcasecmp(*(const char **)a, *(const char **)b);
}

void namelist_print(struct vector *v)
{
	int i;
	int max_len = 0;
	int rows, cols;

	qsort(v->ptr, v->size, v->elemsize, namelist_cmp);

	for (i = 0; i < v->size; i++) {
		const char *text = VECTOR_AT(*v, i, const char *);
		int len = strlen(text);

		if (len > max_len)
			max_len = len;
	}

	max_len += 2;
	cols = 72 / max_len;
	rows = (v->size + cols - 1) / cols;

	for (i = 0; i < rows; i++) {
		int j;

		printc("    ");
		for (j = 0; j < cols; j++) {
			int k = j * rows + i;
			const char *text;

			if (k >= v->size)
				break;

			text = VECTOR_AT(*v, k, const char *);
			printc("%s", text);
			for (k = strlen(text); k < max_len; k++)
				printc(" ");
		}

		printc("\n");
	}
}
