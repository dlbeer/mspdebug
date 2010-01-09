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
#include "device.h"

#define MEM_SIZE	65536

static u_int8_t *memory;

static void sim_close(void)
{
	if (memory) {
		free(memory);
		memory = NULL;
	}
}

static int sim_control(device_ctl_t action)
{
	switch (action) {
	case DEVICE_CTL_ERASE:
		memset(memory, 0xff, MEM_SIZE);
		return 0;

	case DEVICE_CTL_HALT:
		return 0;

	default:
		fprintf(stderr, "sim: CPU control is not implemented\n");
		break;
	}

	return -1;
}

static int sim_wait(void)
{
	return 0;
}

static int sim_breakpoint(u_int16_t addr)
{
	return 0;
}

static int sim_getregs(u_int16_t *regs)
{
	fprintf(stderr, "sim: register fetch is not implemented\n");
	return -1;
}

static int sim_setregs(const u_int16_t *regs)
{
	fprintf(stderr, "sim: register store is not implemented\n");
	return -1;
}

static int sim_readmem(u_int16_t addr, u_int8_t *mem, int len)
{
	if (addr + len > MEM_SIZE)
		len = MEM_SIZE - addr;

	memcpy(mem, memory + addr, len);
	return 0;
}

static int sim_writemem(u_int16_t addr, const u_int8_t *mem, int len)
{
	if (addr + len > MEM_SIZE)
		len = MEM_SIZE - addr;

	memcpy(memory + addr, mem, len);
	return 0;
}

static const struct device sim_device = {
	.close		= sim_close,
	.control	= sim_control,
	.wait		= sim_wait,
	.breakpoint	= sim_breakpoint,
	.getregs	= sim_getregs,
	.setregs	= sim_setregs,
	.readmem	= sim_readmem,
	.writemem	= sim_writemem
};

const struct device *sim_open(void)
{
	memory = malloc(MEM_SIZE);
	if (!memory) {
		perror("sim: can't allocate memory");
		return NULL;
	}

	printf("Simulation started, 0x%x bytes of RAM\n", MEM_SIZE);
	return &sim_device;
}
