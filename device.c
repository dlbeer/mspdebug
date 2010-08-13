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

#include "device.h"

device_t device_default;

static int addbrk(device_t dev, uint16_t addr)
{
	int i;
	int which = -1;
	struct device_breakpoint *bp;

	for (i = 0; i < dev->max_breakpoints; i++) {
		bp = &dev->breakpoints[i];

		if (bp->flags & DEVICE_BP_ENABLED) {
			if (bp->addr == addr)
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

	return which;
}

static void delbrk(device_t dev, uint16_t addr)
{
	int i;

	for (i = 0; i < dev->max_breakpoints; i++) {
		struct device_breakpoint *bp = &dev->breakpoints[i];

		if ((bp->flags & DEVICE_BP_ENABLED) &&
		    bp->addr == addr) {
			bp->flags = DEVICE_BP_DIRTY;
			bp->addr = 0;
		}
	}
}

int device_setbrk(device_t dev, int which, int enabled, address_t addr)
{
	if (which < 0) {
		if (enabled)
			return addbrk(dev, addr);

		delbrk(dev, addr);
	} else {
		struct device_breakpoint *bp = &dev->breakpoints[which];
		int new_flags = enabled ? DEVICE_BP_ENABLED : 0;

		if (!enabled)
			addr = 0;

		if (bp->addr != addr ||
		    (bp->flags & DEVICE_BP_ENABLED) != new_flags) {
			bp->flags = new_flags | DEVICE_BP_DIRTY;
			bp->addr = addr;
		}
	}

	return 0;
}
