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

#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "dis.h"
#include "stab.h"
#include "util.h"

/**********************************************************************/
/* Disassembler
 */

/* Decode a single-operand instruction.
 *
 * Returns the number of bytes consumed in decoding, or -1 if the a
 * valid single-operand instruction could not be found.
 */
static int decode_single(u_int8_t *code, u_int16_t offset, u_int16_t size,
			 struct msp430_instruction *insn)
{
	u_int16_t op = (code[1] << 8) | code[0];
	int need_arg = 0;

	insn->op = op & 0xff80;
	insn->is_byte_op = op & 0x0400;

	insn->dst_mode = (op >> 4) & 0x3;
	insn->dst_reg = op & 0xf;

	switch (insn->dst_mode) {
	case MSP430_AMODE_REGISTER: break;

	case MSP430_AMODE_INDEXED:
		need_arg = 1;
		if (insn->dst_reg == MSP430_REG_PC) {
			insn->dst_addr = offset + 2;
			insn->dst_mode = MSP430_AMODE_SYMBOLIC;
		} else if (insn->dst_reg == MSP430_REG_SR)
			insn->dst_mode = MSP430_AMODE_ABSOLUTE;
		break;

	case MSP430_AMODE_INDIRECT: break;

	case MSP430_AMODE_INDIRECT_INC:
		if (insn->dst_reg == MSP430_REG_PC) {
			insn->dst_mode = MSP430_AMODE_IMMEDIATE;
			need_arg = 1;
		}
		break;

	default: break;
	}

	if (need_arg) {
		if (size < 4)
			return -1;

		insn->dst_addr += (code[3] << 8) | code[2];
		return 4;
	}

	return 2;
}

/* Decode a double-operand instruction.
 *
 * Returns the number of bytes consumed or -1 if a valid instruction
 * could not be found.
 */
static int decode_double(u_int8_t *code, u_int16_t offset, u_int16_t size,
			 struct msp430_instruction *insn)
{
	u_int16_t op = (code[1] << 8) | code[0];
	int need_src = 0;
	int need_dst = 0;
	int ret = 2;

	insn->op = op & 0xf000;
	insn->is_byte_op = op & 0x0040;

	insn->src_mode = (op >> 4) & 0x3;
	insn->src_reg = (op >> 8) & 0xf;

	insn->dst_mode = (op >> 7) & 0x1;
	insn->dst_reg = op & 0xf;

	switch (insn->dst_mode) {
	case MSP430_AMODE_REGISTER: break;
	case MSP430_AMODE_INDEXED:
		need_dst = 1;

		if (insn->dst_reg == MSP430_REG_PC) {
			insn->dst_mode = MSP430_AMODE_SYMBOLIC;
			insn->dst_addr = offset + 2;
		} else if (insn->dst_reg == MSP430_REG_SR)
			insn->dst_mode = MSP430_AMODE_ABSOLUTE;
		break;

	default: break;
	}

	switch (insn->src_mode) {
	case MSP430_AMODE_REGISTER: break;
	case MSP430_AMODE_INDEXED:
		need_src = 1;

		if (insn->src_reg == MSP430_REG_PC) {
			insn->src_mode = MSP430_AMODE_SYMBOLIC;
			insn->dst_addr = offset + 2;
		} else if (insn->src_reg == MSP430_REG_SR)
			insn->src_mode = MSP430_AMODE_ABSOLUTE;
		else if (insn->src_reg == MSP430_REG_R3)
			need_src = 0;
		break;

	case MSP430_AMODE_INDIRECT: break;

	case MSP430_AMODE_INDIRECT_INC:
		if (insn->src_reg == MSP430_REG_PC) {
			insn->src_mode = MSP430_AMODE_IMMEDIATE;
			need_src = 1;
		}
		break;

	default: break;
	}

	offset += 2;
	code += 2;
	size -= 2;

	if (need_src) {
		if (size < 2)
			return -1;

		insn->src_addr += (code[1] << 8) | code[0];
		offset += 2;
		code += 2;
		size -= 2;
		ret += 2;
	}

	if (need_dst) {
		if (size < 2)
			return -1;

		insn->dst_addr += (code[1] << 8) | code[0];
		ret += 2;
	}

	return ret;
}

/* Decode a jump instruction.
 *
 * All jump instructions are one word in length, so this function
 * always returns 2 (to indicate the consumption of 2 bytes).
 */
static int decode_jump(u_int8_t *code, u_int16_t offset, u_int16_t len,
		       struct msp430_instruction *insn)
{
	u_int16_t op = (code[1] << 8) | code[0];
	int tgtrel = op & 0x3ff;

	if (tgtrel & 0x200)
		tgtrel -= 0x400;

	insn->op = op & 0xfc00;
	insn->dst_addr = offset + 2 + tgtrel * 2;
	insn->dst_mode = MSP430_AMODE_SYMBOLIC;
	insn->dst_reg = MSP430_REG_PC;

	return 2;
}

static void remap_cgen(msp430_amode_t *mode,
		       u_int16_t *addr,
		       msp430_reg_t *reg)
{
	if (*reg == MSP430_REG_SR) {
		if (*mode == MSP430_AMODE_INDIRECT) {
			*mode = MSP430_AMODE_IMMEDIATE;
			*addr = 4;
		} else if (*mode == MSP430_AMODE_INDIRECT_INC) {
			*mode = MSP430_AMODE_IMMEDIATE;
			*addr = 8;
		}
	} else if (*reg == MSP430_REG_R3) {
		if (*mode == MSP430_AMODE_REGISTER)
			*addr = 0;
		else if (*mode == MSP430_AMODE_INDEXED)
			*addr = 1;
		else if (*mode == MSP430_AMODE_INDIRECT)
			*addr = 2;
		else if (*mode == MSP430_AMODE_INDIRECT_INC)
			*addr = 0xffff;

		*mode = MSP430_AMODE_IMMEDIATE;
	}
}

/* Take a decoded instruction and replace certain addressing modes of
 * the constant generator registers with their corresponding immediate
 * values.
 */
static void find_cgens(struct msp430_instruction *insn)
{
	if (insn->itype == MSP430_ITYPE_DOUBLE)
		remap_cgen(&insn->src_mode, &insn->src_addr,
			   &insn->src_reg);
	else if (insn->itype == MSP430_ITYPE_SINGLE)
		remap_cgen(&insn->dst_mode, &insn->dst_addr,
			   &insn->dst_reg);
}

/* Recognise special cases of real instructions and translate them to
 * emulated instructions.
 */
static void find_emulated_ops(struct msp430_instruction *insn)
{
	switch (insn->op) {
	case MSP430_OP_ADD:
		if (insn->src_mode == MSP430_AMODE_IMMEDIATE) {
			if (insn->src_addr == 1) {
				insn->op = MSP430_OP_INC;
				insn->itype = MSP430_ITYPE_SINGLE;
			} else if (insn->src_addr == 2) {
				insn->op = MSP430_OP_INCD;
				insn->itype = MSP430_ITYPE_SINGLE;
			}
		} else if (insn->dst_mode == insn->src_mode &&
			   insn->dst_reg == insn->src_reg &&
			   insn->dst_addr == insn->src_addr) {
			insn->op = MSP430_OP_RLA;
			insn->itype = MSP430_ITYPE_SINGLE;
		}
		break;

	case MSP430_OP_ADDC:
		if (insn->src_mode == MSP430_AMODE_IMMEDIATE &&
		    !insn->src_addr) {
			insn->op = MSP430_OP_ADC;
			insn->itype = MSP430_ITYPE_SINGLE;
		} else if (insn->dst_mode == insn->src_mode &&
			   insn->dst_reg == insn->src_reg &&
			   insn->dst_addr == insn->src_addr) {
			insn->op = MSP430_OP_RLC;
			insn->itype = MSP430_ITYPE_SINGLE;
		}
		break;

	case MSP430_OP_BIC:
		if (insn->dst_mode == MSP430_AMODE_REGISTER &&
		    insn->dst_reg == MSP430_REG_SR &&
		    insn->src_mode == MSP430_AMODE_IMMEDIATE) {
			if (insn->src_addr == 1) {
				insn->op = MSP430_OP_CLRC;
				insn->itype = MSP430_ITYPE_NOARG;
			} else if (insn->src_addr == 4) {
				insn->op = MSP430_OP_CLRN;
				insn->itype = MSP430_ITYPE_NOARG;
			} else if (insn->src_addr == 2) {
				insn->op = MSP430_OP_CLRZ;
				insn->itype = MSP430_ITYPE_NOARG;
			} else if (insn->src_addr == 8) {
				insn->op = MSP430_OP_DINT;
				insn->itype = MSP430_ITYPE_NOARG;
			}
		}
		break;

	case MSP430_OP_BIS:
		if (insn->dst_mode == MSP430_AMODE_REGISTER &&
		    insn->dst_reg == MSP430_REG_SR &&
		    insn->src_mode == MSP430_AMODE_IMMEDIATE) {
			if (insn->src_addr == 1) {
				insn->op = MSP430_OP_SETC;
				insn->itype = MSP430_ITYPE_NOARG;
			} else if (insn->src_addr == 4) {
				insn->op = MSP430_OP_SETN;
				insn->itype = MSP430_ITYPE_NOARG;
			} else if (insn->src_addr == 2) {
				insn->op = MSP430_OP_SETZ;
				insn->itype = MSP430_ITYPE_NOARG;
			} else if (insn->src_addr == 8) {
				insn->op = MSP430_OP_EINT;
				insn->itype = MSP430_ITYPE_NOARG;
			}
		}
		break;

	case MSP430_OP_CMP:
		if (insn->src_mode == MSP430_AMODE_IMMEDIATE &&
		    !insn->src_addr) {
			insn->op = MSP430_OP_TST;
			insn->itype = MSP430_ITYPE_SINGLE;
		}
		break;

	case MSP430_OP_DADD:
		if (insn->src_mode == MSP430_AMODE_IMMEDIATE &&
		    !insn->src_addr) {
			insn->op = MSP430_OP_DADC;
			insn->itype = MSP430_ITYPE_SINGLE;
		}
		break;

	case MSP430_OP_MOV:
		if (insn->src_mode == MSP430_AMODE_INDIRECT_INC &&
		    insn->src_reg == MSP430_REG_SP) {
			if (insn->dst_mode == MSP430_AMODE_REGISTER &&
			    insn->dst_reg == MSP430_REG_PC) {
				insn->op = MSP430_OP_RET;
				insn->itype = MSP430_ITYPE_NOARG;
			} else {
				insn->op = MSP430_OP_POP;
				insn->itype = MSP430_ITYPE_SINGLE;
			}
		} else if (insn->dst_mode == MSP430_AMODE_REGISTER &&
			   insn->dst_reg == MSP430_REG_PC) {
			insn->op = MSP430_OP_BR;
			insn->itype = MSP430_ITYPE_SINGLE;
			insn->dst_mode = insn->src_mode;
			insn->dst_reg = insn->src_reg;
			insn->dst_addr = insn->src_addr;
		} else if (insn->src_mode == MSP430_AMODE_IMMEDIATE &&
			   !insn->src_addr) {
			if (insn->dst_mode == MSP430_AMODE_REGISTER &&
			    insn->dst_reg == MSP430_REG_R3) {
				insn->op = MSP430_OP_NOP;
				insn->itype = MSP430_ITYPE_NOARG;
			} else {
				insn->op = MSP430_OP_CLR;
				insn->itype = MSP430_ITYPE_SINGLE;
			}
		}
		break;

	case MSP430_OP_SUB:
		if (insn->src_mode == MSP430_AMODE_IMMEDIATE) {
			if (insn->src_addr == 1) {
				insn->op = MSP430_OP_DEC;
				insn->itype = MSP430_ITYPE_SINGLE;
			} else if (insn->src_addr == 2) {
				insn->op = MSP430_OP_DECD;
				insn->itype = MSP430_ITYPE_SINGLE;
			}
		}
		break;

	case MSP430_OP_SUBC:
		if (insn->src_mode == MSP430_AMODE_IMMEDIATE &&
		    !insn->src_addr) {
			insn->op = MSP430_OP_SBC;
			insn->itype = MSP430_ITYPE_SINGLE;
		}
		break;

	case MSP430_OP_XOR:
		if (insn->src_mode == MSP430_AMODE_IMMEDIATE &&
		    insn->src_addr == 0xffff) {
			insn->op = MSP430_OP_INV;
			insn->itype = MSP430_ITYPE_SINGLE;
		}
		break;

	default: break;
	}
}

/* Decode a single instruction.
 *
 * Returns the number of bytes consumed, or -1 if an error occured.
 *
 * The caller needs to pass a pointer to the bytes to be decoded, the
 * virtual offset of those bytes, and the maximum number available. If
 * successful, the decoded instruction is written into the structure
 * pointed to by insn.
 */
int dis_decode(u_int8_t *code, u_int16_t offset, u_int16_t len,
	       struct msp430_instruction *insn)
{
	u_int16_t op;
	int ret;

	memset(insn, 0, sizeof(*insn));

	if (len < 2)
		return -1;

	insn->offset = offset;
	op = (code[1] << 8) | code[0];

	if ((op & 0xf000) == 0x1000)
		insn->itype = MSP430_ITYPE_SINGLE;
	else if ((op & 0xff00) >= 0x2000 &&
		 (op & 0xff00) < 0x4000)
		insn->itype = MSP430_ITYPE_JUMP;
	else if ((op & 0xf000) >= 0x4000)
		insn->itype = MSP430_ITYPE_DOUBLE;
	else
		return -1;

	switch (insn->itype) {
	case MSP430_ITYPE_SINGLE:
		ret = decode_single(code, offset, len, insn);
		break;

	case MSP430_ITYPE_DOUBLE:
		ret = decode_double(code, offset, len, insn);
		break;

	case MSP430_ITYPE_JUMP:
		ret = decode_jump(code, offset, len, insn);
		break;

	default: break;
	}

	find_cgens(insn);
	find_emulated_ops(insn);

	insn->len = ret;
	return ret;
}

/* Return the mnemonic for an operation, if possible.
 *
 * If the argument is not a valid operation, this function returns the
 * string "???".
 */
static const char *msp_op_name(msp430_op_t op)
{
	static const struct {
		msp430_op_t     op;
		const char      *mnemonic;
	} ops[] = {
		/* Single operand */
		{MSP430_OP_RRC,         "RRC"},
		{MSP430_OP_SWPB,        "SWPB"},
		{MSP430_OP_RRA,         "RRA"},
		{MSP430_OP_SXT,         "SXT"},
		{MSP430_OP_PUSH,        "PUSH"},
		{MSP430_OP_CALL,        "CALL"},
		{MSP430_OP_RETI,        "RETI"},

		/* Jump */
		{MSP430_OP_JNZ,         "JNZ"},
		{MSP430_OP_JZ,          "JZ"},
		{MSP430_OP_JNC,         "JNC"},
		{MSP430_OP_JC,          "JC"},
		{MSP430_OP_JN,          "JN"},
		{MSP430_OP_JL,          "JL"},
		{MSP430_OP_JGE,         "JGE"},
		{MSP430_OP_JMP,         "JMP"},

		/* Double operand */
		{MSP430_OP_MOV,         "MOV"},
		{MSP430_OP_ADD,         "ADD"},
		{MSP430_OP_ADDC,        "ADDC"},
		{MSP430_OP_SUBC,        "SUBC"},
		{MSP430_OP_SUB,         "SUB"},
		{MSP430_OP_CMP,         "CMP"},
		{MSP430_OP_DADD,        "DADD"},
		{MSP430_OP_BIT,         "BIT"},
		{MSP430_OP_BIC,         "BIC"},
		{MSP430_OP_BIS,         "BIS"},
		{MSP430_OP_XOR,         "XOR"},
		{MSP430_OP_AND,         "AND"},

		/* Emulated instructions */
		{MSP430_OP_ADC,         "ADC"},
		{MSP430_OP_BR,          "BR"},
		{MSP430_OP_CLR,         "CLR"},
		{MSP430_OP_CLRC,        "CLRC"},
		{MSP430_OP_CLRN,        "CLRN"},
		{MSP430_OP_CLRZ,        "CLRZ"},
		{MSP430_OP_DADC,        "DADC"},
		{MSP430_OP_DEC,         "DEC"},
		{MSP430_OP_DECD,        "DECD"},
		{MSP430_OP_DINT,        "DINT"},
		{MSP430_OP_EINT,        "EINT"},
		{MSP430_OP_INC,         "INC"},
		{MSP430_OP_INCD,        "INCD"},
		{MSP430_OP_INV,         "INV"},
		{MSP430_OP_NOP,         "NOP"},
		{MSP430_OP_POP,         "POP"},
		{MSP430_OP_RET,         "RET"},
		{MSP430_OP_RLA,         "RLA"},
		{MSP430_OP_RLC,         "RLC"},
		{MSP430_OP_SBC,         "SBC"},
		{MSP430_OP_SETC,        "SETC"},
		{MSP430_OP_SETN,        "SETN"},
		{MSP430_OP_SETZ,        "SETZ"},
		{MSP430_OP_TST,         "TST"}
	};
	int i;

	for (i = 0; i < ARRAY_LEN(ops); i++)
		if (op == ops[i].op)
			return ops[i].mnemonic;

	return "???";
}

static const char *const msp430_reg_names[] = {
	"PC",  "SP",  "SR",  "R3",
	"R4",  "R5",  "R6",  "R7",
	"R8",  "R9",  "R10", "R11",
	"R12", "R13", "R14", "R15"
};

/* Given an operands addressing mode, value and associated register,
 * print the canonical representation of it to stdout.
 *
 * Returns the number of characters printed.
 */
static int format_operand(char *buf, int max_len,
			  msp430_amode_t amode, u_int16_t addr,
			  msp430_reg_t reg)
{
	const char *prefix = "";
	const char *suffix = "";
	char rbuf[32];
	char name[64];
	u_int16_t offset;
	int numeric = 0;

	assert (reg >= 0 && reg < ARRAY_LEN(msp430_reg_names));

	switch (amode) {
	case MSP430_AMODE_REGISTER:
		return snprintf(buf, max_len, "%s", msp430_reg_names[reg]);

	case MSP430_AMODE_INDEXED:
		snprintf(rbuf, sizeof(rbuf), "(%s)", msp430_reg_names[reg]);
		suffix = rbuf;
		numeric = 1;
		break;

	case MSP430_AMODE_SYMBOLIC:
		break;

	case MSP430_AMODE_ABSOLUTE:
		prefix = "&";
		break;

	case MSP430_AMODE_INDIRECT:
		return snprintf(buf, max_len, "@%s", msp430_reg_names[reg]);

	case MSP430_AMODE_INDIRECT_INC:
		return snprintf(buf, max_len, "@%s+", msp430_reg_names[reg]);

	case MSP430_AMODE_IMMEDIATE:
		prefix = "#";
		numeric = 1;
		break;

	default:
		return snprintf(buf, max_len, "???");
	}

	if ((!numeric ||
	     (addr >= 0x200 && addr < 0xfff0)) &&
	    !stab_nearest(addr, name, sizeof(name), &offset) &&
	    !offset)
		return snprintf(buf, max_len, "%s%s%s",
				prefix, name, suffix);

	return snprintf(buf, max_len,
			numeric ? "%s0x%x%s" : "%s0x%04x%s",
			prefix, addr, suffix);
}

/* Write assembly language for the instruction to this buffer */
int dis_format(char *buf, int max_len,
	       const struct msp430_instruction *insn)
{
	int count = 0;

	/* Opcode mnemonic */
	count = snprintf(buf, max_len, "%s", msp_op_name(insn->op));
	if (insn->is_byte_op)
		count += snprintf(buf + count, max_len - count, ".B");
	while (count < 8 && count + 1 < max_len)
		buf[count++] = ' ';

	/* Source operand */
	if (insn->itype == MSP430_ITYPE_DOUBLE) {
		count += format_operand(buf + count,
					max_len - count,
					insn->src_mode,
					insn->src_addr,
					insn->src_reg);

		if (count + 1 < max_len)
			buf[count++] = ',';

		while (count < 19 && count + 1 < max_len)
			buf[count++] = ' ';

		if (count + 1 < max_len)
			buf[count++] = ' ';
	}

	/* Destination operand */
	if (insn->itype != MSP430_ITYPE_NOARG)
		count += format_operand(buf + count,
					max_len - count,
					insn->dst_mode,
					insn->dst_addr,
					insn->dst_reg);

	buf[count] = 0;
	return count;
}
