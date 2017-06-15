/* MSPDebug - debugging tool for the eZ430
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
 *
 * Various constants and tables come from uif430, written by Robert
 * Kavaler (kavaler@diva.com). This is available under the same license
 * as this program, from www.relavak.com.
 */

#include <stdlib.h>

#include "fet.h"
#include "fet_proto.h"
#include "fet_core.h"
#include "output.h"
#include "comport.h"
#include "ftdi.h"
#include "rf2500.h"
#include "ti3410.h"
#include "cp210x.h"
#include "cdc_acm.h"
#include "obl.h"

static device_t fet_open_rf2500(const struct device_args *args)
{
	transport_t trans;

	if (args->flags & DEVICE_FLAG_TTY) {
		printc_err("This driver does not support TTY devices.\n");
		return NULL;
	}

	trans = rf2500_open(args->path, args->requested_serial);
	if (!trans)
		return NULL;

	return fet_open(args, FET_PROTO_SEPARATE_DATA, trans,
			0, &device_rf2500);
}

const struct device_class device_rf2500 = {
	.name		= "rf2500",
	.help		=
"eZ430-RF2500 devices. Only USB connection is supported.",
	.open		= fet_open_rf2500,
	.destroy	= fet_destroy,
	.readmem	= fet_readmem,
	.writemem	= fet_writemem,
	.erase		= fet_erase,
	.getregs	= fet_getregs,
	.setregs	= fet_setregs,
	.ctl		= fet_ctl,
	.poll		= fet_poll,
	.getconfigfuses = NULL
};

static device_t fet_open_olimex_iso_mk2(const struct device_args *args)
{
	transport_t trans;
	uint32_t version;

	if (args->flags & DEVICE_FLAG_TTY)
		trans = comport_open(args->path, 115200);
	else
		trans = cdc_acm_open(args->path, args->requested_serial,
				     115200, 0x15ba, 0x0100);

	if (!trans)
		return NULL;

	if (args->require_fwupdate) {
		if (obl_update(trans, args->require_fwupdate) < 0) {
			trans->ops->destroy(trans);
			return NULL;
		}

		obl_reset(trans);
		trans->ops->destroy(trans);

		printc("Resetting, please wait...\n");
		delay_s(15);

		if (args->flags & DEVICE_FLAG_TTY)
			trans = comport_open(args->path, 115200);
		else
			trans = cdc_acm_open(args->path,
				    args->requested_serial,
				    115200, 0x15ba, 0x0100);

		if (!trans)
			return NULL;
	}

	if (!obl_get_version(trans, &version))
		printc_dbg("Olimex firmware version: %x\n", version);

	return fet_open(args,
			FET_PROTO_NOLEAD_SEND | FET_PROTO_EXTRA_RECV,
			trans,
			FET_FORCE_RESET | FET_IDENTIFY_NEW,
			&device_olimex_iso_mk2);
}

const struct device_class device_olimex_iso_mk2 = {
	.name		= "olimex-iso-mk2",
	.help		=
"Olimex MSP430-JTAG-ISO-MK2.",
	.open		= fet_open_olimex_iso_mk2,
	.destroy	= fet_destroy,
	.readmem	= fet_readmem,
	.writemem	= fet_writemem,
	.erase		= fet_erase,
	.getregs	= fet_getregs,
	.setregs	= fet_setregs,
	.ctl		= fet_ctl,
	.poll		= fet_poll,
	.getconfigfuses = NULL
};

static device_t fet_open_olimex(const struct device_args *args)
{
	transport_t trans;

	if (args->flags & DEVICE_FLAG_TTY)
		trans = comport_open(args->path, 115200);
	else
		trans = cdc_acm_open(args->path, args->requested_serial,
				     115200, 0x15ba, 0x0031);

	if (!trans)
		return NULL;

	return fet_open(args,
			FET_PROTO_NOLEAD_SEND | FET_PROTO_EXTRA_RECV,
			trans,
			FET_IDENTIFY_NEW | FET_FORCE_RESET,
			&device_olimex);
}

const struct device_class device_olimex = {
	.name		= "olimex",
	.help		=
"Olimex MSP-JTAG-TINY.",
	.open		= fet_open_olimex,
	.destroy	= fet_destroy,
	.readmem	= fet_readmem,
	.writemem	= fet_writemem,
	.erase		= fet_erase,
	.getregs	= fet_getregs,
	.setregs	= fet_setregs,
	.ctl		= fet_ctl,
	.poll		= fet_poll,
	.getconfigfuses = NULL
};

static device_t fet_open_olimex_v1(const struct device_args *args)
{
	transport_t trans;

	if (args->flags & DEVICE_FLAG_TTY)
		trans = comport_open(args->path, 500000);
	else
		trans = cp210x_open(args->path, args->requested_serial,
				    500000, 0x15ba, 0x0002);

        if (!trans)
                return NULL;

	return fet_open(args,
			FET_PROTO_NOLEAD_SEND | FET_PROTO_EXTRA_RECV,
			trans,
			FET_IDENTIFY_NEW,
			&device_olimex_v1);
}

const struct device_class device_olimex_v1 = {
	.name		= "olimex-v1",
	.help		=
"Olimex MSP-JTAG-TINY (V1).",
	.open		= fet_open_olimex_v1,
	.destroy	= fet_destroy,
	.readmem	= fet_readmem,
	.writemem	= fet_writemem,
	.erase		= fet_erase,
	.getregs	= fet_getregs,
	.setregs	= fet_setregs,
	.ctl		= fet_ctl,
	.poll		= fet_poll
};

static device_t fet_open_olimex_iso(const struct device_args *args)
{
	transport_t trans;

	if (args->flags & DEVICE_FLAG_TTY)
		trans = comport_open(args->path, 200000);
	else
		trans = ftdi_open(args->path, args->requested_serial,
				  0x15ba, 0x0008, 200000);

	if (!trans)
		return NULL;

	return fet_open(args,
			FET_PROTO_NOLEAD_SEND | FET_PROTO_EXTRA_RECV,
			trans,
			FET_IDENTIFY_NEW,
			&device_olimex_iso);
}

const struct device_class device_olimex_iso = {
	.name		= "olimex-iso",
	.help		=
"Olimex MSP-JTAG-ISO.",
	.open		= fet_open_olimex_iso,
	.destroy	= fet_destroy,
	.readmem	= fet_readmem,
	.writemem	= fet_writemem,
	.erase		= fet_erase,
	.getregs	= fet_getregs,
	.setregs	= fet_setregs,
	.ctl		= fet_ctl,
	.poll		= fet_poll,
	.getconfigfuses = NULL
};

static device_t fet_open_uif(const struct device_args *args)
{
	transport_t trans;

	if (args->flags & DEVICE_FLAG_TTY)
		trans = comport_open(args->path, 460800);
	else
		trans = ti3410_open(args->path, args->requested_serial);

	if (!trans)
		return NULL;

	return fet_open(args, 0, trans, 0, &device_uif);
}

const struct device_class device_uif = {
	.name		= "uif",
	.help		=
"TI FET430UIF and compatible devices (e.g. eZ430).",
	.open		= fet_open_uif,
	.destroy	= fet_destroy,
	.readmem	= fet_readmem,
	.writemem	= fet_writemem,
	.erase		= fet_erase,
	.getregs	= fet_getregs,
	.setregs	= fet_setregs,
	.ctl		= fet_ctl,
	.poll		= fet_poll,
	.getconfigfuses = NULL
};
