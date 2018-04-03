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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "dis.h"
#include "util.h"

#define ALL_ONES               0xfffff
#define EXTENSION_BIT          0x20000

/**********************************************************************/
/* Disassembler
 */

static address_t add_index(address_t reg_base, address_t index,
			   int is_20bit)
{
	return (reg_base + index) & (is_20bit ? 0xfffff : 0xffff);
}

static int decode_00xx(const uint8_t *code, address_t len,
		       struct msp430_instruction *insn)
{
	uint16_t op = code[0] | (code[1] << 8);
	int subtype = (op >> 4) & 0xf;
	int have_arg = 0;
	address_t arg = 0;

	/* Parameters common to most cases */
	insn->op = MSP430_OP_MOVA;
	insn->itype = MSP430_ITYPE_DOUBLE;
	insn->dsize = MSP430_DSIZE_AWORD;
	insn->dst_mode = MSP430_AMODE_REGISTER;
	insn->dst_reg = op & 0xf;
	insn->src_mode = MSP430_AMODE_REGISTER;
	insn->src_reg = (op >> 8) & 0xf;

	if (len >= 4) {
		have_arg = 1;
		arg = code[2] | (code[3] << 8);
	}

	switch (subtype) {
	case 0:
		insn->src_mode = MSP430_AMODE_INDIRECT;
		return 2;

	case 1:
		insn->src_mode = MSP430_AMODE_INDIRECT_INC;
		return 2;

	case 2:
		if (!have_arg)
			return -1;
		insn->src_mode = MSP430_AMODE_ABSOLUTE;
		insn->src_addr = ((op & 0xf00) << 8) | arg;
		return 4;

	case 3:
		if (!have_arg)
			return -1;
		insn->src_mode = MSP430_AMODE_INDEXED;
		insn->src_addr = arg;
		return 4;

	case 4:
	case 5:
		/* RxxM */
		insn->itype = MSP430_ITYPE_DOUBLE;
		insn->op = op & 0xf3e0;
		insn->dst_mode = MSP430_AMODE_REGISTER;
		insn->dst_reg = op & 0xf;
		insn->src_mode = MSP430_AMODE_IMMEDIATE;
		insn->src_addr = 1 + ((op >> 10) & 3);
		insn->dsize = (op & 0x0010) ?
			MSP430_DSIZE_WORD : MSP430_DSIZE_AWORD;
		return 2;

	case 6:
		if (!have_arg)
			return -1;

		insn->dst_mode = MSP430_AMODE_ABSOLUTE;
		insn->dst_addr = ((op & 0xf) << 16) | arg;
		return 4;

	case 7:
		if (!have_arg)
			return -1;
		insn->dst_mode = MSP430_AMODE_INDEXED;
		insn->dst_addr = arg;
		return 4;

	case 8:
		if (!have_arg)
			return -1;
		insn->src_mode = MSP430_AMODE_IMMEDIATE;
		insn->src_addr = ((op & 0xf00) << 8) | arg;
		return 4;

	case 9:
		if (!have_arg)
			return -1;
		insn->op = MSP430_OP_CMPA;
		insn->src_mode = MSP430_AMODE_IMMEDIATE;
		insn->src_addr = ((op & 0xf00) << 8) | arg;
		return 4;

	case 10:
		if (!have_arg)
			return -1;
		insn->op = MSP430_OP_ADDA;
		insn->src_mode = MSP430_AMODE_IMMEDIATE;
		insn->src_addr = ((op & 0xf00) << 8) | arg;
		return 4;

	case 11:
		if (!have_arg)
			return -1;
		insn->op = MSP430_OP_SUBA;
		insn->src_mode = MSP430_AMODE_IMMEDIATE;
		insn->src_addr = ((op & 0xf00) << 8) | arg;
		return 4;

	case 12:
		return 2;

	case 13:
		insn->op = MSP430_OP_CMPA;
		return 2;

	case 14:
		insn->op = MSP430_OP_ADDA;
		return 2;

	case 15:
		insn->op = MSP430_OP_SUBA;
		return 2;
	}

	return -1;
}

static int decode_13xx(const uint8_t *code, address_t len,
		       struct msp430_instruction *insn)
{
	uint16_t op = code[0] | (code[1] << 8);
	int subtype = (op >> 4) & 0xf;

	insn->itype = MSP430_ITYPE_SINGLE;
	insn->op = MSP430_OP_CALLA;

	switch (subtype) {
	case 0:
		insn->itype = MSP430_ITYPE_NOARG;
		insn->op = MSP430_OP_RETI;
		return 2;

	case 4:
		insn->dst_mode = MSP430_AMODE_REGISTER;
		insn->dst_reg = op & 0xf;
		return 2;

	case 5:
		insn->dst_mode = MSP430_AMODE_INDEXED;
		insn->dst_reg = op & 0xf;
		break;

	case 6:
		insn->dst_mode = MSP430_AMODE_INDIRECT;
		insn->dst_reg = op & 0xf;
		return 2;

	case 7:
		insn->dst_mode = MSP430_AMODE_INDIRECT_INC;
		insn->dst_reg = op & 0xf;
		return 2;

	case 8:
		insn->dst_mode = MSP430_AMODE_ABSOLUTE;
		insn->dst_addr = (address_t)(op & 0xf) << 16;
		break;

	case 9:
		insn->dst_mode = MSP430_AMODE_SYMBOLIC;
		insn->dst_addr = (address_t)(op & 0xf) << 16;
		break;

	case 11:
		insn->dst_mode = MSP430_AMODE_IMMEDIATE;
		insn->dst_addr = (address_t)(op & 0xf) << 16;
		break;

	default:
		return -1;
	}

	if (len < 4)
		return -1;

	insn->dsize = MSP430_DSIZE_AWORD;
	insn->dst_addr |= code[2];
	insn->dst_addr |= code[3] << 8;

	return 4;
}

static int decode_14xx(const uint8_t *code,
		       struct msp430_instruction *insn)
{
	uint16_t op = (code[1] << 8) | code[0];

	/* PUSHM/POPM */
	insn->itype = MSP430_ITYPE_DOUBLE;
	insn->op = op & 0xfe00;
	insn->dst_mode = MSP430_AMODE_REGISTER;
	insn->dst_reg = op & 0xf;
	insn->src_mode = MSP430_AMODE_IMMEDIATE;
	insn->src_addr = 1 + ((op >> 4) & 0xf);
	insn->dsize = (op & 0x0100) ?
		MSP430_DSIZE_WORD : MSP430_DSIZE_AWORD;

	return 2;
}

/* Decode a single-operand instruction.
 *
 * Returns the number of bytes consumed in decoding, or -1 if the a
 * valid single-operand instruction could not be found.
 */
static int decode_single(const uint8_t *code, address_t offset,
			 address_t size, struct msp430_instruction *insn)
{
	uint16_t op = (code[1] << 8) | code[0];
	int need_arg = 0;

	insn->itype = MSP430_ITYPE_SINGLE;
	insn->op = op & 0xff80;
	insn->dsize = (op & 0x0400) ? MSP430_DSIZE_BYTE : MSP430_DSIZE_WORD;

	insn->dst_mode = (op >> 4) & 0x3;
	insn->dst_reg = op & 0xf;

	switch (insn->dst_mode) {
	case MSP430_AMODE_REGISTER: break;

	case MSP430_AMODE_INDEXED:
		need_arg = 1;
		if (insn->dst_reg == MSP430_REG_PC) {
			insn->dst_addr = offset + 4;
			insn->dst_mode = MSP430_AMODE_SYMBOLIC;
		} else if (insn->dst_reg == MSP430_REG_SR) {
			insn->dst_mode = MSP430_AMODE_ABSOLUTE;
		} else if (insn->dst_reg == MSP430_REG_R3) {
			need_arg = 0; /* constant generator: #1 */
		}
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

		insn->dst_addr = add_index(insn->dst_addr,
			(code[3] << 8) | code[2], 0);
		return 4;
	}

	return 2;
}

/* Decode a double-operand instruction.
 *
 * Returns the number of bytes consumed or -1 if a valid instruction
 * could not be found.
 */
static int decode_double(const uint8_t *code, address_t offset,
			 address_t size, struct msp430_instruction *insn,
			 uint16_t ex_word)
{
	uint16_t op = (code[1] << 8) | code[0];
	int need_src = 0;
	int need_dst = 0;
	int ret = 2;

	/* Decode and consume opcode */
	insn->itype = MSP430_ITYPE_DOUBLE;
	insn->op = op & 0xf000;
	insn->dsize = (op & 0x0040) ? MSP430_DSIZE_BYTE : MSP430_DSIZE_WORD;

	insn->src_mode = (op >> 4) & 0x3;
	insn->src_reg = (op >> 8) & 0xf;

	insn->dst_mode = (op >> 7) & 0x1;
	insn->dst_reg = op & 0xf;

	offset += 2;
	code += 2;
	size -= 2;

	/* Decode and consume source operand */
	switch (insn->src_mode) {
	case MSP430_AMODE_REGISTER: break;
	case MSP430_AMODE_INDEXED:
		need_src = 1;

		if (insn->src_reg == MSP430_REG_PC) {
			insn->src_mode = MSP430_AMODE_SYMBOLIC;
			insn->src_addr = offset;
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

	if (need_src) {
		if (size < 2)
			return -1;

		insn->src_addr = add_index(insn->src_addr,
			((ex_word << 9) & 0xf0000) |
			((code[1] << 8) | code[0]),
			ex_word);
		offset += 2;
		code += 2;
		size -= 2;
		ret += 2;
	}

	/* Decode and consume destination operand */
	switch (insn->dst_mode) {
	case MSP430_AMODE_REGISTER: break;
	case MSP430_AMODE_INDEXED:
		need_dst = 1;

		if (insn->dst_reg == MSP430_REG_PC) {
			insn->dst_mode = MSP430_AMODE_SYMBOLIC;
			insn->dst_addr = offset;
		} else if (insn->dst_reg == MSP430_REG_SR)
			insn->dst_mode = MSP430_AMODE_ABSOLUTE;
		break;

	default: break;
	}

	if (need_dst) {
		if (size < 2)
			return -1;

		insn->dst_addr = add_index(insn->dst_addr,
			((ex_word << 16) & 0xf0000) |
			(code[1] << 8) | code[0],
			ex_word);
		ret += 2;
	}

	return ret;
}

/* Decode a jump instruction.
 *
 * All jump instructions are one word in length, so this function
 * always returns 2 (to indicate the consumption of 2 bytes).
 */
static int decode_jump(const uint8_t *code, address_t offset,
		       struct msp430_instruction *insn)
{
	uint16_t op = (code[1] << 8) | code[0];
	int tgtrel = op & 0x3ff;

	if (tgtrel & 0x200)
		tgtrel -= 0x400;

	insn->op = op & 0xfc00;
	insn->itype = MSP430_ITYPE_JUMP;
	insn->dst_addr = offset + 2 + tgtrel * 2;
	insn->dst_mode = MSP430_AMODE_SYMBOLIC;
	insn->dst_reg = MSP430_REG_PC;

	return 2;
}

static void remap_cgen(msp430_amode_t *mode,
		       address_t *addr,
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
			*addr = ALL_ONES;

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

	case MSP430_OP_ADDA:
		if (insn->src_mode == MSP430_AMODE_IMMEDIATE &&
		    insn->src_addr == 2) {
			insn->op = MSP430_OP_INCDA;
			insn->itype = MSP430_ITYPE_SINGLE;
		}
		break;

	case MSP430_OP_ADDX:
		if (insn->src_mode == MSP430_AMODE_IMMEDIATE) {
			if (insn->src_addr == 1) {
				insn->op = MSP430_OP_INCX;
				insn->itype = MSP430_ITYPE_SINGLE;
			} else if (insn->src_addr == 2) {
				insn->op = MSP430_OP_INCDX;
				insn->itype = MSP430_ITYPE_SINGLE;
			}
		} else if (insn->dst_mode == insn->src_mode &&
			   insn->dst_reg == insn->src_reg &&
			   insn->dst_addr == insn->src_addr) {
			insn->op = MSP430_OP_RLAX;
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

	case MSP430_OP_ADDCX:
		if (insn->src_mode == MSP430_AMODE_IMMEDIATE &&
		    !insn->src_addr) {
			insn->op = MSP430_OP_ADCX;
			insn->itype = MSP430_ITYPE_SINGLE;
		} else if (insn->dst_mode == insn->src_mode &&
			   insn->dst_reg == insn->src_reg &&
			   insn->dst_addr == insn->src_addr) {
			insn->op = MSP430_OP_RLCX;
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

	case MSP430_OP_CMPA:
		if (insn->src_mode == MSP430_AMODE_IMMEDIATE &&
		    !insn->src_addr) {
			insn->op = MSP430_OP_TSTA;
			insn->itype = MSP430_ITYPE_SINGLE;
		}
		break;

	case MSP430_OP_CMPX:
		if (insn->src_mode == MSP430_AMODE_IMMEDIATE &&
		    !insn->src_addr) {
			insn->op = MSP430_OP_TSTX;
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

	case MSP430_OP_DADDX:
		if (insn->src_mode == MSP430_AMODE_IMMEDIATE &&
		    !insn->src_addr) {
			insn->op = MSP430_OP_DADCX;
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

	case MSP430_OP_MOVA:
		if (insn->src_mode == MSP430_AMODE_INDIRECT_INC &&
		    insn->src_reg == MSP430_REG_SP) {
			if (insn->dst_mode == MSP430_AMODE_REGISTER &&
			    insn->dst_reg == MSP430_REG_PC) {
				insn->op = MSP430_OP_RETA;
				insn->itype = MSP430_ITYPE_NOARG;
			} else {
				insn->op = MSP430_OP_POPX;
				insn->itype = MSP430_ITYPE_SINGLE;
			}
		} else if (insn->dst_mode == MSP430_AMODE_REGISTER &&
			   insn->dst_reg == MSP430_REG_PC) {
			insn->op = MSP430_OP_BRA;
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
				insn->op = MSP430_OP_CLRX;
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

	case MSP430_OP_SUBA:
		if (insn->src_mode == MSP430_AMODE_IMMEDIATE &&
		    insn->src_addr == 2) {
			insn->op = MSP430_OP_DECDA;
			insn->itype = MSP430_ITYPE_SINGLE;
		}
		break;

	case MSP430_OP_SUBX:
		if (insn->src_mode == MSP430_AMODE_IMMEDIATE) {
			if (insn->src_addr == 1) {
				insn->op = MSP430_OP_DECX;
				insn->itype = MSP430_ITYPE_SINGLE;
			} else if (insn->src_addr == 2) {
				insn->op = MSP430_OP_DECDX;
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

	case MSP430_OP_SUBCX:
		if (insn->src_mode == MSP430_AMODE_IMMEDIATE &&
		    !insn->src_addr) {
			insn->op = MSP430_OP_SECX;
			insn->itype = MSP430_ITYPE_SINGLE;
		}
		break;

	case MSP430_OP_XOR:
		if (insn->src_mode == MSP430_AMODE_IMMEDIATE &&
		    insn->src_addr == ALL_ONES) {
			insn->op = MSP430_OP_INV;
			insn->itype = MSP430_ITYPE_SINGLE;
		}
		break;

	case MSP430_OP_XORX:
		if (insn->src_mode == MSP430_AMODE_IMMEDIATE &&
		    insn->src_addr == ALL_ONES) {
			insn->op = MSP430_OP_INVX;
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
int dis_decode(const uint8_t *code, address_t offset, address_t len,
	       struct msp430_instruction *insn)
{
	uint16_t op;
	uint16_t ex_word = 0;
	int ret;
	address_t ds_mask = ALL_ONES;

	memset(insn, 0, sizeof(*insn));
	insn->offset = offset;

	/* Perform decoding */
	if (len < 2)
		return -1;
	op = (code[1] << 8) | code[0];

	if ((op & 0xf800) == 0x1800) {
		ex_word = op;
		code += 2;
		offset += 2;
		len -= 2;

		if (len < 2)
			return -1;
		op = (code[1] << 8) | code[0];

		if ((op & 0xf000) >= 0x4000)
			ret = decode_double(code, offset, len, insn, ex_word);
		else if ((op & 0xf000) == 0x1000 && (op & 0xfc00) < 0x1280)
			ret = decode_single(code, offset, len, insn);
		else
			return -1;

		insn->op |= EXTENSION_BIT;
		ret += 2;

		if (insn->dst_mode == MSP430_AMODE_REGISTER &&
		    (insn->itype == MSP430_ITYPE_SINGLE ||
		     insn->src_mode == MSP430_AMODE_REGISTER)) {
			if ((ex_word >> 8) & 1) {
				if (insn->op != MSP430_OP_RRCX)
					return -1;
				insn->op = MSP430_OP_RRUX;
			}
			insn->rep_register = (ex_word >> 7) & 1;
			insn->rep_index = ex_word & 0xf;
		}

		if (!(ex_word & 0x40))
			insn->dsize |= 2;
	} else {
		if ((op & 0xf000) == 0x0000)
			ret = decode_00xx(code, len, insn);
		else if ((op & 0xfc00) == 0x1400)
			ret = decode_14xx(code, insn);
		else if ((op & 0xff00) == 0x1300)
			ret = decode_13xx(code, len, insn);
		else if ((op & 0xf000) == 0x1000)
			ret = decode_single(code, offset, len, insn);
		else if ((op & 0xf000) >= 0x2000 && (op & 0xf000) < 0x4000)
			ret = decode_jump(code, offset, insn);
		else if ((op & 0xf000) >= 0x4000)
			ret = decode_double(code, offset, len, insn, 0);
		else
			return -1;
	}

	/* Interpret "emulated" instructions, constant generation, and
	 * trim data sizes.
	 */
	find_cgens(insn);
	find_emulated_ops(insn);

	if (insn->dsize == MSP430_DSIZE_BYTE)
		ds_mask = 0xff;
	else if (insn->dsize == MSP430_DSIZE_WORD)
		ds_mask = 0xffff;

	if (insn->src_mode == MSP430_AMODE_IMMEDIATE)
		insn->src_addr &= ds_mask;
	if (insn->dst_mode == MSP430_AMODE_IMMEDIATE)
		insn->dst_addr &= ds_mask;

	insn->len = ret;
	return ret;
}

static const struct {
	msp430_op_t     op;
	const char      *mnemonic;
} opcode_names[] = {
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
	{MSP430_OP_TST,         "TST"},

	/* MSP430X double operand (extension word) */
	{MSP430_OP_MOVX,        "MOVX"},
	{MSP430_OP_ADDX,        "ADDX"},
	{MSP430_OP_ADDCX,       "ADDCX"},
	{MSP430_OP_SUBCX,       "SUBCX"},
	{MSP430_OP_SUBX,        "SUBX"},
	{MSP430_OP_CMPX,        "CMPX"},
	{MSP430_OP_DADDX,       "DADDX"},
	{MSP430_OP_BITX,        "BITX"},
	{MSP430_OP_BICX,        "BICX"},
	{MSP430_OP_BISX,        "BISX"},
	{MSP430_OP_XORX,        "XORX"},
	{MSP430_OP_ANDX,        "ANDX"},

	/* MSP430X single operand (extension word) */
	{MSP430_OP_RRCX,        "RRCX"},
	{MSP430_OP_RRUX,        "RRUX"},
	{MSP430_OP_SWPBX,       "SWPBX"},
	{MSP430_OP_RRAX,        "RRAX"},
	{MSP430_OP_SXTX,        "SXTX"},
	{MSP430_OP_PUSHX,       "PUSHX"},

	/* MSP430X group 13xx */
	{MSP430_OP_CALLA,	"CALLA"},

	/* MSP430X group 14xx */
	{MSP430_OP_PUSHM,	"PUSHM"},
	{MSP430_OP_POPM,	"POPM"},

	/* MSP430X address instructions */
	{MSP430_OP_MOVA,        "MOVA"},
	{MSP430_OP_CMPA,        "CMPA"},
	{MSP430_OP_SUBA,	"SUBA"},
	{MSP430_OP_ADDA,	"ADDA"},

	/* MSP430X group 00xx, non-address */
	{MSP430_OP_RRCM,        "RRCM"},
	{MSP430_OP_RRAM,        "RRAM"},
	{MSP430_OP_RLAM,	"RLAM"},
	{MSP430_OP_RRUM,	"RRUM"},

	/* MSP430X emulated instructions */
	{MSP430_OP_ADCX,	"ADCX"},
	{MSP430_OP_BRA,		"BRA"},
	{MSP430_OP_RETA,	"RETA"},
	{MSP430_OP_CLRX,	"CLRX"},
	{MSP430_OP_DADCX,	"DADCX"},
	{MSP430_OP_DECX,	"DECX"},
	{MSP430_OP_DECDA,	"DECDA"},
	{MSP430_OP_DECDX,	"DECDX"},
	{MSP430_OP_INCX,	"INCX"},
	{MSP430_OP_INCDA,	"INCDA"},
	{MSP430_OP_INVX,	"INVX"},
	{MSP430_OP_RLAX,	"RLAX"},
	{MSP430_OP_RLCX,	"RLCX"},
	{MSP430_OP_SECX,	"SECX"},
	{MSP430_OP_TSTA,	"TSTA"},
	{MSP430_OP_TSTX,	"TSTX"},
	{MSP430_OP_POPX,	"POPX"},
	{MSP430_OP_INCDX,	"INCDX"}
};

/* Return the mnemonic for an operation, if possible. */
const char *dis_opcode_name(msp430_op_t op)
{
	int i;

	for (i = 0; i < ARRAY_LEN(opcode_names); i++)
		if (op == opcode_names[i].op)
			return opcode_names[i].mnemonic;

	return NULL;
}

int dis_opcode_from_name(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_LEN(opcode_names); i++)
		if (!strcasecmp(name, opcode_names[i].mnemonic))
			return opcode_names[i].op;

	return -1;
}

static const char *const msp430_reg_names[] = {
	"PC",  "SP",  "SR",  "R3",
	"R4",  "R5",  "R6",  "R7",
	"R8",  "R9",  "R10", "R11",
	"R12", "R13", "R14", "R15"
};

int dis_reg_from_name(const char *name)
{
	int i;

	if (!strcasecmp(name, "pc"))
		return 0;
	if (!strcasecmp(name, "sp"))
		return 1;
	if (!strcasecmp(name, "sr"))
		return 2;

	if (toupper(*name) == 'R')
		name++;

	for (i = 0; name[i]; i++)
		if (!isdigit(name[i]))
			return -1;

	i = atoi(name);
	if (i < 0 || i > 15)
		return -1;

	return i;
}

const char *dis_reg_name(msp430_reg_t reg)
{
	if (reg <= 15)
		return msp430_reg_names[reg];

	return NULL;
}
