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

#include <string.h>
#include "output.h"
#include "device.h"
#include "bytes.h"

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

static uint8_t tlv_data[1024];

int tlv_read(device_t dev)
{
	if (dev->type->readmem(dev, 0x1a00, tlv_data, 8) < 0)
		return -1;

	uint8_t info_len = tlv_data[0];
	if (info_len < 1 || info_len > 8)
		return -1;

	int tlv_size = 4 * (1 << info_len);
	if (dev->type->readmem(dev, 0x1a00+8, tlv_data+8, tlv_size-8) < 0)
		return -1;

	return 0;
}

int tlv_find(const uint8_t type, uint8_t * const size, uint8_t ** const ptr)
{
	const int tlv_size = 4 * (1 << tlv_data[0]);
	int i = 8;
	*ptr = NULL;
	*size = 0;
	while (i + 3 < tlv_size) {
		uint8_t tag = tlv_data[i++];
		uint8_t len = tlv_data[i++];

		if (tag == 0xff)
			break;

		if (tag == type) {
			*ptr = tlv_data + i;
			*size = len;
			break;
		}

		i += len;
	}

        return *ptr != NULL;
}


static void show_device_type(device_t dev)
{
	printc("Device: %s", dev->chip->name);

	if (device_is_fram(dev))
		printc(" [FRAM]");

	printc("\n");
}


int device_probe_id(device_t dev, const char *force_id)
{
	/* skip probe if driver already did it */
	if (dev->chip) {
		show_device_type(dev);
		return 0;
	}

	/* use forced id if present */
	if (force_id) {
		dev->chip = chipinfo_find_by_name(force_id);
		if (!dev->chip) {
			printc_err("unknown chip: %s\n", force_id);
			return -1;
		}
                printc("Device: %s (forced)\n", dev->chip->name);
		return 0;
	}

	/* no probing if not requested */
	if (!dev->need_probe)
		return 0;

	/* else proceed with identification */
	uint8_t data[16];

	if (dev->type->readmem(dev, 0xff0, data, sizeof(data)) < 0) {
		printc_err("device_probe_id: read failed\n");
		return -1;
	}

	struct chipinfo_id id;
	memset(&id, 0, sizeof(id));

	if (data[0] == 0x80) {
		if (tlv_read(dev) < 0) {
			printc_err("device_probe_id: tlv_read failed\n");
			return -1;
		}

		dev->dev_id[0] = tlv_data[4];
		dev->dev_id[1] = tlv_data[5];
		dev->dev_id[2] = tlv_data[6];

		id.ver_id = r16le(tlv_data + 4);
		id.revision = tlv_data[6];
		id.config = tlv_data[7];
		id.fab = 0x55;
		id.self = 0x5555;
		id.fuses = 0x55;

		/* Search TLV for sub-ID */
		uint8_t len;
		uint8_t *p;
		if (tlv_find(0x14, &len, &p)) {
			if (len >= 2)
				id.ver_sub_id = r16le(p);
		}

	} else {
		dev->dev_id[0] = data[0];
		dev->dev_id[1] = data[1];
		dev->dev_id[2] = data[13];

		id.ver_id = r16le(data);
		id.ver_sub_id = 0;
		id.revision = data[2];
		id.fab = data[3];
		id.self = r16le(data + 8);
		id.config = data[13] & 0x7f;
	}

	printc_dbg("Chip ID data:\n");
	printc_dbg("  ver_id:         %04x\n", id.ver_id);
	printc_dbg("  ver_sub_id:     %04x\n", id.ver_sub_id);
	printc_dbg("  revision:       %02x\n", id.revision);
	printc_dbg("  fab:            %02x\n", id.fab);
	printc_dbg("  self:           %04x\n", id.self);
	printc_dbg("  config:         %02x\n", id.config);
	//printc_dbg("  fuses:          %02x\n", id.fuses);
	//printc_dbg("  activation_key: %08x\n", id.activation_key);

	dev->chip = chipinfo_find_by_id(&id);
	if (!dev->chip)
		return -1;

	show_device_type(dev);
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
