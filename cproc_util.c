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
#include "cproc_util.h"
#include "stab.h"
#include "util.h"

static int format_addr(stab_t stab, char *buf, int max_len,
		       msp430_amode_t amode, uint16_t addr)
{
	char name[64];
	address_t offset;
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

	if ((!numeric ||
	     (addr >= 0x200 && addr < 0xfff0)) &&
	    !stab_nearest(stab, addr, name, sizeof(name), &offset) &&
	    !offset)
		return snprintf(buf, max_len,
				"%s\x1b[1m%s\x1b[0m", prefix, name);
	else if (numeric)
		return snprintf(buf, max_len,
				"%s\x1b[1m0x%x\x1b[0m", prefix, addr);
	else
		return snprintf(buf, max_len,
				"%s\x1b[1m0x%04x\x1b[0m", prefix, addr);
}

static int format_reg(char *buf, int max_len,
		      msp430_amode_t amode, msp430_reg_t reg)
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

	return snprintf(buf, max_len,
			"%s\x1b[33m%s\x1b[0m%s",
			prefix, name, suffix);
}

/* Given an operands addressing mode, value and associated register,
 * print the canonical representation of it to stdout.
 *
 * Returns the number of characters printed.
 */
static int format_operand(stab_t stab, char *buf, int max_len,
			  msp430_amode_t amode, uint16_t addr,
			  msp430_reg_t reg)
{
	int len = 0;

	len += format_addr(stab, buf, max_len, amode, addr);
	len += format_reg(buf + len, max_len - len, amode, reg);
	return len;
}

/* Write assembly language for the instruction to this buffer */
static int dis_format(stab_t stab, char *buf, int max_len,
		      const struct msp430_instruction *insn)
{
	int len;
	int tlen;
	int total = 0;
	const char *opname = dis_opcode_name(insn->op);

	if (!opname)
		opname = "???";

	len = snprintf(buf + total, max_len - total,
		       "\x1b[36m%s%s\x1b[0m", opname,
		       insn->is_byte_op ? ".B" : "");
	tlen = textlen(buf + total);
	total += len;

	while (tlen < 8 && total < max_len) {
		buf[total++] = ' ';
		tlen++;
	}

	/* Source operand */
	if (insn->itype == MSP430_ITYPE_DOUBLE) {
		len = format_operand(stab, buf + total,
				     max_len - total,
				     insn->src_mode,
				     insn->src_addr,
				     insn->src_reg);
		tlen = textlen(buf + total);
		total += len;

		if (total < max_len)
			buf[total++] = ',';

		while (tlen < 15 && total < max_len) {
			tlen++;
			buf[total++] = ' ';
		}

		if (total < max_len)
			buf[total++] = ' ';
	}

	/* Destination operand */
	if (insn->itype != MSP430_ITYPE_NOARG)
		total += format_operand(stab, buf + total,
					max_len - total,
					insn->dst_mode,
					insn->dst_addr,
					insn->dst_reg);

	if (total < max_len)
		buf[total] = 0;
	else if (total) {
		total--;
		buf[total] = 0;
	}

	return total;
}

void cproc_disassemble(cproc_t cp,
		       address_t offset, const uint8_t *data, int length)
{
	stab_t stab = cproc_stab(cp);
	int first_line = 1;

	while (length) {
		struct msp430_instruction insn = {0};
		int retval;
		int count;
		int i;
		address_t oboff;
		char obname[64];
		char buf[256];
		int len = 0;

		if (!stab_nearest(stab, offset, obname, sizeof(obname),
				  &oboff)) {
			if (!oboff)
				cproc_printf(cp, "\x1b[m%s:\x1b[0m", obname);
			else if (first_line)
				cproc_printf(cp, "\x1b[m%s+0x%x:\x1b[0m",
					     obname, oboff);
		}
		first_line = 0;

		retval = dis_decode(data, offset, length, &insn);
		count = retval > 0 ? retval : 2;
		if (count > length)
			count = length;

		len += snprintf(buf + len, sizeof(buf) - len,
				"    \x1b[36m%05x\x1b[0m:", offset);

		for (i = 0; i < count; i++)
			len += snprintf(buf + len, sizeof(buf) - len,
					" %02x", data[i]);

		while (i < 7) {
			buf[len++] = ' ';
			buf[len++] = ' ';
			buf[len++] = ' ';
			i++;
		}

		if (retval >= 0)
			len += dis_format(stab, buf + len, sizeof(buf) - len,
					  &insn);

		buf[len] = 0;
		cproc_printf(cp, "%s", buf);
		offset += count;
		length -= count;
		data += count;
	}
}

void cproc_hexdump(cproc_t cp, address_t addr, const uint8_t *data, int data_len)
{
	int offset = 0;

	while (offset < data_len) {
		char buf[128];
		int len = 0;
		int i, j;

		/* Address label */
		len += snprintf(buf + len, sizeof(buf) - len,
				"    \x1b[36m%05x:\x1b[0m", offset + addr);

		/* Hex portion */
		for (i = 0; i < 16 && offset + i < data_len; i++)
			len += snprintf(buf + len, sizeof(buf) - len,
					" %02x", data[offset + i]);
		for (j = i; j < 16; j++) {
			buf[len++] = ' ';
			buf[len++] = ' ';
			buf[len++] = ' ';
		}

		/* Printable characters */
		len += snprintf(buf + len, sizeof(buf) - len,
				" \x1b[32m|");
		for (j = 0; j < i; j++) {
			int c = data[offset + j];

			buf[len++] = (c >= 32 && c <= 126) ? c : '.';
		}
		for (; j < 16; j++)
			buf[len++] = ' ';
		len += snprintf(buf + len, sizeof(buf) - len,
				"|\x1b[0m");

		cproc_printf(cp, "%s", buf);
		offset += i;
	}
}

void cproc_regs(cproc_t cp, const address_t *regs)
{
	int i;

	for (i = 0; i < 4; i++) {
		int j;
		char buf[128];
		int len = 0;

		for (j = 0; j < 4; j++)
			buf[len++] = ' ';
		for (j = 0; j < 4; j++) {
			int k = j * 4 + i;

			len += snprintf(buf + len, sizeof(buf) - len,
					"(\x1b[1m%3s:\x1b[0m %05x)  ",
					dis_reg_name(k), regs[k]);
		}

		cproc_printf(cp, "%s", buf);
	}
}
