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

#define MEM_SIZE	65536
#define MEM_IO_END      0x200

struct sim_device {
	struct device           base;

	uint8_t                 memory[MEM_SIZE];
	uint16_t                regs[DEVICE_NUM_REGS];

	int                     running;
	uint16_t                current_insn;

	int			watchpoint_hit;
};

#define MEM_GETB(dev, offset) ((dev)->memory[offset])
#define MEM_SETB(dev, offset, value) ((dev)->memory[offset] = (value))
#define MEM_GETW(dev, offset)					\
	((dev)->memory[offset] |				\
	 ((dev)->memory[(offset + 1) & 0xffff] << 8))
#define MEM_SETW(dev, offset, value)					\
	do {								\
		(dev)->memory[offset & ~1] = (value) & 0xff;			\
		(dev)->memory[offset | 1] = (value) >> 8;	\
	} while (0);

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
			 int amode, int reg, int is_byte,
			 uint16_t *addr_ret, uint32_t *data_ret)
{
	uint16_t addr = 0;
	uint32_t mask = is_byte ? 0xff : 0xffff;

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

		addr = MEM_GETW(dev, dev->regs[MSP430_REG_PC]);

		if (reg != MSP430_REG_SR)
			addr += dev->regs[reg];

		dev->regs[MSP430_REG_PC] += 2;
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
		dev->regs[reg] += (is_byte && reg != MSP430_REG_PC &&
					      reg != MSP430_REG_SP) ? 1 : 2;
		break;
	}

	if (addr_ret)
		*addr_ret = addr;

	if (data_ret) {
		watchpoint_check(dev, addr, 0);

		*data_ret = MEM_GETW(dev, addr) & mask;

		if (addr < MEM_IO_END) {
			int ret;

			if (is_byte) {
				uint8_t x = *data_ret;

				ret = simio_read_b(addr, &x);
				*data_ret = x;
			} else {
				uint16_t x = *data_ret;

				ret = simio_read(addr, &x);
				*data_ret = x;
			}

			return ret;
		}
	}

	return 0;
}

static int store_operand(struct sim_device *dev,
			 int amode, int reg, int is_byte,
			 uint16_t addr, uint16_t data)
{
	if (amode == MSP430_AMODE_REGISTER) {
		dev->regs[reg] = is_byte ? data & 0xFF : data;
		return 0;
	}

	watchpoint_check(dev, addr, 1);

	if (is_byte)
		MEM_SETB(dev, addr, data);
	else
		MEM_SETW(dev, addr, data);

	if (addr < MEM_IO_END) {
		if (is_byte)
			return simio_write_b(addr, data);

		return simio_write(addr, data);
	}

	return 0;
}

#define ARITH_BITS (MSP430_SR_V | MSP430_SR_N | MSP430_SR_Z | MSP430_SR_C)

static int step_double(struct sim_device *dev, uint16_t ins)
{
	uint16_t opcode = ins & 0xf000;
	int sreg = (ins >> 8) & 0xf;
	int amode_dst = (ins >> 7) & 1;
	int is_byte = ins & 0x0040;
	int amode_src = (ins >> 4) & 0x3;
	int dreg = ins & 0x000f;
	uint32_t src_data;
	uint16_t dst_addr = 0;
	uint32_t dst_data;
	uint32_t res_data;
	uint32_t msb = is_byte ? 0x80 : 0x8000;
	uint32_t mask = is_byte ? 0xff : 0xffff;
	uint32_t shiftMask = 0x000f;
	uint32_t i = 0;
	int cycles;

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

	if (fetch_operand(dev, amode_src, sreg, is_byte, NULL, &src_data) < 0)
		return -1;
	if (fetch_operand(dev, amode_dst, dreg, is_byte, &dst_addr,
			  opcode == MSP430_OP_MOV ? NULL : &dst_data) < 0)
		return -1;

	switch (opcode) {
	case MSP430_OP_MOV:
		res_data = src_data;
		break;

	case MSP430_OP_SUB:
	case MSP430_OP_SUBC:
	case MSP430_OP_CMP:
		src_data = (~src_data) & mask;
	case MSP430_OP_ADD:
	case MSP430_OP_ADDC:
		if (opcode == MSP430_OP_ADDC || opcode == MSP430_OP_SUBC)
			res_data = (dev->regs[MSP430_REG_SR] &
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
		if (!((src_data ^ dst_data) & msb) &&
		    (src_data ^ res_data) & msb)
			dev->regs[MSP430_REG_SR] |= MSP430_SR_V;
		break;

	case MSP430_OP_DADD:
		res_data = 0;
		if (dev->regs[MSP430_REG_SR] & MSP430_SR_C)
			res_data++;
		shiftMask = 0x000f;
		for(i = 0; i < 4; ++i)
		{
			res_data += (src_data & shiftMask) + (dst_data & shiftMask);
			if( (res_data & (0x1f << (i*4))) > (9 << (i*4)))
				res_data += 6 << (i*4);
			shiftMask = shiftMask << 4;
		}

		dev->regs[MSP430_REG_SR] &= ~ARITH_BITS;
		if (!(res_data & mask))
			dev->regs[MSP430_REG_SR] |= MSP430_SR_Z;
		if (res_data & msb)
			dev->regs[MSP430_REG_SR] |= MSP430_SR_N;
		if (res_data & (msb << 1))
			dev->regs[MSP430_REG_SR] |= MSP430_SR_C;
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
		printc_err("sim: invalid double-operand opcode: "
			"0x%04x (PC = 0x%04x)\n",
			opcode, dev->current_insn);
		return -1;
	}

	if (opcode != MSP430_OP_CMP && opcode != MSP430_OP_BIT &&
		store_operand(dev, amode_dst, dreg, is_byte,
			      dst_addr, res_data) < 0)
		return -1;

	return cycles;
}

static int step_single(struct sim_device *dev, uint16_t ins)
{
	uint16_t opcode = ins & 0xff80;
	int is_byte = ins & 0x0040;
	int amode = (ins >> 4) & 0x3;
	int reg = ins & 0x000f;
	uint16_t msb = is_byte ? 0x80 : 0x8000;
	uint32_t mask = is_byte ? 0xff : 0xffff;
	uint16_t src_addr = 0;
	uint32_t src_data;
	uint32_t res_data = 0;
	int cycles = 1;

	if (fetch_operand(dev, amode, reg, is_byte, &src_addr, &src_data) < 0)
		return -1;

	if (amode == MSP430_AMODE_INDEXED)
		cycles = 4;
	else if (amode == MSP430_AMODE_REGISTER)
		cycles = 1;
	else
		cycles = 3;

	switch (opcode) {
	case MSP430_OP_RRC:
	case MSP430_OP_RRA:
		res_data = (src_data >> 1) & ~msb;
		if (opcode == MSP430_OP_RRC) {
			if (dev->regs[MSP430_REG_SR] & MSP430_SR_C)
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
		break;

	case MSP430_OP_SXT:
		res_data = src_data & 0xff;
		dev->regs[MSP430_REG_SR] &= ~ARITH_BITS;

		if (src_data & 0x80) {
			res_data |= 0xff00;
			dev->regs[MSP430_REG_SR] |= MSP430_SR_N;
		}

		dev->regs[MSP430_REG_SR] |=
			(res_data & mask) ? MSP430_SR_C : MSP430_SR_Z;
		break;

	case MSP430_OP_PUSH:
		dev->regs[MSP430_REG_SP] -= 2;
		MEM_SETW(dev, dev->regs[MSP430_REG_SP], src_data);

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
		dev->regs[MSP430_REG_SP] -= 2;
		MEM_SETW(dev, dev->regs[MSP430_REG_SP],
			 dev->regs[MSP430_REG_PC]);
		dev->regs[MSP430_REG_PC] = src_data;

		if (amode == MSP430_AMODE_REGISTER ||
		    amode == MSP430_AMODE_INDIRECT)
			cycles = 4;
		else
			cycles = 5;
		break;

	case MSP430_OP_RETI:
		dev->regs[MSP430_REG_SR] =
			MEM_GETW(dev, dev->regs[MSP430_REG_SP]);
		dev->regs[MSP430_REG_SP] += 2;
		dev->regs[MSP430_REG_PC] =
			MEM_GETW(dev, dev->regs[MSP430_REG_SP]);
		dev->regs[MSP430_REG_SP] += 2;
		cycles = 5;
		break;

	default:
		printc_err("sim: unknown single-operand opcode: 0x%04x "
			"(PC = 0x%04x)\n", opcode, dev->current_insn);
		return -1;
	}

	if (opcode != MSP430_OP_PUSH && opcode != MSP430_OP_CALL &&
	    opcode != MSP430_OP_RETI &&
		store_operand(dev, amode, reg, is_byte, src_addr, res_data) < 0)
		return -1;

	return cycles;
}

static int step_jump(struct sim_device *dev, uint16_t ins)
{
	uint16_t opcode = ins & 0xfc00;
	uint16_t pc_offset = (ins & 0x03ff) << 1;
	uint16_t sr = dev->regs[MSP430_REG_SR];

	if (pc_offset & 0x0400)
		pc_offset |= 0xff800;

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

	if (sr)
		dev->regs[MSP430_REG_PC] += pc_offset;

	return 2;
}

/* Fetch and execute one instruction. Return the number of CPU cycles
 * it would have taken, or -1 if an error occurs.
 */
static int step_cpu(struct sim_device *dev)
{
	uint16_t ins;
	int ret;

	/* Fetch the instruction */
	dev->current_insn = dev->regs[MSP430_REG_PC];
	ins = MEM_GETW(dev, dev->current_insn);
	dev->regs[MSP430_REG_PC] += 2;

	/* Handle different instruction types */
	if ((ins & 0xf000) >= 0x4000)
		ret = step_double(dev, ins);
	else if ((ins & 0xf000) >= 0x2000)
		ret = step_jump(dev, ins);
	else
		ret = step_single(dev, ins);

	/* If things went wrong, restart at the current instruction */
	if (ret < 0)
		dev->regs[MSP430_REG_PC] = dev->current_insn;

	return ret;
}

static void do_reset(struct sim_device *dev)
{
	simio_step(dev->regs[MSP430_REG_SR], 4);
	memset(dev->regs, 0, sizeof(dev->regs));
	dev->regs[MSP430_REG_PC] = MEM_GETW(dev, 0xfffe);
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
			printc_err("sim: invalid interrupt number: %d\n", irq);
			return -1;
		}

		dev->regs[MSP430_REG_SP] -= 2;
		MEM_SETW(dev, dev->regs[MSP430_REG_SP],
			 dev->regs[MSP430_REG_PC]);

		dev->regs[MSP430_REG_SP] -= 2;
		MEM_SETW(dev, dev->regs[MSP430_REG_SP],
			 dev->regs[MSP430_REG_SR]);

		dev->regs[MSP430_REG_SR] &=
			~(MSP430_SR_GIE | MSP430_SR_CPUOFF);
		dev->regs[MSP430_REG_PC] = MEM_GETW(dev, 0xffe0 + irq * 2);

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
		printc_err("sim: memory read out of range\n");
		return -1;
	}

	if (addr + len > MEM_SIZE)
		len = MEM_SIZE - addr;

	/* Read byte IO addresses */
	while (len && (addr < 0x100)) {
		simio_read_b(addr, mem);
		mem++;
		len--;
		addr++;
	}

	/* Read word IO addresses */
	while (len > 2 && (addr < 0x200)) {
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
		printc_err("sim: memory write out of range\n");
		return -1;
	}

	/* Write byte IO addresses */
	while (len && (addr < 0x100)) {
		simio_write_b(addr, *mem);
		mem++;
		len--;
		addr++;
	}

	/* Write word IO addresses */
	while (len > 2 && (addr < 0x200)) {
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
		printc_err("sim: unsupported operation\n");
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

	printc_dbg("Simulation started, 0x%x bytes of RAM\n", MEM_SIZE);
	return (device_t)dev;
}

const struct device_class device_sim = {
	.name		= "sim",
	.help		= "Simulation mode.",
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
