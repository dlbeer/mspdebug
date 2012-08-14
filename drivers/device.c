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

#include "output.h"
#include "device.h"

device_t device_default;

static int addbrk(device_t dev, address_t addr, device_bptype_t type)
{
	int i;
	int which = -1;
	struct device_breakpoint *bp;

	for (i = 0; i < dev->max_breakpoints; i++) {
		bp = &dev->breakpoints[i];

		if (bp->flags & DEVICE_BP_ENABLED) {
			if (bp->addr == addr && bp->type == type)
				return i;
		} else if (which < 0) {
			which = i;
		}
	}

	if (which < 0)
		return -1;

	bp = &dev->breakpoints[which];
	bp->flags = DEVICE_BP_ENABLED | DEVICE_BP_DIRTY;
	bp->addr = addr;
	bp->type = type;

	return which;
}

static void delbrk(device_t dev, address_t addr, device_bptype_t type)
{
	int i;

	for (i = 0; i < dev->max_breakpoints; i++) {
		struct device_breakpoint *bp = &dev->breakpoints[i];

		if ((bp->flags & DEVICE_BP_ENABLED) &&
		    bp->addr == addr && bp->type == type) {
			bp->flags = DEVICE_BP_DIRTY;
			bp->addr = 0;
		}
	}
}

int device_setbrk(device_t dev, int which, int enabled, address_t addr,
		  device_bptype_t type)
{
	if (which < 0) {
		if (enabled)
			return addbrk(dev, addr, type);

		delbrk(dev, addr, type);
	} else {
		struct device_breakpoint *bp = &dev->breakpoints[which];
		int new_flags = enabled ? DEVICE_BP_ENABLED : 0;

		if (!enabled)
			addr = 0;

		if (bp->addr != addr ||
		    (bp->flags & DEVICE_BP_ENABLED) != new_flags) {
			bp->flags = new_flags | DEVICE_BP_DIRTY;
			bp->addr = addr;
			bp->type = type;
		}
	}

	return 0;
}

int device_probe_id(device_t dev)
{
	uint8_t data[16];

	if (dev->type->readmem(dev, 0xff0, data, sizeof(data)) < 0) {
		printc_err("device_probe_id: read failed\n");
		return -1;
	}

	if (data[0] == 0x80) {
		if (dev->type->readmem(dev, 0x1a00, data, sizeof(data)) < 0) {
			printc_err("device_probe_id: read failed\n");
			return -1;
		}

		dev->dev_id[0] = data[4];
		dev->dev_id[1] = data[5];
		dev->dev_id[2] = data[6];
	} else {
		dev->dev_id[0] = data[0];
		dev->dev_id[1] = data[1];
		dev->dev_id[2] = data[13];
	}

	printc_dbg("Chip ID data: %02x %02x", dev->dev_id[0], dev->dev_id[1]);
	if (dev->dev_id[2])
		printc_dbg(" %02x", dev->dev_id[2]);

	if (device_is_fram(dev))
		printc_dbg(" [FRAM]");

	printc_dbg("\n");
	return 0;
}

/* Is there a more reliable way of doing this? */
int device_is_fram(device_t dev)
{
	const uint8_t a = dev->dev_id[0];
	const uint8_t b = dev->dev_id[1];

	return ((a < 0x04) && (b == 0x81)) ||
	       (((a & 0xf0) == 0x70) && ((b & 0x8e) == 0x80));
}

int device_erase(device_erase_type_t et, address_t addr)
{
	if (device_is_fram(device_default)) {
		printc_err("warning: not attempting erase of FRAM device\n");
		return 0;
	}

	return device_default->type->erase(device_default, et, addr);
}
