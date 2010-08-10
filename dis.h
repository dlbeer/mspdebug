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

#ifndef DIS_H_

#include <stdint.h>
#include "util.h"

/* Addressing modes.
 *
 * Addressing modes are not determined solely by the address mode bits
 * in an instruction. Rather, those bits specify one of four possible
 * modes (REGISTER, INDEXED, INDIRECT and INDIRECT_INC). Using some of
 * these modes in conjunction with special registers like PC or the
 * constant generator registers results in extra modes. For example, the
 * following code, written using INDIRECT_INC on PC:
 *
 *     MOV      @PC+, R5
 *     .word    0x5729
 *
 * can also be written as an instruction using IMMEDIATE addressing:
 *
 *     MOV      #0x5729, R5
 */
typedef enum {
	MSP430_AMODE_REGISTER           = 0x0,
	MSP430_AMODE_INDEXED            = 0x1,
	MSP430_AMODE_SYMBOLIC           = 0x81,
	MSP430_AMODE_ABSOLUTE           = 0x82,
	MSP430_AMODE_INDIRECT           = 0x2,
	MSP430_AMODE_INDIRECT_INC       = 0x3,
	MSP430_AMODE_IMMEDIATE          = 0x83
} msp430_amode_t;

/* MSP430 registers.
 *
 * These are divided into:
 *
 *     PC/R0:    program counter
 *     SP/R1:    stack pointer
 *     SR/R2:    status register/constant generator 1
 *     R3:       constant generator 2
 *     R4-R15:   general purpose registers
 */
typedef enum {
	MSP430_REG_PC           = 0,
	MSP430_REG_SP           = 1,
	MSP430_REG_SR           = 2,
	MSP430_REG_R3           = 3,
	MSP430_REG_R4           = 4,
	MSP430_REG_R5           = 5,
	MSP430_REG_R6           = 6,
	MSP430_REG_R7           = 7,
	MSP430_REG_R8           = 8,
	MSP430_REG_R9           = 9,
	MSP430_REG_R10          = 10,
	MSP430_REG_R11          = 11,
	MSP430_REG_R12          = 12,
	MSP430_REG_R13          = 13,
	MSP430_REG_R14          = 14,
	MSP430_REG_R15          = 15,
} msp430_reg_t;

/* Status register bits. */
#define MSP430_SR_V             0x0100
#define MSP430_SR_SCG1          0x0080
#define MSP430_SR_SCG0          0x0040
#define MSP430_SR_OSCOFF        0x0020
#define MSP430_SR_CPUOFF        0x0010
#define MSP430_SR_GIE           0x0008
#define MSP430_SR_N             0x0004
#define MSP430_SR_Z             0x0002
#define MSP430_SR_C             0x0001

/* MSP430 instruction formats.
 *
 * NOARG is not an actual instruction format recognised by the CPU.
 * It is used only for emulated instructions.
 */
typedef enum {
	MSP430_ITYPE_NOARG,
	MSP430_ITYPE_JUMP,
	MSP430_ITYPE_DOUBLE,
	MSP430_ITYPE_SINGLE
} msp430_itype_t;

/* MSP430(X) data sizes.
 *
 * An address-word is a 20-bit value. When stored in memory, they are
 * stored as two 16-bit words in the following order:
 *
 *    data[15:0], {12'b0, data[19:16]}
 */
typedef enum {
	MSP430_DSIZE_WORD    = 0,
	MSP430_DSIZE_BYTE    = 1,
	MSP430_DSIZE_UNKNOWN = 2,
	MSP430_DSIZE_AWORD   = 3,
} msp430_dsize_t;

/* MSP430 operations.
 *
 * Some of these are emulated instructions. Emulated instructions are
 * alternate mnemonics for combinations of some real opcodes with
 * common operand values. For example, the following real instruction:
 *
 *    MOV   #0, R8
 *
 * can be written as the following emulated instruction:
 *
 *    CLR   R8
 */
typedef enum {
	/* Single operand */
	MSP430_OP_RRC           = 0x1000,
	MSP430_OP_SWPB          = 0x1080,
	MSP430_OP_RRA           = 0x1100,
	MSP430_OP_SXT           = 0x1180,
	MSP430_OP_PUSH          = 0x1200,
	MSP430_OP_CALL          = 0x1280,
	MSP430_OP_RETI          = 0x1300,

	/* Jump */
	MSP430_OP_JNZ           = 0x2000,
	MSP430_OP_JZ            = 0x2400,
	MSP430_OP_JNC           = 0x2800,
	MSP430_OP_JC            = 0x2C00,
	MSP430_OP_JN            = 0x3000,
	MSP430_OP_JGE           = 0x3400,
	MSP430_OP_JL            = 0x3800,
	MSP430_OP_JMP           = 0x3C00,

	/* Double operand */
	MSP430_OP_MOV           = 0x4000,
	MSP430_OP_ADD           = 0x5000,
	MSP430_OP_ADDC          = 0x6000,
	MSP430_OP_SUBC          = 0x7000,
	MSP430_OP_SUB           = 0x8000,
	MSP430_OP_CMP           = 0x9000,
	MSP430_OP_DADD          = 0xA000,
	MSP430_OP_BIT           = 0xB000,
	MSP430_OP_BIC           = 0xC000,
	MSP430_OP_BIS           = 0xD000,
	MSP430_OP_XOR           = 0xE000,
	MSP430_OP_AND           = 0xF000,

	/* Emulated instructions */
	MSP430_OP_ADC           = 0x10000,
	MSP430_OP_BR            = 0x10001,
	MSP430_OP_CLR           = 0x10002,
	MSP430_OP_CLRC          = 0x10003,
	MSP430_OP_CLRN          = 0x10004,
	MSP430_OP_CLRZ          = 0x10005,
	MSP430_OP_DADC          = 0x10006,
	MSP430_OP_DEC           = 0x10007,
	MSP430_OP_DECD          = 0x10008,
	MSP430_OP_DINT          = 0x10009,
	MSP430_OP_EINT          = 0x1000A,
	MSP430_OP_INC           = 0x1000B,
	MSP430_OP_INCD          = 0x1000C,
	MSP430_OP_INV           = 0x1000D,
	MSP430_OP_NOP           = 0x1000E,
	MSP430_OP_POP           = 0x1000F,
	MSP430_OP_RET           = 0x10010,
	MSP430_OP_RLA           = 0x10011,
	MSP430_OP_RLC           = 0x10012,
	MSP430_OP_SBC           = 0x10013,
	MSP430_OP_SETC          = 0x10014,
	MSP430_OP_SETN          = 0x10015,
	MSP430_OP_SETZ          = 0x10016,
	MSP430_OP_TST           = 0x10017,

	/* MSP430X single operand (extension word) */
	MSP430_OP_RRCX          = 0x21000,
	MSP430_OP_RRUX          = 0x21001, /* note: ZC = 1 */
	MSP430_OP_SWPBX         = 0x21080,
	MSP430_OP_RRAX          = 0x21100,
	MSP430_OP_SXTX          = 0x21180,
	MSP430_OP_PUSHX         = 0x21200,

	/* MSP430X double operand (extension word) */
	MSP430_OP_MOVX          = 0x24000,
	MSP430_OP_ADDX          = 0x25000,
	MSP430_OP_ADDCX         = 0x26000,
	MSP430_OP_SUBCX         = 0x27000,
	MSP430_OP_SUBX          = 0x28000,
	MSP430_OP_CMPX          = 0x29000,
	MSP430_OP_DADDX         = 0x2A000,
	MSP430_OP_BITX          = 0x2B000,
	MSP430_OP_BICX          = 0x2C000,
	MSP430_OP_BISX          = 0x2D000,
	MSP430_OP_XORX          = 0x2E000,
	MSP430_OP_ANDX          = 0x2F000,

	/* MSP430X group 13xx */
	MSP430_OP_CALLA         = 0x21300,

	/* MSP430X group 14xx */
	MSP430_OP_PUSHM         = 0x1400,
	MSP430_OP_POPM		= 0x1600,

	/* MSP430X address instructions */
	MSP430_OP_MOVA          = 0x0000,
	MSP430_OP_CMPA          = 0x0090,
	MSP430_OP_ADDA          = 0x00A0,
	MSP430_OP_SUBA          = 0x00B0,

	/* MSP430X group 00xx, non-address */
	MSP430_OP_RRCM		= 0x0040,
	MSP430_OP_RRAM		= 0x0140,
	MSP430_OP_RLAM		= 0x0240,
	MSP430_OP_RRUM		= 0x0340,

	/* MSP430X emulated instructions */
	MSP430_OP_ADCX		= 0x40000,
	MSP430_OP_BRA		= 0x40001,
	MSP430_OP_RETA		= 0x40002,
	MSP430_OP_CLRX		= 0x40003,
	MSP430_OP_DADCX         = 0x40004,
	MSP430_OP_DECX		= 0x40005,
	MSP430_OP_DECDA		= 0x40006,
	MSP430_OP_DECDX		= 0x40007,
	MSP430_OP_INCX		= 0x40008,
	MSP430_OP_INCDA		= 0x40009,
	MSP430_OP_INVX		= 0x4000A,
	MSP430_OP_RLAX		= 0x4000B,
	MSP430_OP_RLCX		= 0x4000C,
	MSP430_OP_SECX		= 0x4000D,
	MSP430_OP_TSTA		= 0x4000E,
	MSP430_OP_TSTX		= 0x4000F,
	MSP430_OP_POPX		= 0x40010,
	MSP430_OP_INCDX		= 0x40011,
} msp430_op_t;

/* This represents a decoded instruction. All decoded addresses are
 * absolute or register-indexed, depending on the addressing mode.
 *
 * For jump instructions, the target address is stored in dst_operand.
 */
struct msp430_instruction {
	address_t               offset;
	int                     len;

	msp430_op_t             op;
	msp430_itype_t          itype;
	msp430_dsize_t          dsize;

	msp430_amode_t          src_mode;
	address_t               src_addr;
	msp430_reg_t            src_reg;

	msp430_amode_t          dst_mode;
	address_t		dst_addr;
	msp430_reg_t            dst_reg;

	int			rep_index;
	int			rep_register;
};

/* Decode a single instruction.
 *
 * Returns the number of bytes consumed, or -1 if an error occured.
 *
 * The caller needs to pass a pointer to the bytes to be decoded, the
 * virtual offset of those bytes, and the maximum number available. If
 * successful, the decoded instruction is written into the structure
 * pointed to by insn.
 */
int dis_decode(const uint8_t *code,
	       address_t offset, address_t len,
	       struct msp430_instruction *insn);

/* Look up names for registers and opcodes */
int dis_opcode_from_name(const char *name);
const char *dis_opcode_name(msp430_op_t op);
int dis_reg_from_name(const char *name);
const char *dis_reg_name(msp430_reg_t reg);

#endif
