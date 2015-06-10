/* MSPDebug - debugging tool for the eZ430
 * Copyright (C) 2009-2012 Daniel Beer
 * Copyright (C) 2012 Stanimir Bonev
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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "util.h"
#include "fet.h"
#include "fet_core.h"
#include "fet_error.h"
#include "fet_proto.h"
#include "fet_db.h"
#include "output.h"
#include "opdb.h"
#include "ctrlc.h"

#include "fet_olimex_db.h"
#include "devicelist.h"

struct fet_device {
	struct device                   base;

	int                             version;
	int				fet_flags;

	int				poll_enable;

	struct fet_proto		proto;
	fperm_t				active_fperm;
};

/**********************************************************************
 * FET command codes.
 *
 * These come from uif430 by Robert Kavaler (kavaler@diva.com).
 * www.relavak.com
 */

#define C_INITIALIZE            0x01
#define C_CLOSE                 0x02
#define C_IDENTIFY              0x03
#define C_DEVICE                0x04
#define C_CONFIGURE             0x05
#define C_VCC                   0x06
#define C_RESET                 0x07
#define C_READREGISTERS         0x08
#define C_WRITEREGISTERS        0x09
#define C_READREGISTER          0x0a
#define C_WRITEREGISTER         0x0b
#define C_ERASE                 0x0c
#define C_READMEMORY            0x0d
#define C_WRITEMEMORY           0x0e
#define C_FASTFLASHER           0x0f
#define C_BREAKPOINT            0x10
#define C_RUN                   0x11
#define C_STATE                 0x12
#define C_SECURE                0x13
#define C_VERIFYMEMORY          0x14
#define C_FASTVERIFYMEMORY      0x15
#define C_ERASECHECK            0x16
#define C_EEMOPEN               0x17
#define C_EEMREADREGISTER       0x18
#define C_EEMREADREGISTERTEST   0x19
#define C_EEMWRITEREGISTER      0x1a
#define C_EEMCLOSE              0x1b
#define C_ERRORNUMBER           0x1c
#define C_GETCURVCCT            0x1d
#define C_GETEXTVOLTAGE         0x1e
#define C_FETSELFTEST           0x1f
#define C_FETSETSIGNALS         0x20
#define C_FETRESET              0x21
#define C_READI2C               0x22
#define C_WRITEI2C              0x23
#define C_ENTERBOOTLOADER       0x24

#define C_IDENT1                0x28
#define C_IDENT2                0x29
#define C_IDENT3                0x2b

#define C_CMM_PARAM		0x36
#define C_CMM_CTRL		0x37
#define C_CMM_READ		0x38

/* Constants for parameters of various FET commands */
#define FET_CONFIG_VERIFICATION 0
#define FET_CONFIG_EMULATION    1
#define FET_CONFIG_CLKCTRL      2
#define FET_CONFIG_MCLKCTRL     3
#define FET_CONFIG_FLASH_TESET  4
#define FET_CONFIG_FLASH_LOCK   5
#define FET_CONFIG_PROTOCOL     8
#define FET_CONFIG_UNLOCK_BSL	11

#define FET_RUN_FREE           1
#define FET_RUN_STEP           2
#define FET_RUN_BREAKPOINT     3

#define FET_RESET_PUC          0x01
#define FET_RESET_RST          0x02
#define FET_RESET_VCC          0x04
#define FET_RESET_ALL          0x07

#define FET_ERASE_SEGMENT      0
#define FET_ERASE_MAIN         1
#define FET_ERASE_ALL          2

#define FET_POLL_RUNNING        0x01
#define FET_POLL_BREAKPOINT     0x02

/**********************************************************************
 * MSP430 high-level control functions
 */

static void show_dev_info(const char *name, const struct fet_device *dev)
{
	printc_dbg("Device: %s\n", name);
	printc_dbg("Number of breakpoints: %d\n", dev->base.max_breakpoints);
}

static int identify_old(struct fet_device *dev)
{
	char idtext[64];

	if (fet_proto_xfer(&dev->proto, C_IDENTIFY, NULL, 0, 2, 70, 0) < 0)
		return -1;

	if (dev->proto.datalen < 0x26) {
		printc_err("fet: missing info\n");
		return -1;
	}

	memcpy(idtext, dev->proto.data + 4, 32);
	idtext[32] = 0;

	dev->base.max_breakpoints = LE_WORD(dev->proto.data, 0x2a);

	show_dev_info(idtext, dev);

	return 0;
}

static int identify_new(struct fet_device *dev, const char *force_id)
{
	const struct fet_db_record *r;

	if (fet_proto_xfer(&dev->proto, C_IDENT1, NULL, 0, 2, 0, 0) < 0) {
		printc_err("fet: command C_IDENT1 failed\n");
		return -1;
	}

	if (dev->proto.datalen < 2) {
		printc_err("fet: missing info\n");
		return -1;
	}

	printc_dbg("Device ID: 0x%02x%02x\n",
	       dev->proto.data[0], dev->proto.data[1]);

	if (force_id)
		r = fet_db_find_by_name(force_id);
	else
		r = fet_db_find_by_msg28(dev->proto.data,
					 dev->proto.datalen);

	if (!r) {
		printc_err("fet: unknown device\n");
		debug_hexdump("msg28_data:", dev->proto.data,
			      dev->proto.datalen);
		return -1;
	}

	dev->base.max_breakpoints = r->msg29_data[0x14];

	printc_dbg("  Code start address: 0x%x\n",
		   LE_WORD(r->msg29_data, 0));
	/*
	 * The value at 0x02 seems to contain a "virtual code end
	 * address". So this value seems to be useful only for
	 * calculating the total ROM size.
	 *
	 * For example, as for the msp430f6736 with 128kb ROM, the ROM
	 * is split into two areas: A "near" ROM, and a "far ROM".
	 */
	const uint32_t codeSize =
	  LE_LONG(r->msg29_data, 0x02)
	  - LE_WORD(r->msg29_data, 0)
	  + 1;
	printc_dbg("  Code size         : %u byte = %u kb\n",
		   codeSize,
		   codeSize / 1024);

	printc_dbg("  RAM  start address: 0x%x\n",
		   LE_WORD(r->msg29_data, 0x0c));
	printc_dbg("  RAM  end   address: 0x%x\n",
		   LE_WORD(r->msg29_data, 0x0e));
	const uint16_t ramSize =
	  LE_WORD(r->msg29_data, 0x0e)
	  - LE_WORD(r->msg29_data, 0x0c)
	  + 1;
	printc_dbg("  RAM  size         : %u byte = %u kb\n",
		   ramSize,
		   ramSize / 1024);

	show_dev_info(r->name, dev);

	if (fet_proto_xfer(&dev->proto, C_IDENT3,
			   r->msg2b_data, r->msg2b_len, 0) < 0)
		printc_err("fet: warning: message C_IDENT3 failed\n");

	if (fet_proto_xfer(&dev->proto, C_IDENT2,
			   r->msg29_data, FET_DB_MSG29_LEN,
			   3, r->msg29_params[0], r->msg29_params[1],
			   r->msg29_params[2]) < 0) {
		printc_err("fet: message C_IDENT2 failed\n");
		return -1;
	}

	return 0;
}

static int identify_olimex(struct fet_device *dev, const char *force_id)
{
	const struct fet_olimex_db_record *r;
	int db_indx;
	devicetype_t set_id = DT_UNKNOWN_DEVICE;
	devicetype_t dev_id = DT_UNKNOWN_DEVICE;
	uint8_t jtag_id;

	printc_dbg("Using Olimex identification procedure\n");

	if (force_id) {
		db_indx = fet_olimex_db_find_by_name(force_id);

		if (db_indx < 0) {
			printc_err("fet: no such device: %s\n", force_id);
			return -1;
		}

		dev_id = set_id = fet_olimex_db_index_to_type(db_indx);
	}

	/* first try */
	if (fet_proto_xfer(&dev->proto, C_IDENT1, NULL, 0, 3,
			   set_id, set_id, 0) < 0 &&
	    (4 != dev->proto.error)) /* No device error */
	{

		printc_err("fet: command C_IDENT1 failed\n");
		return -1;
	}

	if (dev->proto.datalen < 19) {
		printc_err("fet: missing info\n");
		return -1;
	}

	jtag_id = dev->proto.data[18];

	/* find device in data base */
	if (DT_UNKNOWN_DEVICE == dev_id) {
		db_indx = fet_olimex_db_identify(dev->proto.data);
		dev_id = fet_olimex_db_index_to_type(db_indx);
	}

	if ((DT_UNKNOWN_DEVICE == dev_id && 0x91 == jtag_id) ||
	    (4 == dev->proto.error)) {
		/* second try with magic pattern */
		if (fet_proto_xfer(&dev->proto, C_IDENT1, NULL, 0, 3,
				   set_id, dev_id, 0) < 0) {
			printc_err("fet: command C_IDENT1 with "
				   "magic patern failed\n");
			return -1;
		}

		db_indx = fet_olimex_db_identify(dev->proto.data);
		dev_id = fet_olimex_db_index_to_type(db_indx);
	}

	printc_dbg("Device ID: 0x%02x%02x\n",
	       dev->proto.data[0], dev->proto.data[1]);

	if (DT_UNKNOWN_DEVICE == dev_id) {
		printc_err("fet: can't find device in DB\n");
		return -1;
	}

	r = fet_db_get_record(dev_id);

	dev->base.max_breakpoints = r->msg29_data[0x14];

	printc_dbg("  Code start address: 0x%x\n",
		   LE_WORD(r->msg29_data, 0));
	/*
	 * The value at 0x02 seems to contain a "virtual code end
	 * address". So this value seems to be useful only for
	 * calculating the total ROM size.
	 *
	 * For example, as for the msp430f6736 with 128kb ROM, the ROM
	 * is split into two areas: A "near" ROM, and a "far ROM".
	 */
	const uint32_t codeSize =
	  LE_LONG(r->msg29_data, 0x02)
	  - LE_WORD(r->msg29_data, 0)
	  + 1;
	printc_dbg("  Code size         : %u byte = %u kb\n",
		   codeSize,
		   codeSize / 1024);

	printc_dbg("  RAM  start address: 0x%x\n",
		   LE_WORD(r->msg29_data, 0x0c));
	printc_dbg("  RAM  end   address: 0x%x\n",
		   LE_WORD(r->msg29_data, 0x0e));

	const uint16_t ramSize =
	  LE_WORD(r->msg29_data, 0x0e)
	  - LE_WORD(r->msg29_data, 0x0c)
	  + 1;

	printc_dbg("  RAM  size         : %u byte = %u kb\n",
		   ramSize, ramSize / 1024);

	show_dev_info(r->name, dev);

	if (fet_proto_xfer(&dev->proto, C_IDENT3,
			   r->msg2b_data, r->msg2b_len, 0) < 0)
		printc_err("fet: warning: message C_IDENT3 failed\n");

	if (fet_proto_xfer(&dev->proto, C_IDENT2,
			   r->msg29_data, FET_DB_MSG29_LEN,
			   3, r->msg29_params[0], r->msg29_params[1],
			   r->msg29_params[2]) < 0) {
		printc_err("fet: message C_IDENT2 failed\n");
		return -1;
	}

	return 0;
}

static int is_new_olimex(const struct fet_device *dev)
{
	if ((&device_olimex_iso_mk2 == dev->base.type) &&
	    (20000004 <= dev->version))
		return 1;

	if (((&device_olimex == dev->base.type) ||
	     (&device_olimex_v1 == dev->base.type) ||
	     (&device_olimex_iso == dev->base.type)) &&
	    (10004003 <= dev->version))
		return 1;

	return 0;
}

static int try_new(struct fet_device *dev, const char *force_id)
{
	if (!identify_new(dev, force_id))
		return 0;

	return identify_olimex(dev, force_id);
}

static int do_identify(struct fet_device *dev, const char *force_id)
{
	if (is_new_olimex(dev))
		return identify_olimex(dev, force_id);

	if (dev->fet_flags & FET_IDENTIFY_NEW)
		return try_new(dev, force_id);

	if (dev->version < 20300000)
		return identify_old(dev);

	return try_new(dev, force_id);
}

static void power_init(struct fet_device *dev)
{
	if (fet_proto_xfer(&dev->proto, C_CMM_PARAM, NULL, 0, 0) < 0) {
		printc_err("warning: device does not support power "
			   "profiling\n");
		return;
	}

	if (dev->proto.argv[0] <= 0 || dev->proto.argv[0] <= 0) {
		printc_err("Bad parameters returned by C_CMM_PARAM: "
			   "bufsize = %d bytes, %d us/sample\n",
			   dev->proto.argv[1], dev->proto.argv[0]);
		return;
	}

	printc("Power profiling enabled: bufsize = %d bytes, %d us/sample\n",
		dev->proto.argv[1], dev->proto.argv[0]);
	printc_shell("power-sample-us %d\n", dev->proto.argv[0]);

	dev->base.power_buf = powerbuf_new(POWERBUF_DEFAULT_SAMPLES,
		dev->proto.argv[0]);
	if (!dev->base.power_buf) {
		printc_err("Failed to allocate memory for power profile\n");
		return;
	}
}

static int power_start(struct fet_device *dev)
{
	if (!dev->base.power_buf)
		return 0;

	if (fet_proto_xfer(&dev->proto, C_CMM_CTRL, NULL, 0, 1, 1) < 0) {
		printc_err("fet: failed to start power profiling, "
			   "disabling\n");
		powerbuf_free(dev->base.power_buf);
		dev->base.power_buf = NULL;
		return -1;
	}

	powerbuf_begin_session(dev->base.power_buf, time(NULL));
	dev->poll_enable = 1;
	return 0;
}

static int power_end(struct fet_device *dev)
{
	if (!dev->base.power_buf)
		return 0;

	powerbuf_end_session(dev->base.power_buf);
	dev->poll_enable = 0;

	if (fet_proto_xfer(&dev->proto, C_CMM_CTRL, NULL, 0, 1, 1) < 0) {
		printc_err("fet: failed to end power profiling\n");
		return -1;
	}

	return 0;
}

static void shell_power(const uint8_t *data, int len)
{
	while (len > 0) {
		int plen = 128;
		char text[256];

		if (plen > len)
			plen = len;

		base64_encode(data, plen, text, sizeof(text));
		printc_shell("power-samples %s\n", text);

		len -= plen;
		data += plen;
	}
}

static int power_poll(struct fet_device *dev)
{
	address_t mab;
	address_t mab_samples[1024];
	unsigned int cur_samples[1024];
	unsigned int count = 0;
	int i;

	if (!dev->base.power_buf || !dev->poll_enable)
		return 0;

	if (fet_proto_xfer(&dev->proto, C_CMM_READ, NULL, 0, 0) < 0) {
		printc_err("fet: failed to fetch power data, disabling\n");
		power_end(dev);
		powerbuf_free(dev->base.power_buf);
		dev->base.power_buf = NULL;
		dev->poll_enable = 0;
		return -1;
	}

	shell_power(dev->proto.data, dev->proto.datalen);

	mab = powerbuf_last_mab(dev->base.power_buf);
	for (i = 0; i + 3 < dev->proto.datalen; i += 4) {
		uint32_t s = LE_LONG(dev->proto.data, i);

		if (s & 0x80000000) {
			mab = s & 0x7fffffff;
		} else if (count + 1 < ARRAY_LEN(cur_samples)) {
			cur_samples[count] = s;
			mab_samples[count] = mab;
			count++;
		}
	}

	powerbuf_add_samples(dev->base.power_buf, count,
			     cur_samples, mab_samples);

	return 0;
}

static int refresh_fperm(struct fet_device *dev)
{
	fperm_t fp = opdb_read_fperm();
	fperm_t delta = dev->active_fperm ^ fp;

	if (delta & FPERM_LOCKED_FLASH) {
		int opt = (fp & FPERM_LOCKED_FLASH) ? 1 : 0;

		printc_dbg("%s locked flash access\n",
			   opt ? "Enabling" : "Disabling");
		if (fet_proto_xfer(&dev->proto,
				   C_CONFIGURE, NULL, 0,
				   2, FET_CONFIG_FLASH_LOCK, opt) < 0) {
			printc_err("fet: FET_CONFIG_FLASH_LOCK failed\n");
			return -1;
		}
	}

	if (delta & FPERM_BSL) {
		int opt = (fp & FPERM_BSL) ? 1 : 0;

		printc_dbg("%s BSL access\n",
			   opt ? "Enabling" : "Disabling");
		if (fet_proto_xfer(&dev->proto,
				   C_CONFIGURE, NULL, 0,
				   2, FET_CONFIG_UNLOCK_BSL, opt) < 0) {
			printc_err("fet: FET_CONFIG_UNLOCK_BSL failed\n");
			return -1;
		}
	}

	dev->active_fperm = fp;
	return 0;
}

static int do_run(struct fet_device *dev, int type)
{
	if (fet_proto_xfer(&dev->proto, C_RUN, NULL, 0, 2, type, 0) < 0) {
		printc_err("fet: failed to restart CPU\n");
		return -1;
	}

	return 0;
}

int fet_erase(device_t dev_base, device_erase_type_t type, address_t addr)
{
	struct fet_device *dev = (struct fet_device *)dev_base;
	int fet_erase_type = FET_ERASE_MAIN;

	if (fet_proto_xfer(&dev->proto,
			   C_CONFIGURE, NULL, 0,
			   2, FET_CONFIG_CLKCTRL, 0x26) < 0) {
		printc_err("fet: config (1) failed\n");
		return -1;
	}

	refresh_fperm(dev);

	switch (type) {
	case DEVICE_ERASE_MAIN:
		fet_erase_type = FET_ERASE_MAIN;
		addr = 0xfffe;
		break;

	case DEVICE_ERASE_SEGMENT:
		fet_erase_type = FET_ERASE_SEGMENT;
		break;

	case DEVICE_ERASE_ALL:
		fet_erase_type = FET_ERASE_ALL;
		addr = 0xfffe;
		break;

	default:
		printc_err("fet: unsupported erase type\n");
		return -1;
	}

	if (fet_proto_xfer(&dev->proto, C_ERASE, NULL, 0,
			   3, fet_erase_type, addr, 1) < 0) {
		printc_err("fet: erase command failed\n");
		return -1;
	}

	if (fet_proto_xfer(&dev->proto, C_RESET, NULL, 0,
			   3, FET_RESET_ALL, 0, 0) < 0) {
		printc_err("fet: reset failed\n");
		return -1;
	}

	return 0;
}

device_status_t fet_poll(device_t dev_base)
{
	struct fet_device *dev = (struct fet_device *)dev_base;

	if (fet_proto_xfer(&dev->proto, C_STATE, NULL, 0, 1, 0) < 0) {
		printc_err("fet: polling failed\n");
		power_end(dev);
		return DEVICE_STATUS_ERROR;
	}

	if (dev->base.power_buf)
		power_poll(dev);
	else
		delay_ms(50);

	if (!(dev->proto.argv[0] & FET_POLL_RUNNING)) {
		power_end(dev);
		return DEVICE_STATUS_HALTED;
	}

	if (ctrlc_check())
		return DEVICE_STATUS_INTR;

	return DEVICE_STATUS_RUNNING;
}

static int refresh_bps(struct fet_device *dev)
{
	int i;
	int ret = 0;

	for (i = 0; i < dev->base.max_breakpoints; i++) {
		struct device_breakpoint *bp = &dev->base.breakpoints[i];

		if ((bp->flags & DEVICE_BP_DIRTY) &&
		    bp->type == DEVICE_BPTYPE_BREAK) {
			uint16_t addr = bp->addr;

			if (!(bp->flags & DEVICE_BP_ENABLED))
				addr = 0;

			if (fet_proto_xfer(&dev->proto, C_BREAKPOINT, NULL, 0,
					   2, i, addr) < 0) {
				printc_err("fet: failed to refresh "
					"breakpoint #%d\n", i);
				ret = -1;
			} else {
				bp->flags &= ~DEVICE_BP_DIRTY;
			}
		}
	}

	return ret;
}

int fet_ctl(device_t dev_base, device_ctl_t action)
{
	struct fet_device *dev = (struct fet_device *)dev_base;

	switch (action) {
	case DEVICE_CTL_RESET:
		if (fet_proto_xfer(&dev->proto, C_RESET, NULL, 0,
				   3, FET_RESET_ALL, 0, 0) < 0) {
			printc_err("fet: reset failed\n");
			return -1;
		}
		break;

	case DEVICE_CTL_RUN:
		if (refresh_bps(dev) < 0)
			printc_err("warning: fet: failed to refresh "
				"breakpoints\n");

		power_start(dev);
		if (do_run(dev, FET_RUN_BREAKPOINT) < 0) {
			power_end(dev);
			return -1;
		}

		return 0;

	case DEVICE_CTL_HALT:
		power_end(dev);
		if (fet_proto_xfer(&dev->proto, C_STATE, NULL, 0, 1, 1) < 0) {
			printc_err("fet: failed to halt CPU\n");
			return -1;
		}
		break;

	case DEVICE_CTL_STEP:
		if (do_run(dev, FET_RUN_STEP) < 0)
			return -1;

		for (;;) {
			device_status_t status = fet_poll(dev_base);

			if (status == DEVICE_STATUS_ERROR ||
			    status == DEVICE_STATUS_INTR)
				return -1;

			if (status == DEVICE_STATUS_HALTED)
				break;
		}
		break;

	case DEVICE_CTL_SECURE:
		if (fet_proto_xfer(&dev->proto, C_SECURE, NULL, 0, 0) < 0) {
			printc_err("fet: failed to secure device\n");
			return -1;
		}
		break;
	}

	return 0;
}

void fet_destroy(device_t dev_base)
{
	struct fet_device *dev = (struct fet_device *)dev_base;

	if (dev->fet_flags & FET_SKIP_CLOSE) {
		printc_dbg("Skipping close procedure\n");
	} else {
		/* The second argument to C_RESET is a boolean which
		 * specifies whether the chip should run or not. The
		 * final argument is also a boolean. Setting it non-zero
		 * is required to get the RST pin working on the G2231,
		 * but it must be zero on the FR5739, or else the value
		 * of the reset vector gets set to 0xffff at the start
		 * of the next JTAG session.
		 */
		if (fet_proto_xfer(&dev->proto, C_RESET, NULL, 0, 3,
				   FET_RESET_ALL, 1,
				   !device_is_fram(dev_base)) < 0)
			printc_err("fet: final reset failed\n");

		if (fet_proto_xfer(&dev->proto, C_CLOSE, NULL, 0, 1, 0) < 0)
			printc_err("fet: close command failed\n");

		if (dev->base.power_buf)
			powerbuf_free(dev->base.power_buf);
	}

	dev->proto.transport->ops->destroy(dev->proto.transport);
	free(dev);
}

static int read_byte(struct fet_device *dev, address_t addr, uint8_t *out)
{
	address_t base = addr & ~1;

	if (fet_proto_xfer(&dev->proto, C_READMEMORY, NULL, 0,
			   2, base, 2) < 0) {
		printc_err("fet: failed to read byte from 0x%04x\n", addr);
		return -1;
	}

	*out = dev->proto.data[addr & 1];
	return 0;
}

static int write_byte(struct fet_device *dev, address_t addr, uint8_t value)
{
	uint8_t buf[2];
	address_t base = addr & ~1;

	if (fet_proto_xfer(&dev->proto, C_READMEMORY, NULL, 0, 2, base, 2) < 0) {
		printc_err("fet: failed to read byte from 0x%04x\n", addr);
		return -1;
	}

	buf[0] = dev->proto.data[0];
	buf[1] = dev->proto.data[1];
	buf[addr & 1] = value;

	if (fet_proto_xfer(&dev->proto, C_WRITEMEMORY, buf, 2, 1, base) < 0) {
		printc_err("fet: failed to write byte from 0x%04x\n", addr);
		return -1;
	}

	return 0;
}

static int get_adjusted_block_size(void)
{
	int block_size = opdb_get_numeric("fet_block_size") & ~1;

	if (block_size < 2)
		block_size = 2;
	if (block_size > FET_PROTO_MAX_BLOCK)
		block_size = FET_PROTO_MAX_BLOCK;

	return block_size;
}

int fet_readmem(device_t dev_base, address_t addr, uint8_t *buffer,
		address_t count)
{
	struct fet_device *dev = (struct fet_device *)dev_base;
	int block_size = get_adjusted_block_size();

	if (addr & 1) {
		if (read_byte(dev, addr, buffer) < 0)
			return -1;
		addr++;
		buffer++;
		count--;
	}

	while (count > 1) {
		int plen = count > block_size ? block_size : count;

		plen &= ~0x1;

		if (fet_proto_xfer(&dev->proto, C_READMEMORY, NULL, 0,
				   2, addr, plen) < 0) {
			printc_err("fet: failed to read "
				"from 0x%04x\n", addr);
			return -1;
		}

		if (dev->proto.datalen < plen) {
			printc_err("fet: short data: "
				"%d bytes\n", dev->proto.datalen);
			return -1;
		}

		memcpy(buffer, dev->proto.data, plen);
		buffer += plen;
		count -= plen;
		addr += plen;
	}

	if (count && read_byte(dev, addr, buffer) < 0)
		return -1;

	return 0;
}

int fet_writemem(device_t dev_base, address_t addr,
		 const uint8_t *buffer, address_t count)
{
	struct fet_device *dev = (struct fet_device *)dev_base;
	int block_size = get_adjusted_block_size();

	refresh_fperm(dev);

	if (addr & 1) {
		if (write_byte(dev, addr, *buffer) < 0)
			return -1;
		addr++;
		buffer++;
		count--;
	}

	while (count > 1) {
		int plen = count > block_size ? block_size : count;
		int ret;

		plen &= ~0x1;

		ret = fet_proto_xfer(&dev->proto,
				     C_WRITEMEMORY, buffer, plen, 1, addr);

		if (ret < 0) {
			printc_err("fet: failed to write to 0x%04x\n",
				addr);
			return -1;
		}

		buffer += plen;
		count -= plen;
		addr += plen;
	}

	if (count && write_byte(dev, addr, *buffer) < 0)
		return -1;

	return 0;
}

int fet_getregs(device_t dev_base, address_t *regs)
{
	struct fet_device *dev = (struct fet_device *)dev_base;
	int i;

	if (fet_proto_xfer(&dev->proto, C_READREGISTERS, NULL, 0, 0) < 0)
		return -1;

	if (dev->proto.datalen < DEVICE_NUM_REGS * 4) {
		printc_err("fet: short reply (%d bytes)\n",
			dev->proto.datalen);
		return -1;
	}

	for (i = 0; i < DEVICE_NUM_REGS; i++)
		regs[i] = LE_LONG(dev->proto.data, i * 4);

	return 0;
}

int fet_setregs(device_t dev_base, const address_t *regs)
{
	struct fet_device *dev = (struct fet_device *)dev_base;
	uint8_t buf[DEVICE_NUM_REGS * 4];;
	int i;
	int ret;

	memset(buf, 0, sizeof(buf));

	for (i = 0; i < DEVICE_NUM_REGS; i++) {
		buf[i * 4] = regs[i] & 0xff;
		buf[i * 4 + 1] = (regs[i] >> 8) & 0xff;
		buf[i * 4 + 2] = (regs[i] >> 16) & 0xff;
		buf[i * 4 + 3] = regs[i] >> 24;
	}

	ret = fet_proto_xfer(&dev->proto, C_WRITEREGISTERS,
			     buf, sizeof(buf), 1, 0xffff);

	if (ret < 0) {
		printc_err("fet: context set failed\n");
		return -1;
	}

	return 0;
}

static int do_configure(struct fet_device *dev,
			const struct device_args *args)
{
	if (!(args->flags & DEVICE_FLAG_JTAG)) {
		if (!fet_proto_xfer(&dev->proto, C_CONFIGURE, NULL, 0,
				    2, FET_CONFIG_PROTOCOL, 1)) {
			printc_dbg("Configured for Spy-Bi-Wire\n");
			return 0;
		}

		printc_err("fet: Spy-Bi-Wire configuration failed\n");
		return -1;
	}

	if (!fet_proto_xfer(&dev->proto, C_CONFIGURE, NULL, 0,
			    2, FET_CONFIG_PROTOCOL, 2)) {
		printc_dbg("Configured for JTAG (2)\n");
		return 0;
	}

	printc_err("fet: warning: JTAG configuration failed -- "
		"retrying\n");

	if (!fet_proto_xfer(&dev->proto, C_CONFIGURE, NULL, 0,
			    2, FET_CONFIG_PROTOCOL, 0)) {
		printc_dbg("Configured for JTAG (0)\n");
		return 0;
	}

	printc_err("fet: JTAG configuration failed\n");
	return -1;
}

int try_open(struct fet_device *dev, const struct device_args *args,
	     int send_reset)
{
	transport_t transport = dev->proto.transport;

	if (dev->proto.proto_flags & FET_PROTO_NOLEAD_SEND) {
		printc("Resetting Olimex command processor...\n");
		transport->ops->send(transport, (const uint8_t *)"\x7e", 1);
		delay_ms(5);
		transport->ops->send(transport, (const uint8_t *)"\x7e", 1);
		delay_ms(5);
	}

	printc_dbg("Initializing FET...\n");
	if (fet_proto_xfer(&dev->proto, C_INITIALIZE, NULL, 0, 0) < 0) {
		printc_err("fet: open failed\n");
		return -1;
	}

	dev->version = dev->proto.argv[0];
	printc_dbg("FET protocol version is %d\n", dev->version);

	if (fet_proto_xfer(&dev->proto, 0x27, NULL, 0, 1, 4) < 0) {
		printc_err("fet: init failed\n");
		return -1;
	}

	/* set VCC */
	if (fet_proto_xfer(&dev->proto, C_VCC, NULL, 0,
			   1, args->vcc_mv) < 0)
		printc_err("warning: fet: set VCC failed\n");
	else
		printc_dbg("Set Vcc: %d mV\n", args->vcc_mv);


	if (do_configure(dev, args) < 0)
		return -1;

	if (send_reset || args->flags & DEVICE_FLAG_FORCE_RESET) {
		printc_dbg("Sending reset...\n");
		if (fet_proto_xfer(&dev->proto, C_RESET, NULL, 0,
				   3, FET_RESET_ALL, 0, 0) < 0)
			printc_err("warning: fet: reset failed\n");
	}

	/* Identify the chip */
	if (do_identify(dev, args->forced_chip_id) < 0) {
		printc_err("fet: identify failed\n");
		return -1;
	}

	return 0;
}

device_t fet_open(const struct device_args *args,
		  int proto_flags, transport_t transport,
		  int fet_flags,
		  const struct device_class *type)
{
	struct fet_device *dev = malloc(sizeof(*dev));
	int i;

	if (args->flags & DEVICE_FLAG_SKIP_CLOSE)
		fet_flags |= FET_SKIP_CLOSE;

	if (!dev) {
		pr_error("fet: failed to allocate memory");
		return NULL;
	}

	memset(dev, 0, sizeof(*dev));

	fet_proto_init(&dev->proto, transport, proto_flags);

	dev->base.type = type;
	dev->fet_flags = fet_flags;

	if (try_open(dev, args, fet_flags & FET_FORCE_RESET) < 0) {
		delay_ms(500);
		printc_dbg("Trying again...\n");
		if (try_open(dev, args, !is_new_olimex(dev)) < 0)
			goto fail;
	}

	/* Make sure breakpoints get reset on the first run */
	if (dev->base.max_breakpoints > DEVICE_MAX_BREAKPOINTS)
		dev->base.max_breakpoints = DEVICE_MAX_BREAKPOINTS;
	for (i = 0; i < dev->base.max_breakpoints; i++)
		dev->base.breakpoints[i].flags = DEVICE_BP_DIRTY;

	/* Initialize power profiling */
	power_init(dev);

	return (device_t)dev;

 fail:
	transport->ops->destroy(transport);
	free(dev);
	return NULL;
}
