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

#define MEM_SIZE	65536
#define MEM_IO_END      0x200

struct sim_device {
	struct device           base;

	sim_fetch_func_t        fetch_func;
	sim_store_func_t        store_func;
	void                    *user_data;

	uint8_t                 memory[MEM_SIZE];
	uint16_t                regs[DEVICE_NUM_REGS];

	int                     running;
	uint16_t                current_insn;
};

#define MEM_GETB(dev, offset) ((dev)->memory[offset])
#define MEM_SETB(dev, offset, value) ((dev)->memory[offset] = (value))
#define MEM_GETW(dev, offset)					\
	((dev)->memory[offset] |				\
	 ((dev)->memory[(offset + 1) & 0xffff] << 8))
#define MEM_SETW(dev, offset, value)					\
	do {								\
		(dev)->memory[offset] = (value) & 0xff;			\
		(dev)->memory[(offset + 1) & 0xffff] = (value) >> 8;	\
	} while (0);

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
		dev->regs[MSP430_REG_PC] += 2;

		if (reg != MSP430_REG_SR)
			addr += dev->regs[reg];
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
		dev->regs[reg] += 2;
		break;
	}

	if (addr_ret)
		*addr_ret = addr;

	if (data_ret) {
		*data_ret = MEM_GETW(dev, addr) & mask;

		if (addr < MEM_IO_END && dev->fetch_func) {
			uint16_t data16 = *data_ret;
			int ret;

			ret = dev->fetch_func(dev->user_data,
					      dev->current_insn,
					      addr, is_byte, &data16);
			*data_ret = data16;
			return ret;
		}
	}

	return 0;
}

static void store_operand(struct sim_device *dev,
			  int amode, int reg, int is_byte,
			  uint16_t addr, uint16_t data)
{
	if (is_byte)
		MEM_SETB(dev, addr, data);
	else
		MEM_SETW(dev, addr, data);

	if (amode == MSP430_AMODE_REGISTER)
		dev->regs[reg] = data;
	else if (addr < MEM_IO_END && dev->store_func)
		dev->store_func(dev->user_data, dev->current_insn,
				addr, is_byte, data);
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
		    (src_data ^ dst_data) & msb)
			dev->regs[MSP430_REG_SR] |= MSP430_SR_V;
		break;

	case MSP430_OP_DADD:
		res_data = src_data + dst_data;
		if (dev->regs[MSP430_REG_SR] & MSP430_SR_C)
			res_data++;

		dev->regs[MSP430_REG_SR] &= ~ARITH_BITS;
		if (!(res_data & mask))
			dev->regs[MSP430_REG_SR] |= MSP430_SR_Z;
		if (res_data == 1)
			dev->regs[MSP430_REG_SR] |= MSP430_SR_N;
		if ((is_byte && res_data > 99) ||
		    (!is_byte && res_data > 9999))
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

	if (opcode != MSP430_OP_CMP && opcode != MSP430_OP_BIT)
		store_operand(dev, amode_dst, dreg, is_byte,
			      dst_addr, res_data);

	return 0;
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

	if (fetch_operand(dev, amode, reg, is_byte, &src_addr, &src_data) < 0)
		return -1;

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
		break;

	case MSP430_OP_CALL:
		dev->regs[MSP430_REG_SP] -= 2;
		MEM_SETW(dev, dev->regs[MSP430_REG_SP],
			 dev->regs[MSP430_REG_PC]);
		dev->regs[MSP430_REG_PC] = src_data;
		break;

	case MSP430_OP_RETI:
		dev->regs[MSP430_REG_SR] =
			MEM_GETW(dev, dev->regs[MSP430_REG_SP]);
		dev->regs[MSP430_REG_SP] += 2;
		dev->regs[MSP430_REG_PC] =
			MEM_GETW(dev, dev->regs[MSP430_REG_SP]);
		dev->regs[MSP430_REG_SP] += 2;
		break;

	default:
		printc_err("sim: unknown single-operand opcode: 0x%04x "
			"(PC = 0x%04x)\n", opcode, dev->current_insn);
		return -1;
	}

	if (opcode != MSP430_OP_PUSH && opcode != MSP430_OP_CALL &&
	    opcode != MSP430_OP_RETI)
		store_operand(dev, amode, reg, is_byte, src_addr, res_data);

	return 0;
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

	return 0;
}

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
		memset(dev->regs, 0, sizeof(dev->regs));
		dev->regs[MSP430_REG_PC] = MEM_GETW(dev, 0xfffe);
		return 0;

	case DEVICE_CTL_HALT:
		dev->running = 0;
		return 0;

	case DEVICE_CTL_STEP:
		return step_cpu(dev);

	case DEVICE_CTL_RUN:
		dev->running = 1;
		return 0;
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

	ctrlc_reset();
	while (count > 0) {
		int i;

		for (i = 0; i < dev->base.max_breakpoints; i++) {
			struct device_breakpoint *bp =
				&dev->base.breakpoints[i];

			if ((bp->flags & DEVICE_BP_ENABLED) &&
			    dev->regs[MSP430_REG_PC] == bp->addr) {
				dev->running = 0;
				return DEVICE_STATUS_HALTED;
			}
		}

		if (dev->regs[MSP430_REG_SR] & MSP430_SR_CPUOFF) {
			printc("CPU disabled\n");
			dev->running = 0;
			return DEVICE_STATUS_HALTED;
		}

		if (step_cpu(dev) < 0) {
			dev->running = 0;
			return DEVICE_STATUS_ERROR;
		}

		if (ctrlc_check())
			return DEVICE_STATUS_INTR;

		count--;
	}

	return DEVICE_STATUS_RUNNING;
}

device_t sim_open(sim_fetch_func_t fetch_func,
		  sim_store_func_t store_func,
		  void *user_data)
{
	struct sim_device *dev = malloc(sizeof(*dev));

	if (!dev) {
		pr_error("can't allocate memory for simulation");
		return NULL;
	}

	memset(dev, 0, sizeof(*dev));

	dev->base.max_breakpoints = DEVICE_MAX_BREAKPOINTS;
	dev->base.destroy = sim_destroy;
	dev->base.readmem = sim_readmem;
	dev->base.writemem = sim_writemem;
	dev->base.erase = sim_erase;
	dev->base.getregs = sim_getregs;
	dev->base.setregs = sim_setregs;
	dev->base.ctl = sim_ctl;
	dev->base.poll = sim_poll;

	dev->fetch_func = fetch_func;
	dev->store_func = store_func;
	dev->user_data = user_data;

	memset(dev->memory, 0xff, sizeof(dev->memory));
	memset(dev->regs, 0xff, sizeof(dev->regs));

	dev->running = 0;
	dev->current_insn = 0;

	printc_dbg("Simulation started, 0x%x bytes of RAM\n", MEM_SIZE);
	return (device_t)dev;
}
