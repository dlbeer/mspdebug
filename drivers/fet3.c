/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2012 Daniel Beer
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
#include "util/ctrlc.h"
#include "dis.h"
#include "output.h"
#include "comport.h"
#include "cdc_acm.h"
#include "fet3.h"
#include "chipinfo.h"
#include "v3hil.h"

struct fet3 {
	struct device		base;
	struct v3hil		hil;
};

static int fet3_readmem(device_t dev_base, address_t addr,
			uint8_t *mem, address_t len)
{
	struct fet3 *fet = (struct fet3 *)dev_base;

	if (addr & 1) {
		uint8_t word[2];

		if (v3hil_read(&fet->hil, addr - 1, word, 2) < 0)
			return -1;

		mem[0] = word[1];
		addr++;
		len--;
	}

	while (len >= 2) {
		int r = 128;

		if (r > len)
			r = len;

		r = v3hil_read(&fet->hil, addr, mem, r & ~1);
		if (r < 0)
			return -1;

		addr += r;
		mem += r;
		len -= r;
	}

	if (len) {
		uint8_t word[2];

		if (v3hil_read(&fet->hil, addr, word, 2) < 0)
			return -1;

		mem[0] = word[0];
		addr++;
		len--;
	}

	return 0;
}

static int fet3_writemem(device_t dev_base, address_t addr,
			    const uint8_t *mem, address_t len)
{
	struct fet3 *fet = (struct fet3 *)dev_base;

	if (addr & 1) {
		uint8_t word[2];

		if (v3hil_read(&fet->hil, addr - 1, word, 2) < 0)
			return -1;

		word[1] = mem[0];
		if (v3hil_write(&fet->hil, addr - 1, word, 2) < 0)
			return -1;

		addr++;
		len--;
	}

	while (len >= 2) {
		int r = 128;

		if (r > len)
			r = len;

		r = v3hil_write(&fet->hil, addr, mem, r & ~1);
		if (r < 0)
			return -1;

		addr += r;
		mem += r;
		len -= r;
	}

	if (len) {
		uint8_t word[2];

		if (v3hil_read(&fet->hil, addr, word, 2) < 0)
			return -1;

		word[0] = mem[0];
		if (v3hil_write(&fet->hil, addr, word, 2) < 0)
			return -1;

		addr++;
		len--;
	}

	return 0;
}

static int fet3_setregs(device_t dev_base, const address_t *regs)
{
	struct fet3 *fet = (struct fet3 *)dev_base;

	memcpy(fet->hil.regs, regs, sizeof(fet->hil.regs));
	return -1;
}

static int fet3_getregs(device_t dev_base, address_t *regs)
{
	struct fet3 *fet = (struct fet3 *)dev_base;

	memcpy(regs, fet->hil.regs, sizeof(fet->hil.regs));
	return 0;
}

static int fet3_ctl(device_t dev_base, device_ctl_t type)
{
	struct fet3 *fet = (struct fet3 *)dev_base;

	switch (type) {
	case DEVICE_CTL_RESET:
		if (v3hil_sync(&fet->hil) < 0)
			return -1;
		return v3hil_update_regs(&fet->hil);

	case DEVICE_CTL_RUN:
		if (v3hil_flush_regs(&fet->hil) < 0)
			return -1;
		return v3hil_context_restore(&fet->hil, 0);

	case DEVICE_CTL_HALT:
		if (v3hil_context_save(&fet->hil) < 0)
			return -1;
		return v3hil_update_regs(&fet->hil);

	case DEVICE_CTL_STEP:
		if (v3hil_flush_regs(&fet->hil) < 0)
			return -1;
		if (v3hil_single_step(&fet->hil) < 0)
			return -1;
		return v3hil_update_regs(&fet->hil);

	default:
		printc_err("fet3: unsupported operation\n");
		return -1;
	}

	return 0;
}

static device_status_t fet3_poll(device_t dev_base)
{
	/* We don't support breakpoints yet, so there's nothing to poll
	 * for.
	 */
	if(delay_ms(500) < 0)
		return DEVICE_STATUS_INTR;

	return DEVICE_STATUS_RUNNING;
}

static int fet3_erase(device_t dev_base, device_erase_type_t type,
		      address_t addr)
{
	struct fet3 *fet = (struct fet3 *)dev_base;

	if (type == DEVICE_ERASE_ALL) {
		printc_err("fet3: mass erase is not supported\n");
		return -1;
	}

	return v3hil_erase(&fet->hil,
		(type == DEVICE_ERASE_MAIN) ? ADDRESS_NONE : addr);
}

static int debug_init(struct fet3 *fet, const struct device_args *args)
{
	if (v3hil_comm_init(&fet->hil) < 0)
		return -1;

	printc_dbg("Set VCC: %d mV\n", args->vcc_mv);
	if (v3hil_set_vcc(&fet->hil, args->vcc_mv) < 0)
		return -1;

	printc_dbg("Starting interface...\n");
	if (v3hil_start_jtag(&fet->hil,
		    (args->flags & DEVICE_FLAG_JTAG) ?
		    V3HIL_JTAG_JTAG : V3HIL_JTAG_SPYBIWIRE) < 0)
		return -1;

	if (args->forced_chip_id) {
		fet->hil.chip = chipinfo_find_by_name(args->forced_chip_id);
		if (!fet->hil.chip) {
			printc_err("fet3: unknown chip: %s\n",
				args->forced_chip_id);
			goto fail_stop_jtag;
		}
	} else {
		if (v3hil_identify(&fet->hil) < 0)
			goto fail_stop_jtag;
	}

	fet->base.chip = fet->hil.chip;

	if (v3hil_configure(&fet->hil) < 0)
		goto fail_stop_jtag;
	if (v3hil_update_regs(&fet->hil) < 0)
		goto fail_stop_jtag;

	return 0;

fail_stop_jtag:
	v3hil_stop_jtag(&fet->hil);
	return -1;
}

static device_t fet3_open(const struct device_args *args)
{
	struct fet3 *fet;
	transport_t trans;

	if (args->flags & DEVICE_FLAG_TTY)
		trans = comport_open(args->path, 460800);
	else
		trans = cdc_acm_open(args->path, args->requested_serial,
				     460800, 0x2047, 0x0013);

	if (!trans) {
		printc_err("fet3: failed to open transport\n");
		return NULL;
	}

	fet = malloc(sizeof(*fet));
	if (!fet) {
		printc_err("fet3: malloc: %s\n", last_error());
		trans->ops->destroy(trans);
		return NULL;
	}

	memset(fet, 0, sizeof(*fet));
	fet->base.type = &device_ezfet;

	/* Breakpoints aren't supported yet */
	fet->base.max_breakpoints = 0;

	v3hil_init(&fet->hil, trans, 0);

	if (debug_init(fet, args) < 0) {
		trans->ops->destroy(trans);
		free(fet);
		return NULL;
	}

	return &fet->base;
}

static void fet3_destroy(device_t dev_base)
{
	struct fet3 *fet = (struct fet3 *)dev_base;
	transport_t tr = fet->hil.hal.trans;

	v3hil_flush_regs(&fet->hil);
	v3hil_context_restore(&fet->hil, 1);
	v3hil_stop_jtag(&fet->hil);

	tr->ops->destroy(tr);
	free(fet);
}

const struct device_class device_ezfet = {
	.name		= "ezfet",
	.help		= "Texas Instruments eZ-FET",
	.open		= fet3_open,
	.destroy	= fet3_destroy,
	.readmem	= fet3_readmem,
	.writemem	= fet3_writemem,
	.getregs	= fet3_getregs,
	.setregs	= fet3_setregs,
	.ctl		= fet3_ctl,
	.poll		= fet3_poll,
	.erase		= fet3_erase,
	.getconfigfuses = NULL
};
