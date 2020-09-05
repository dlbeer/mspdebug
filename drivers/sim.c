/* MSPDebug - debugging tool for the eZ430
 * Copyright (C) 2009, 2010, 2020 Daniel Beer
 * Copyright (C) 2020 Bruce G. Burns
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "device.h"
#include "dis.h"
#include "util.h"
#include "output.h"
#include "sim.h"
#include "simio_cpu.h"
#include "ctrlc.h"

#define MEM_SIZE	(1<<17)

#define ADDR_BYTE_IO_END      0x100

#define SIMx	dev->base.type->name

struct sim_device {
	struct device           base;

	uint8_t                 memory[MEM_SIZE];
	uint32_t                regs[DEVICE_NUM_REGS];

	int                     running;
	uint32_t                current_insn;

	int			watchpoint_hit;

	int			cpux;

	uint32_t		addr_io_end;
};

#define WIDTH_UNDEFINED		0

static int mem_setb(struct sim_device *dev, uint32_t offset, uint8_t value);
static int mem_setw(struct sim_device *dev, uint32_t offset, uint16_t value);
static int mem_seta(struct sim_device *dev, uint32_t offset, uint32_t value);

static uint16_t mem_getw(struct sim_device *dev, uint32_t offset);
static uint32_t mem_geta(struct sim_device *dev, uint32_t offset);

static void add_to_pc(struct sim_device *dev, int16_t offset);

static int mem_setb(struct sim_device *dev, uint32_t offset, uint8_t value)
{
	if (offset >= MEM_SIZE) {
		printc_err("%s: write to nonexistent addr 0x%05x at PC = 0x%05x\n",
			SIMx,offset,dev->current_insn);
		return -1;
	}
	uint8_t *mem = dev->memory;
	mem[offset] = value;
	return 0;
}
static int mem_setw(struct sim_device *dev, uint32_t offset, uint16_t value)
{
	if (offset >= MEM_SIZE) {
		printc_err("%s: write to nonexistent addr 0x%05x at PC = 0x%05x\n",
			SIMx,offset,dev->current_insn);
		return -1;
	}
	uint8_t *mem = dev->memory;
	offset &= ~1;
	mem[offset + 0] = value;
	mem[offset + 1] = value >> 8;
	return 0;
}
static int mem_seta(struct sim_device *dev, uint32_t offset, uint32_t value)
{
	if (mem_setw(dev,offset,value) < 0) return -1;
	return mem_setw(dev,offset+2,(value >> 16) & 0xF);
}
static uint16_t mem_getw(struct sim_device *dev, uint32_t offset)
{
	offset &= ~1;
	if (offset >= MEM_SIZE) {
		printc_err("%s: read from nonexistent addr 0x%05x at PC = 0x%05x\n",
			SIMx,offset,dev->current_insn);
		return -1;
	}
	uint8_t *mem = dev->memory;
	return (mem[offset] | (mem[offset+1] << 8));
}
static uint32_t mem_geta(struct sim_device *dev, uint32_t offset)
{
	return mem_getw(dev,offset) | ((mem_getw(dev,offset+2) & 0xF) << 16);
}

static void add_to_pc(struct sim_device *dev, int16_t offset)
{
	uint32_t pc = (dev->regs[MSP430_REG_PC] + offset) & 0xFFFFF;
	if (!dev->cpux) pc &= 0x0FFFF;
	dev->regs[MSP430_REG_PC] = pc;
}

static int invalid_opcode(struct sim_device *dev)
{
	printc_err("%s: invalid opcode at PC = 0x%05x\n",
		SIMx, dev->current_insn);
	if (!dev->cpux)
		printc_err("perhaps you should use 'simx' instead of 'sim'?\n");
	return -1;
}

static void watchpoint_check(struct sim_device *dev, uint16_t addr,
			     int is_write)
{
	int i;

	for (i = 0; i < DEVICE_MAX_BREAKPOINTS; i++) {
		const struct device_breakpoint *bp =
			&dev->base.breakpoints[i];

		if ((bp->flags & DEVICE_BP_ENABLED) &&
		    (bp->addr == addr) &&
		    ((bp->type == DEVICE_BPTYPE_WATCH ||
		      (bp->type == DEVICE_BPTYPE_READ && !is_write) ||
		      (bp->type == DEVICE_BPTYPE_WRITE && is_write)))) {
			printc_dbg("Watchpoint %d triggered (0x%04x, %s)\n",
				   i, addr, is_write ? "WRITE" : "READ");
			dev->watchpoint_hit = 1;
			return;
		}
	}
}

static int fetch_operand(struct sim_device *dev,
			 int amode, int reg, int opwidth,
			 uint32_t *addr_ret, uint32_t *data_ret, int ext, int ext_imm)
{
	uint32_t addr = 0;
	uint32_t mask = (1 << opwidth) - 1;
	int is_20bit_imm = 0;

	switch (amode) {
	case MSP430_AMODE_REGISTER:
		if (reg == MSP430_REG_R3) {
			if (data_ret)
				*data_ret = 0;
			return 0;
		}
		if (data_ret)
			*data_ret = dev->regs[reg] & mask;
		return 0;

	case MSP430_AMODE_INDEXED:
		if (reg == MSP430_REG_R3) {
			if (data_ret)
				*data_ret = 1;
			return 0;
		}

		addr = mem_getw(dev, dev->regs[MSP430_REG_PC]);

		if (ext)
			addr |= ext_imm << 16;

		else if (addr & 0x8000)
			addr |= 0xF0000;

		if (reg != MSP430_REG_SR)
			addr += dev->regs[reg];

		if (!ext && (dev->regs[reg] & 0xF0000) == 0)
			addr &= 0x0FFFF;
		else
			addr &= 0xFFFFF;

		add_to_pc(dev,2);
		break;

	case MSP430_AMODE_INDIRECT:
		if (reg == MSP430_REG_SR) {
			if (data_ret)
				*data_ret = 4;
			return 0;
		}

		if (reg == MSP430_REG_R3) {
			if (data_ret)
				*data_ret = 2;
			return 0;
		}
		addr = dev->regs[reg];
		break;

	case MSP430_AMODE_INDIRECT_INC:
		if (reg == MSP430_REG_PC && opwidth == 20) {
			is_20bit_imm = 1;
		}
		if (reg == MSP430_REG_SR) {
			if (data_ret)
				*data_ret = 8;
			return 0;
		}
		if (reg == MSP430_REG_R3) {
			if (data_ret)
				*data_ret = mask;
			return 0;
		}
		addr = dev->regs[reg];
		dev->regs[reg] +=
			(reg == MSP430_REG_PC) ? 2
			: (opwidth == 20) ? 4
			: (reg == MSP430_REG_SP) ? 2
			: (opwidth == 16) ? 2
			: 1;
		break;
	}

	if (addr_ret)
		*addr_ret = addr;

	int ret = 0;

	if (data_ret) {
		watchpoint_check(dev, addr, 0);

		if (addr < dev->addr_io_end) {

			if (opwidth == 8) {
				uint8_t byte;
				ret = simio_read_b(addr, &byte);
				*data_ret = byte;
			} else {
				uint16_t lsw;

				ret = simio_read(addr, &lsw);

				if (ret != 0) return ret;

				if (opwidth == 20) {
					uint16_t msw;
					ret = simio_read(addr+2, &msw);
					*data_ret = ((msw << 16) | lsw) & 0xFFFFF;
				} else {
					*data_ret = lsw;
				}
			}

		} else if (opwidth != 20 || is_20bit_imm) {
			uint16_t wd = mem_getw(dev, addr);
			if (opwidth == 8 && (addr & 1))
				wd >>= 8;
			*data_ret = (wd | (ext_imm << 16)) & mask;
		} else {
			*data_ret = mem_geta(dev,addr) & mask;
		}
	}
	return ret;
}

static int store_operand(struct sim_device *dev,
			 int amode, int reg, int opwidth,
			 uint16_t addr, uint32_t data)
{

	if (amode == MSP430_AMODE_REGISTER) {
		uint32_t mask = ((1 << opwidth) - 1);
		dev->regs[reg] = mask & data;
		return 0;
	}

	watchpoint_check(dev, addr, 1);

	int ret = 0;

	if (opwidth == 8)
		ret = mem_setb(dev, addr, data);

	else if (opwidth == 20)
		ret = mem_seta(dev, addr, data);

	else
		ret = mem_setw(dev, addr, data);

	if (ret != 0) return ret;

	if (addr < dev->addr_io_end) {
		if (opwidth == 8)
			return simio_write_b(addr, data);

		int ret = simio_write(addr, data);

		if (ret != 0 || opwidth != 20) return ret;

		return simio_write(addr + 2, data >> 16);
	}

	return 0;
}

#define ARITH_BITS (MSP430_SR_V | MSP430_SR_N | MSP430_SR_Z | MSP430_SR_C)

static int determine_op_width(uint16_t ins, uint16_t ext)
{
	uint16_t opcode = ins & 0xff80;

	/* handle inconsistent SXTX and SWPBX encoding */
	if (ext && (opcode == MSP430_OP_SWPB || opcode == MSP430_OP_SXT))
		return (ins & 0x40) ? WIDTH_UNDEFINED : (ext & 0x40) ? 16 : 20;

	else if (!ext || (ext & 0x0040))
		return (ins & 0x0040) ? 8 : 16;

	else
		return (ins & 0x0040) ? 20 : WIDTH_UNDEFINED;
}

static int step_double(struct sim_device *dev, uint16_t ins, uint16_t ext)
{
	uint16_t opcode = ins & 0xf000;
	int sreg = (ins >> 8) & 0xf;
	int amode_dst = (ins >> 7) & 1;
	int amode_src = (ins >> 4) & 0x3;
	int dreg = ins & 0x000f;
	uint32_t src_data;
	uint32_t dst_addr = 0;
	uint32_t dst_data;
	uint32_t res_data = 0;
	uint32_t shiftMask = 0x000f;
	uint32_t i = 0;
	int cycles;
	int rept = 1;
	uint16_t zc_sr_mask = ~0;

	int opwidth = determine_op_width(ins,ext);
	if (opwidth == WIDTH_UNDEFINED) {
		printc_err("%s: invalid op width encoding at PC = 0x%04x\n",
			SIMx,dev->current_insn);
		return -1;
	}
	uint32_t mask = (1 << opwidth) - 1;
	uint32_t msb = 1 << (opwidth - 1);

	int ext_src_bits = (ext >> 7) & 0xF;
	int ext_dst_bits = (ext >> 0) & 0xF;

	if (ext && amode_src == MSP430_AMODE_REGISTER
			&& amode_dst == MSP430_AMODE_REGISTER) {
		/* certain ext features only supported on reg-reg ops */
		if (ext & (1<<7))
			rept = (dev->regs[ext_dst_bits] & 0xF) + 1;
		else
			rept = ext_dst_bits + 1;
		if (ext & 0x0100) 
			zc_sr_mask = ~MSP430_SR_C;
	}

	if (!dev->cpux) { /* original CPU timing */

		if (amode_dst == MSP430_AMODE_REGISTER && dreg == MSP430_REG_PC) {
			if (amode_src == MSP430_AMODE_REGISTER ||
			    amode_src == MSP430_AMODE_INDIRECT)
				cycles = 2;
			else
				cycles = 3;
		} else if (sreg == MSP430_REG_SR || sreg == MSP430_REG_R3) {
			if (amode_dst == MSP430_AMODE_REGISTER)
				cycles = 1;
			else
				cycles = 4;
		} else {
			if (amode_src == MSP430_AMODE_INDIRECT ||
			    amode_src == MSP430_AMODE_INDIRECT_INC)
				cycles = 2;
			else if (amode_src == MSP430_AMODE_INDEXED)
				cycles = 3;
			else
				cycles = 1;

			if (amode_dst == MSP430_AMODE_INDEXED)
				cycles += 3;
		}

	} else { /* CPUX timing */
		cycles = 1;					/* read opcode */
		if (ext) cycles += 1;		/* read ext wd */

		if (amode_src == MSP430_AMODE_INDEXED)
			cycles += 1;			/* read offset */

		if (amode_src != MSP430_AMODE_REGISTER) {
			cycles += 1;			/* read src value */
			if (opwidth > 16 && (sreg != MSP430_REG_PC || amode_src != MSP430_AMODE_INDIRECT_INC))
				cycles += 1;		/* read src value high bits */
		}
		if (amode_dst == MSP430_AMODE_INDEXED) {
			cycles += 1;			/* read offset; */
			if (opcode != MSP430_OP_MOV) {
				cycles += 1;		/* read dst value */
				if (opwidth > 16)
					cycles += 1;	/* read dst value high bits */
			}
			if (opcode != MSP430_OP_BIT && opcode != MSP430_OP_CMP) {
				cycles += 1;		/* write dst value */
				if (opwidth > 16)
					cycles += 1;	/* write dst value high bits */
			}
		} else if (dreg == MSP430_REG_PC) {
			if (opcode != MSP430_OP_MOV
					&& opcode != MSP430_OP_ADD
					&& opcode != MSP430_OP_SUB)
				cycles += 1;	/* pipelining hit */
			if (amode_src != MSP430_AMODE_INDIRECT_INC || sreg != MSP430_REG_PC)
				cycles += 1;	/* pipelining hit */
		}
		cycles += rept - 1;
	}

	if (fetch_operand(dev, amode_src, sreg, opwidth, NULL, &src_data, ext, ext_src_bits) < 0)
		return -1;
	if (fetch_operand(dev, amode_dst, dreg, opwidth, &dst_addr,
			  opcode == MSP430_OP_MOV ? NULL : &dst_data, ext, ext_dst_bits) < 0)
		return -1;

	while (rept--) {

		uint32_t src_save = src_data;

		switch (opcode) {
		case MSP430_OP_MOV:
			res_data = src_data;
			break;

		case MSP430_OP_SUB:
		case MSP430_OP_SUBC:
		case MSP430_OP_CMP:
			src_data ^= mask;
		case MSP430_OP_ADD:
		case MSP430_OP_ADDC:
			if (opcode == MSP430_OP_ADDC || opcode == MSP430_OP_SUBC)
				res_data = (dev->regs[MSP430_REG_SR] & zc_sr_mask &
					    MSP430_SR_C) ? 1 : 0;
			else if (opcode == MSP430_OP_SUB || opcode == MSP430_OP_CMP)
				res_data = 1;
			else
				res_data = 0;

			res_data += src_data;
			res_data += dst_data;

			dev->regs[MSP430_REG_SR] &= ~ARITH_BITS;
			if (!(res_data & mask))
				dev->regs[MSP430_REG_SR] |= MSP430_SR_Z;
			if (res_data & msb)
				dev->regs[MSP430_REG_SR] |= MSP430_SR_N;
			if (res_data & (msb << 1))
				dev->regs[MSP430_REG_SR] |= MSP430_SR_C;
			if ((src_data ^ dst_data ^
					res_data ^ (res_data>>1)) & msb)
				dev->regs[MSP430_REG_SR] |= MSP430_SR_V;
			break;

		case MSP430_OP_DADD:
			res_data = 0;
			if (dev->regs[MSP430_REG_SR] & zc_sr_mask & MSP430_SR_C)
				res_data++;
			shiftMask = 0x000f;
			for(i = 0; i < 5; ++i)
			{
				res_data += (src_data & shiftMask) + (dst_data & shiftMask);
				if( (res_data & (0x1f << (i*4))) > (9 << (i*4))) {
					res_data += 6 << (i*4);
					res_data |= (0x10 << (i*4));
					res_data &= ~(0x20 << (i*4));
				}
				shiftMask = shiftMask << 4;
			}

			dev->regs[MSP430_REG_SR] &= ~ARITH_BITS;
			if (!(res_data & mask))
				dev->regs[MSP430_REG_SR] |= MSP430_SR_Z;
			if (res_data & msb)
				dev->regs[MSP430_REG_SR] |= MSP430_SR_N;
			if (res_data & (msb << 1))
				dev->regs[MSP430_REG_SR] |= MSP430_SR_C;

			/* V not specified for DADD, but FR5939 appears to match: */
			const int S = opwidth - 4;
			if (	(!((src_data^dst_data)&msb) && (
						((8<<S) <= res_data && res_data < (10<<S)) ||
						((22<<S) <= res_data && res_data < (24<<S))
						)) ||
					(src_data + dst_data >= (20<<S) && !(res_data & msb)) )
				dev->regs[MSP430_REG_SR] |= MSP430_SR_V;

			break;

		case MSP430_OP_BIT:
		case MSP430_OP_AND:
			res_data = src_data & dst_data;

			dev->regs[MSP430_REG_SR] &= ~ARITH_BITS;
			dev->regs[MSP430_REG_SR] |=
				(res_data & mask) ? MSP430_SR_C : MSP430_SR_Z;
			if (res_data & msb)
				dev->regs[MSP430_REG_SR] |= MSP430_SR_N;
			break;

		case MSP430_OP_BIC:
			res_data = dst_data & ~src_data;
			break;

		case MSP430_OP_BIS:
			res_data = dst_data | src_data;
			break;

		case MSP430_OP_XOR:
			res_data = dst_data ^ src_data;
			dev->regs[MSP430_REG_SR] &= ~ARITH_BITS;
			dev->regs[MSP430_REG_SR] |=
				(res_data & mask) ? MSP430_SR_C : MSP430_SR_Z;
			if (res_data & msb)
				dev->regs[MSP430_REG_SR] |= MSP430_SR_N;
			if (src_data & dst_data & msb)
				dev->regs[MSP430_REG_SR] |= MSP430_SR_V;
			break;

		default:
			return invalid_opcode(dev);
		}

		/* no need to repeat ops that will yeild same result every time */
		if (opcode == MSP430_OP_CMP || opcode == MSP430_OP_BIT
			|| opcode == MSP430_OP_BIS || opcode == MSP430_OP_BIC
			|| opcode == MSP430_OP_AND)
			break;

		src_data = src_save;
		dst_data = res_data & mask;
		if (dreg == sreg)
			src_data = res_data & mask;
	}

	if (opcode != MSP430_OP_CMP && opcode != MSP430_OP_BIT &&
		store_operand(dev, amode_dst, dreg, opwidth,
			      dst_addr, res_data) < 0)
		return -1;

	return cycles;
}

static int step_single(struct sim_device *dev, uint16_t ins, uint16_t ext)
{
	uint16_t opcode = ins & 0xff80;
	int amode = (ins >> 4) & 0x3;
	int reg = ins & 0x000f;
	uint32_t src_addr = 0;
	uint32_t src_data;
	uint32_t res_data = 0;
	int cycles = 1;
	int rept = 1;
	uint16_t zc_sr_mask = ~0;
	int store_results = 1;

	int opwidth = determine_op_width(ins,ext);
	if (opwidth == WIDTH_UNDEFINED)
		return invalid_opcode(dev);

	uint32_t mask = (1 << opwidth) - 1;
	uint32_t msb = 1 << (opwidth - 1);

	int ext_dst_bits = (ext >> 0) & 0xF;

	if (ext && amode == MSP430_AMODE_REGISTER) {
		/* certain ext features only supported on reg ops */
		if (ext & (1<<7))
			rept = (dev->regs[ext_dst_bits] & 0xF) + 1;
		else
			rept = ext_dst_bits + 1;
		if (ext & 0x0100) 
			zc_sr_mask = ~MSP430_SR_C;
	}

	if (!dev->cpux) { /* original CPU timing */

		switch (opcode) {
		case MSP430_OP_PUSH:
			if (amode == MSP430_AMODE_REGISTER)
				cycles = 3;
			else if (amode == MSP430_AMODE_INDIRECT ||
				 (amode == MSP430_AMODE_INDIRECT_INC &&
				  reg == MSP430_REG_PC))
				cycles = 4;
			else
				cycles = 5;
			break;
		case MSP430_OP_CALL:
			if (amode == MSP430_AMODE_REGISTER ||
				amode == MSP430_AMODE_INDIRECT)
				cycles = 4;
			else
				cycles = 5;
			break;
		case MSP430_OP_RETI:
			cycles = 5;
			break;
		default:
			if (amode == MSP430_AMODE_INDEXED)
				cycles = 4;
			else if (amode == MSP430_AMODE_REGISTER)
				cycles = 1;
			else
				cycles = 3;
			break;
		}

	} else { /* CPUX timing */
		cycles = 1;					/* read opcode */
		if (ext) cycles += 1;		/* read ext wd */

		if (amode == MSP430_AMODE_INDEXED)
			cycles += 1;			/* read offset */

		switch (opcode) {			/* special-case opcodes */

		case MSP430_OP_CALL:
			if (amode == MSP430_AMODE_INDEXED && reg == MSP430_REG_SR)
				cycles += 1;		/* extra cycle for call &xxx */
			/* fall through */

		case MSP430_OP_PUSH:
			if (amode == MSP430_AMODE_REGISTER)
				cycles += 1;		/* sp decr pipeline hit */
			else {
				cycles += 1;		/* read data */
				if (opwidth > 16 &&
						!(amode == MSP430_AMODE_INDIRECT_INC &&
						reg == MSP430_REG_PC))
					cycles += 1;	/* read high wd, except if immediate */
			}
			cycles += 1;		/* write to stack */
			if (opwidth > 16 || opcode == MSP430_OP_CALL)
				cycles += 1;	/* write high bits to dest or stack */

			/* to match observed MSP430FR5739 behavior requires the following
					additional fudge */
			if (opwidth == 20 && amode == MSP430_AMODE_INDEXED)
				cycles += 1;	/* reason unknown */

			if (opwidth > 16)
				cycles += rept - 1;

			break;

		default:
			if (amode != MSP430_AMODE_REGISTER) {
				cycles += 2;			/* read/write data */
				if (opwidth > 16)
					cycles += 2;		/* extra read/write cycles */
			}
			break;
		}
		cycles += rept - 1;
	}

	if (fetch_operand(dev, amode, reg, opwidth, &src_addr, &src_data,
			ext, ext_dst_bits) < 0)
		return -1;

	while (rept--) {
		switch (opcode) {
		case MSP430_OP_RRC:
		case MSP430_OP_RRA:
			res_data = (src_data >> 1) & ~msb;
			if (opcode == MSP430_OP_RRC) {
				if (dev->regs[MSP430_REG_SR] & zc_sr_mask & MSP430_SR_C)
					res_data |= msb;
			} else {
				res_data |= src_data & msb;
			}

			dev->regs[MSP430_REG_SR] &= ~ARITH_BITS;
			if (!(res_data & mask))
				dev->regs[MSP430_REG_SR] |= MSP430_SR_Z;
			if (res_data & msb)
				dev->regs[MSP430_REG_SR] |= MSP430_SR_N;
			if (src_data & 1)
				dev->regs[MSP430_REG_SR] |= MSP430_SR_C;
			break;

		case MSP430_OP_SWPB:
			res_data = ((src_data & 0xff) << 8) | ((src_data >> 8) & 0xff);
			if (opwidth == 20)
				res_data |= src_data & 0xF0000;
			break;

		case MSP430_OP_SXT:
			dev->regs[MSP430_REG_SR] &= ~ARITH_BITS;

			/* Although not documented by TI, the FR5739 extends from
			   bit 15 rather than from bit 7 if the ZC bit of the extended
			   opcode word is set.  This is implemented here.  */

			uint32_t signbit = ((ext & 0x0100) ? 0x08000 : 0x00080);

			res_data = src_data & (signbit - 1);

			if (src_data & signbit) {
				res_data |= ((1<<20) - signbit);
				dev->regs[MSP430_REG_SR] |= MSP430_SR_N;
			}

			dev->regs[MSP430_REG_SR] |=
				res_data ? MSP430_SR_C : MSP430_SR_Z;

			if (amode == MSP430_AMODE_REGISTER && dev->cpux)
				opwidth = 20;	/* store all bits for reg dst */
			break;

		case MSP430_OP_PUSH:
			res_data = src_data; // in case of repeat

			dev->regs[MSP430_REG_SP] -= opwidth <= 16 ? 2 : 4;

			if (opwidth == 8)
				src_data |= mem_getw(dev, dev->regs[MSP430_REG_SP]) & 0xFF00;

			if (((opwidth <= 16)
				? mem_setw(dev, dev->regs[MSP430_REG_SP], src_data)
				: mem_seta(dev, dev->regs[MSP430_REG_SP], src_data)) < 0)
				return -1;

			store_results = 0;
			break;

		case MSP430_OP_CALL:
			dev->regs[MSP430_REG_SP] -= 2;
			if (mem_setw(dev, dev->regs[MSP430_REG_SP],
				 dev->regs[MSP430_REG_PC]) < 0)
				 return -1;
			dev->regs[MSP430_REG_PC] = src_data & 0xFFFF;
			store_results = 0;
			break;

		case MSP430_OP_RETI:
			/* handled in step_reti_calla() for CPUX */

			{
			dev->regs[MSP430_REG_SR] = 
				mem_getw(dev, dev->regs[MSP430_REG_SP]) & 0x0FFF;
			dev->regs[MSP430_REG_SP] += 2;
			dev->regs[MSP430_REG_PC] =
				mem_getw(dev, dev->regs[MSP430_REG_SP]);
			dev->regs[MSP430_REG_SP] += 2;
			store_results = 0;
			}
			break;

		default:
			return invalid_opcode(dev);
		}
		src_data = res_data;
	}

	if (store_results &&
			store_operand(dev, amode, reg, opwidth, src_addr, res_data) < 0)
		return -1;

	return cycles;
}

static int step_jump(struct sim_device *dev, uint16_t ins)
{
	uint16_t opcode = ins & 0xfc00;
	int32_t pc_offset = (((ins + 0x200) & 0x03ff) - 0x200) << 1;
	uint16_t sr = dev->regs[MSP430_REG_SR];

	switch (opcode) {
	case MSP430_OP_JNZ:
		sr = !(sr & MSP430_SR_Z);
		break;

	case MSP430_OP_JZ:
		sr &= MSP430_SR_Z;
		break;

	case MSP430_OP_JNC:
		sr = !(sr & MSP430_SR_C);
		break;

	case MSP430_OP_JC:
		sr &= MSP430_SR_C;
		break;

	case MSP430_OP_JN:
		sr &= MSP430_SR_N;
		break;

	case MSP430_OP_JGE:
		sr = ((sr & MSP430_SR_N) ? 1 : 0) ==
			((sr & MSP430_SR_V) ? 1 : 0);
		break;

	case MSP430_OP_JL:
		sr = ((sr & MSP430_SR_N) ? 1 : 0) !=
			((sr & MSP430_SR_V) ? 1 : 0);
		break;

	case MSP430_OP_JMP:
		sr = 1;
		break;
	}

	if (sr) {
		add_to_pc(dev,pc_offset);
	}

	return 2;
}

static int step_RxxM(struct sim_device *dev, uint16_t ins)
{
	/* RxxM instruction */
	// XXX TBD

	uint16_t dreg = ((ins >>  0) & 0xF);
	uint16_t rept = ((ins >> 10) & 0x3) + 1;

	int cycles = rept;

	int opwidth = (ins & 0x10) ? 16 : 20;
	uint32_t mask = (1 << opwidth) - 1;
	uint32_t msb = 1 << (opwidth - 1);

	uint32_t src_data = dev->regs[dreg] & mask;
	uint32_t res_data = 0;

	uint32_t cy = dev->regs[MSP430_REG_SR] & MSP430_SR_C;
	uint32_t oflo = 0;


	while (rept--) {

		switch (ins & 0x03e0) {
			case MSP430_OP_RRCM:
				res_data = (src_data >> 1);
				if (cy) res_data |= msb;
				cy = src_data & 1;
				break;
			case MSP430_OP_RRAM:
				res_data = (src_data >> 1) | (src_data & msb);
				cy = src_data & 1;
				break;
			case MSP430_OP_RRUM:
				res_data = (src_data >> 1);
				cy = src_data & 1;
				break;
			case MSP430_OP_RLAM:
				res_data = src_data << 1;
				cy = src_data & msb;
				oflo = (src_data ^ res_data) & msb;
				break;
			default:
				return invalid_opcode(dev);
		}
		src_data = res_data; /* for next iteration, if any */
	}

	dev->regs[dreg] = res_data & mask;

	dev->regs[MSP430_REG_SR] &= ~ARITH_BITS;

	if (cy)
		dev->regs[MSP430_REG_SR] |= MSP430_SR_C;
	if (!res_data)
		dev->regs[MSP430_REG_SR] |= MSP430_SR_Z;
	if (res_data & msb)
		dev->regs[MSP430_REG_SR] |= MSP430_SR_N;
	if (oflo)
		dev->regs[MSP430_REG_SR] |= MSP430_SR_V;

	/* TI docs say V flag is "undefined" for RLAM, but appears to be same as RLA of final repetition */

	return cycles;
}

struct addr_inst_info_s {
	uint16_t op;
	int src_amode;
	int dst_amode;
	int cycles;
	int cycles_if_dst_pc;
	int words;
};
const struct addr_inst_info_s addr_inst_lut[] = {
	{MSP430_OP_MOVA, MSP430_AMODE_INDIRECT,    MSP430_AMODE_REGISTER, 3,5, 1, },
	{MSP430_OP_MOVA, MSP430_AMODE_INDIRECT_INC,MSP430_AMODE_REGISTER, 3,5, 1, },
	{MSP430_OP_MOVA, MSP430_AMODE_ABSOLUTE,    MSP430_AMODE_REGISTER, 4,6, 2, },
	{MSP430_OP_MOVA, MSP430_AMODE_INDEXED,     MSP430_AMODE_REGISTER, 4,6, 2, },
	{0,0,0,0,0,0, },
	{0,0,0,0,0,0, },
	{MSP430_OP_MOVA, MSP430_AMODE_REGISTER,    MSP430_AMODE_ABSOLUTE, 4,4, 2, },
	{MSP430_OP_MOVA, MSP430_AMODE_REGISTER,    MSP430_AMODE_INDEXED,  4,4, 2, },
	{MSP430_OP_MOVA, MSP430_AMODE_IMMEDIATE,   MSP430_AMODE_REGISTER, 2,3, 2, },
	{MSP430_OP_CMPA, MSP430_AMODE_IMMEDIATE,   MSP430_AMODE_REGISTER, 2,3, 2, }, // note 1
	{MSP430_OP_ADDA, MSP430_AMODE_IMMEDIATE,   MSP430_AMODE_REGISTER, 2,3, 2, }, // note 1
	{MSP430_OP_SUBA, MSP430_AMODE_IMMEDIATE,   MSP430_AMODE_REGISTER, 2,3, 2, }, // note 1
	{MSP430_OP_MOVA, MSP430_AMODE_REGISTER,    MSP430_AMODE_REGISTER, 1,3, 1, },
	{MSP430_OP_CMPA, MSP430_AMODE_REGISTER,    MSP430_AMODE_REGISTER, 1,3, 1, },
	{MSP430_OP_ADDA, MSP430_AMODE_REGISTER,    MSP430_AMODE_REGISTER, 1,3, 1, },
	{MSP430_OP_SUBA, MSP430_AMODE_REGISTER,    MSP430_AMODE_REGISTER, 1,3, 1, },
};

/* note 1: the CPUX docs say 3 cycles for non-PC dest, but the FR5739 executes this
*	in two cycles, and so that value is used here.
*/

static int step_0xxx_addr(struct sim_device *dev, uint16_t ins)
{
	/* MSP430_OP_MOVA, MSP430_OP_CMPA, MSP430_OP_ADDA, MSP430_OP_SUBA */

	const struct addr_inst_info_s *info = &addr_inst_lut[(ins & 0x00F0) >> 4];

	if (!info->words)
		return invalid_opcode(dev);

	int src = (ins & 0x0F00) >> 8;
	int dst = (ins & 0x000F) >> 0;

	const uint32_t mask = 0xFFFFF;
	const uint32_t msb  = 0x80000;

	uint32_t dst_addr = 0;

	uint16_t word2 = 0;
	if (info->words > 1) {
		 word2 = mem_getw(dev, dev->regs[MSP430_REG_PC]);
		 add_to_pc(dev,2);
	}

	uint32_t src_data = 0;
	switch (info->src_amode) {

	case MSP430_AMODE_REGISTER:
		src_data = dev->regs[src];
		break;

	case MSP430_AMODE_IMMEDIATE:
		src_data = (src << 16) | word2;
		break;

	case MSP430_AMODE_INDIRECT:
		src_data = mem_geta(dev,dev->regs[src]);
		break;

	case MSP430_AMODE_INDIRECT_INC:
		src_data = mem_geta(dev,dev->regs[src]);
		dev->regs[src] += 4;
		dev->regs[src] &= mask;
		break;

	case MSP430_AMODE_INDEXED:
		src_data = mem_geta(dev,(dev->regs[src] + (int16_t)word2) & mask);
		break;

	case MSP430_AMODE_ABSOLUTE:
		src_data = mem_geta(dev,(src << 16) | word2);
		break;
	}

	uint32_t dst_data = 0;
	switch (info->dst_amode) {

	case MSP430_AMODE_ABSOLUTE:
		dst_addr = (dst << 16) | word2;
		goto load_dst_data;

	case MSP430_AMODE_INDEXED:
		dst_addr = (dev->regs[dst] + (int16_t)word2) & mask;
		goto load_dst_data;

	load_dst_data:
		if (info->op != MSP430_OP_MOVA) dst_data = mem_geta(dev,dst_addr);
		break;

	case MSP430_AMODE_REGISTER:
		dst_data = dev->regs[dst];
		break;
	}

	uint16_t status = dev->regs[MSP430_REG_SR];
	uint32_t res_data = 0;

	switch (info->op) {
	case MSP430_OP_MOVA:
		res_data = src_data;
		break;

	case MSP430_OP_SUBA:
	case MSP430_OP_CMPA:
		src_data = ((~src_data)+1) & mask;
	case MSP430_OP_ADDA:
		res_data = src_data + dst_data;
		status &= ~ARITH_BITS;
		if (!(res_data & mask))
			status |= MSP430_SR_Z;
		if (res_data & msb)
			status |= MSP430_SR_N;
		if (res_data & (msb << 1))
			status |= MSP430_SR_C;
		if ((src_data ^ dst_data ^
				res_data ^ (res_data>>1)) & msb)
		if (!((src_data ^ dst_data) & (src_data ^ res_data) & msb))
			status |= MSP430_SR_V;

		res_data &= mask;
		break;
	}

	dev->regs[MSP430_REG_SR] = status;

	/* store result if appropriate */
	if (info->op != MSP430_OP_CMPA) {
		switch (info->dst_amode) {
		case MSP430_AMODE_ABSOLUTE:
		case MSP430_AMODE_INDEXED:
			if (mem_seta(dev,dst_addr,res_data) < 0)
				return -1;
			break;

		case MSP430_AMODE_REGISTER:
			dev->regs[dst] = res_data;
			break;
		}
	}

	if (info->dst_amode == MSP430_AMODE_REGISTER && dst == MSP430_REG_PC)
		return info->cycles_if_dst_pc;

	return info->cycles;
}


static int step_pushm_popm(struct sim_device *dev, uint16_t ins)
{
	/* PUSHM/POPM */

	uint16_t opcode = ins & 0xfe00;
	int is_aword = ins & 0x0100;
	int reg = ins & 0x000f;
	int rept = ((ins >> 4) & 0xf) + 1;

	int cycles = 2 + (is_aword ? 2 : 1) * rept;

	switch (opcode) {

	case MSP430_OP_PUSHM:
		while (rept--) {
			dev->regs[MSP430_REG_SP] -= 2;
			if (mem_setw(dev, dev->regs[MSP430_REG_SP], dev->regs[reg--]) < 0)
				return -1;
		}
		break;

	case MSP430_OP_POPM:
		while (rept--) {
			dev->regs[reg++] = mem_getw(dev, dev->regs[MSP430_REG_SP]);
			dev->regs[MSP430_REG_SP] += 2;
		}
		break;

	default:
		return invalid_opcode(dev);
	};

	return cycles;
}

static int step_reti_calla(struct sim_device *dev, uint16_t ins)
{
	/* RETI, CALLA */

	int amode;
	int reg = 0;
	int ext_imm = 0;
	uint32_t data;
	int cycles = 0;

	switch ((ins & 0x00C0)>>6) {
		case 0:				/* RETI */
			/* note: RETI handled in step_single() for basic CPU */
			if (ins != MSP430_OP_RETI)
				return invalid_opcode(dev);

			uint16_t w1 = mem_getw(dev, dev->regs[MSP430_REG_SP]);
			dev->regs[MSP430_REG_SR] = w1 & 0x0FFF;
			dev->regs[MSP430_REG_SP] += 2;
			dev->regs[MSP430_REG_PC] =
				mem_getw(dev, dev->regs[MSP430_REG_SP]);
			dev->regs[MSP430_REG_PC] |= ((w1 & 0xF000) << 4);
			dev->regs[MSP430_REG_SP] += 2;
			cycles = 5;

		case 1:				/* CALLA Rd, x(Rd), @Rd, @Rd+ */
			amode = (ins & 0x30) >> 4;
			reg = (ins & 0xF);
			cycles = (amode & 2) ? 6 : 5;
			if (amode == 1 && reg == MSP430_REG_SP)
				cycles++;
			goto calla_common;

		case 2:				/* CALLA &abs20, rel20, #imm20 */
			if ((ins & 0x30) == 0x20)
				return invalid_opcode(dev);

			amode = ((ins & 0x30) >> 4) | 1;
			ext_imm = (ins & 0xF);
			cycles = (amode & 2) ? 5 : 7;
			goto calla_common;

		calla_common:

			if (fetch_operand(dev,amode,reg,20,NULL,&data,1,ext_imm) < 0)
				return -1;

			dev->regs[MSP430_REG_SP] -= 4;
			if (mem_setw(dev, dev->regs[MSP430_REG_SP] + 2,
				 (dev->regs[MSP430_REG_PC] >> 16) & 0x0000F) < 0)
				 return -1;
			if (mem_setw(dev, dev->regs[MSP430_REG_SP],
				 dev->regs[MSP430_REG_PC]) < 0)
				 return -1;
			dev->regs[MSP430_REG_PC] = data;
			break;

		case 3:				/* RESERVED */
			return invalid_opcode(dev);
	}
	return cycles;
}

/* Fetch and execute one instruction. Return the number of CPU cycles
 * it would have taken, or -1 if an error occurs.
 */
static int step_cpu(struct sim_device *dev)
{
	uint16_t ins;
	int ret;

	const char *where = NULL;
	if (dev->regs[MSP430_REG_PC] < dev->addr_io_end)
		where = "in device space";
	else if (dev->regs[MSP430_REG_PC] >= MEM_SIZE)
		where = "beyond end of memory";
	if (where) {
		/* report bogus PC, provide previous location */
		printc_err("%s: executing %s: PC = 0x%05x; "
			"previous PC value 0x%05x\n",
			SIMx,where,dev->regs[MSP430_REG_PC],dev->current_insn);
		return -1;
	}

	/* Fetch the instruction */
	dev->current_insn = dev->regs[MSP430_REG_PC];

	ins = mem_getw(dev, dev->current_insn);
	add_to_pc(dev,2);

	/* Handle different instruction types */
	if ((ins & 0xf800) == 0x1800 && dev->cpux) {

		/* found extension word */
		uint16_t ext = ins;
		ins = mem_getw(dev, dev->current_insn + 2);
		add_to_pc(dev,2);

		if ((ins & 0xf000) >= 0x4000)
			ret = step_double(dev, ins, ext);
		else if ((ins & 0xf000) == 0x1000 && (ins & 0xfc00) < 0x1280)
			ret = step_single(dev, ins, ext);
		else
			ret = invalid_opcode(dev);

	} else {
		if ((ins & 0xf0e0) == 0x0040 && dev->cpux)
			ret = step_RxxM(dev, ins);
		else if ((ins & 0xf000) == 0x0000 && dev->cpux)
			ret = step_0xxx_addr(dev, ins);
		else if ((ins & 0xfc00) == 0x1400 && dev->cpux)
			ret = step_pushm_popm(dev, ins);
		else if ((ins & 0xff00) == 0x1300 && dev->cpux)
			ret = step_reti_calla(dev, ins);
		else if ((ins & 0xf000) == 0x1000)
			ret = step_single(dev, ins, 0);
		else if ((ins & 0xe000) == 0x2000)
			ret = step_jump(dev, ins);
		else if ((ins & 0xf000) >= 0x4000)
			ret = step_double(dev, ins, 0);
		else
			ret = invalid_opcode(dev);
	}

	/* If things went wrong, restart at the current instruction */
	if (ret < 0)
		dev->regs[MSP430_REG_PC] = dev->current_insn;

	return ret;
}

static void do_reset(struct sim_device *dev)
{
	simio_step(dev->regs[MSP430_REG_SR], 4);
	memset(dev->regs, 0, sizeof(dev->regs));
	dev->regs[MSP430_REG_PC] = mem_getw(dev, 0xfffe);
	dev->regs[MSP430_REG_SR] = 0;
	simio_reset();
}

static int step_system(struct sim_device *dev)
{
	int count = 1;
	int irq;
	uint16_t status = dev->regs[MSP430_REG_SR];

	irq = simio_check_interrupt();
	if (irq == 15) {
		do_reset(dev);
		return 0;
	} else if (((status & MSP430_SR_GIE) && irq >= 0) || irq >= 14) {
		if (irq >= 16) {
			printc_err("%s: invalid interrupt number: %d\n", SIMx, irq);
			return -1;
		}

		dev->regs[MSP430_REG_SP] -= 2;
		if (mem_setw(dev, dev->regs[MSP430_REG_SP],
			 dev->regs[MSP430_REG_PC]) < 0)
			 return -1;

		dev->regs[MSP430_REG_SP] -= 2;
		if (mem_setw(dev, dev->regs[MSP430_REG_SP],
			 dev->regs[MSP430_REG_SR]) < 0)
			 return -1;

		dev->regs[MSP430_REG_SR] &=
			~(MSP430_SR_GIE | MSP430_SR_CPUOFF);
		dev->regs[MSP430_REG_PC] = mem_getw(dev, 0xffe0 + irq * 2);

		simio_ack_interrupt(irq);
		count = 6;
	} else if (!(status & MSP430_SR_CPUOFF)) {
		count = step_cpu(dev);
		if (count < 0)
			return -1;
	}

	simio_step(status, count);
	return 0;
}

/************************************************************************
 * Device interface
 */

static void sim_destroy(device_t dev_base)
{
	free(dev_base);
}

static int sim_readmem(device_t dev_base, address_t addr,
		       uint8_t *mem, address_t len)
{
	struct sim_device *dev = (struct sim_device *)dev_base;

	if (addr > MEM_SIZE || (addr + len) < addr ||
	    (addr + len) > MEM_SIZE) {
		printc_err("%s: memory read out of range\n",SIMx);
		return -1;
	}

	if (addr + len > MEM_SIZE)
		len = MEM_SIZE - addr;

	/* Read byte IO addresses */
	while (len && (addr < ADDR_BYTE_IO_END)) {
		simio_read_b(addr, mem);
		mem++;
		len--;
		addr++;
	}

	/* Read word IO addresses */
	while (len >= 2 && addr < dev->addr_io_end) {
		uint16_t data = 0;

		simio_read(addr, &data);
		mem[0] = data & 0xff;
		mem[1] = data >> 8;
		mem += 2;
		len -= 2;
		addr += 2;
	}

	memcpy(mem, dev->memory + addr, len);
	return 0;
}

static int sim_writemem(device_t dev_base, address_t addr,
			const uint8_t *mem, address_t len)
{
	struct sim_device *dev = (struct sim_device *)dev_base;

	if (addr > MEM_SIZE || (addr + len) < addr ||
	    (addr + len) > MEM_SIZE) {
		printc_err("%s: memory write out of range\n",SIMx);
		return -1;
	}

	/* Write byte IO addresses */
	while (len && (addr < ADDR_BYTE_IO_END)) {
		simio_write_b(addr, *mem);
		mem++;
		len--;
		addr++;
	}

	/* Write word IO addresses */
	if (len == 1 && addr < dev->addr_io_end) {
		printc_err("%s: memory write on word IO, "
                   "at least 2 bytes data are necessary.\n",SIMx);
	} else if (len % 2 != 0 && addr < dev->addr_io_end) {
		printc_err("%s: memory write on word IO, "
                   "the last byte is ignored.\n",SIMx);
	}
	while (len >= 2 && addr < dev->addr_io_end) {
		simio_write(addr, ((uint16_t)mem[1] << 8) | mem[0]);
		mem += 2;
		len -= 2;
		addr += 2;
	}

	memcpy(dev->memory + addr, mem, len);
	return 0;
}

static int sim_getregs(device_t dev_base, address_t *regs)
{
	struct sim_device *dev = (struct sim_device *)dev_base;
	int i;

	for (i = 0; i < DEVICE_NUM_REGS; i++)
		regs[i] = dev->regs[i];
	return 0;
}

static int sim_setregs(device_t dev_base, const address_t *regs)
{
	struct sim_device *dev = (struct sim_device *)dev_base;
	int i;

	for (i = 0; i < DEVICE_NUM_REGS; i++)
		dev->regs[i] = regs[i];
	return 0;
}

static int sim_ctl(device_t dev_base, device_ctl_t op)
{
	struct sim_device *dev = (struct sim_device *)dev_base;

	switch (op) {
	case DEVICE_CTL_RESET:
		do_reset(dev);
		return 0;

	case DEVICE_CTL_HALT:
		dev->running = 0;
		return 0;

	case DEVICE_CTL_STEP:
		return step_system(dev);

	case DEVICE_CTL_RUN:
		dev->running = 1;
		return 0;

	default:
		printc_err("%s: unsupported operation\n",SIMx);
		return -1;
	}

	return 0;
}

static int sim_erase(device_t dev_base, device_erase_type_t type,
		     address_t addr)
{
	struct sim_device *dev = (struct sim_device *)dev_base;

	switch (type) {
	case DEVICE_ERASE_MAIN:
		memset(dev->memory + 0x2000, 0xff, MEM_SIZE - 0x2000);
		break;

	case DEVICE_ERASE_ALL:
		memset(dev->memory, 0xff, MEM_SIZE);
		break;

	case DEVICE_ERASE_SEGMENT:
		addr &= ~0x3f;
		addr &= (MEM_SIZE - 1);
		memset(dev->memory + addr, 0xff, 64);
		break;
	}

	return 0;
}

static device_status_t sim_poll(device_t dev_base)
{
	struct sim_device *dev = (struct sim_device *)dev_base;
	int count = 1000000;

	if (!dev->running)
		return DEVICE_STATUS_HALTED;

	dev->watchpoint_hit = 0;
	while (count > 0) {
		int i;

		for (i = 0; i < dev->base.max_breakpoints; i++) {
			struct device_breakpoint *bp =
				&dev->base.breakpoints[i];

			if ((bp->flags & DEVICE_BP_ENABLED) &&
			    (bp->type == DEVICE_BPTYPE_BREAK) &&
			    dev->regs[MSP430_REG_PC] == bp->addr) {
				dev->running = 0;
				return DEVICE_STATUS_HALTED;
			}
		}

		if (step_system(dev) < 0) {
			dev->running = 0;
			return DEVICE_STATUS_ERROR;
		}

		if (dev->watchpoint_hit) {
			dev->running = 0;
			return DEVICE_STATUS_HALTED;
		}

		if (ctrlc_check())
			return DEVICE_STATUS_INTR;

		count--;
	}

	return DEVICE_STATUS_RUNNING;
}

static device_t sim_open(const struct device_args *args)
{
	struct sim_device *dev = malloc(sizeof(*dev));

	(void)args;

	if (!dev) {
		pr_error("can't allocate memory for simulation");
		return NULL;
	}

	memset(dev, 0, sizeof(*dev));

	dev->base.type = &device_sim;
	dev->base.max_breakpoints = DEVICE_MAX_BREAKPOINTS;

	memset(dev->memory, 0xff, sizeof(dev->memory));
	memset(dev->regs, 0xff, sizeof(dev->regs));

	dev->running = 0;
	dev->current_insn = 0;

	dev->addr_io_end = 0x200;

	printc_dbg("Simulation started, 0x%x bytes of RAM\n", MEM_SIZE);
	return (device_t)dev;
}

static device_t simx_open(const struct device_args *args)
{
	struct sim_device *dev = (struct sim_device *)sim_open(args);
	dev->base.type = &device_simx;
	dev->cpux = 1;
	dev->addr_io_end = 0x1000;
	return (device_t)dev;
}

const struct device_class device_sim = {
	.name		= "sim",
	.help		= "Simulation mode (standard CPU)",
	.open		= sim_open,
	.destroy	= sim_destroy,
	.readmem	= sim_readmem,
	.writemem	= sim_writemem,
	.erase		= sim_erase,
	.getregs	= sim_getregs,
	.setregs	= sim_setregs,
	.ctl		= sim_ctl,
	.poll		= sim_poll,
	.getconfigfuses = NULL
};

const struct device_class device_simx = {
	.name		= "simx",
	.help		= "CPUX Simulation mode",
	.open		= simx_open,
	.destroy	= sim_destroy,
	.readmem	= sim_readmem,
	.writemem	= sim_writemem,
	.erase		= sim_erase,
	.getregs	= sim_getregs,
	.setregs	= sim_setregs,
	.ctl		= sim_ctl,
	.poll		= sim_poll,
	.getconfigfuses = NULL
};

