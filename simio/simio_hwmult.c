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

#include <stdlib.h>
#include <string.h>

#include "simio_device.h"
#include "simio_hwmult.h"
#include "output.h"
#include "expr.h"

/* Multiplier register addresses - taken from mspgcc */
#define MPY            0x0130  /* Multiply Unsigned/Operand 1 */
#define MPYS           0x0132  /* Multiply Signed/Operand 1 */
#define MAC            0x0134  /* Multiply Unsigned and Accumulate/Operand 1 */
#define MACS           0x0136  /* Multiply Signed and Accumulate/Operand 1 */
#define OP2            0x0138  /* Operand 2 */
#define RESLO          0x013A  /* Result Low Word */
#define RESHI          0x013C  /* Result High Word */
#define SUMEXT         0x013E  /* Sum Extend */

struct hwmult {
	struct simio_device		base;

	int				mode;

	uint16_t			op1;
	uint16_t			op2;
	uint32_t			result;
	uint16_t			sumext;
};

struct simio_device *hwmult_create(char **arg_text)
{
	struct hwmult *h = malloc(sizeof(*h));

	(void)arg_text;

	if (!h) {
		pr_error("hwmult: can't allocate memory");
		return NULL;
	}

	memset(h, 0, sizeof(*h));
	h->base.type = &simio_hwmult;

	return (struct simio_device *)h;
}

static void hwmult_destroy(struct simio_device *dev)
{
	free(dev);
}

static void do_multiply(struct hwmult *h)
{
	uint32_t im;
	uint64_t temp = 0;

	/* Multiply */
	if (h->mode & 2)
		im = (int16_t)h->op1 * (int16_t)h->op2;
	else
		im = h->op1 * h->op2;

	/* Accumulate or store */
	if (h->mode & 4)
	{
		temp = (uint64_t)h->result + im;
		h->result = temp;
	}
	else
		h->result = im;

	/* Set SUMEXT */
	if (h->mode & 2) /* MPYS and MACS */
		h->sumext = (h->result & 0x80000000) ? 0xffff : 0;
	else if(h->mode == MAC)
		h->sumext = temp >> 32;
	else /* MPY */
		h->sumext = 0;
}

static int hwmult_write(struct simio_device *dev, address_t addr, uint16_t data)
{
	struct hwmult *h = (struct hwmult *)dev;

	switch (addr) {
	case RESHI:
		h->result = (h->result & 0xffff) | ((uint32_t)data << 16);
		return 0;

	case RESLO:
		h->result = (h->result & 0xffff0000) | data;
		return 0;

	case OP2:
		h->op2 = data;
		do_multiply(h);
		return 0;

	case MPY:
	case MPYS:
	case MAC:
	case MACS:
		h->op1 = data;
		h->mode = addr;
		return 0;
	}

	return 1;
}

static int hwmult_read(struct simio_device *dev, address_t addr, uint16_t *data)
{
	struct hwmult *h = (struct hwmult *)dev;

	switch (addr) {
	case MPY:
	case MPYS:
	case MAC:
	case MACS:
		*data = h->op1;
		return 0;

	case OP2:
		*data = h->op2;
		return 0;

	case RESLO:
		*data = h->result & 0xffff;
		return 0;

	case RESHI:
		*data = h->result >> 16;
		return 0;

	case SUMEXT:
		*data = h->sumext;
		return 0;
	}

	return 1;
}

const struct simio_class simio_hwmult = {
	.name = "hwmult",
	.help =
"This module simulates the hardware multiplier.\n",
	.create			= hwmult_create,
	.destroy		= hwmult_destroy,
	.write			= hwmult_write,
	.read			= hwmult_read
};
