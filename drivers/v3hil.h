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

#ifndef V3HIL_H_
#define V3HIL_H_

#include "hal_proto.h"
#include "chipinfo.h"
#include "util.h"
#include "device.h"
#include "transport.h"

/* Clock calibration data */
struct v3hil_calibrate {
	uint8_t			is_cal;

	/* Calibration parameters for write/erase funclets */
	uint16_t		cal0;
	uint16_t		cal1;
};

struct v3hil {
	struct hal_proto	hal;
	const struct chipinfo	*chip;

	/* 0x89 is old-style CPU */
	uint8_t			jtag_id;

	/* Lower 8 bits of saved WDTCTL */
	uint8_t			wdtctl;

	/* Register cache: this must be flushed before restoring context
	 * and updated after saving context.
	 */
	address_t		regs[DEVICE_NUM_REGS];

	struct v3hil_calibrate	cal;
};

/* Initialize data, associate transport */
void v3hil_init(struct v3hil *h, transport_t trans,
		hal_proto_flags_t flags);

/* Reset communications and probe HAL. */
int v3hil_comm_init(struct v3hil *h);

/* Set voltage */
int v3hil_set_vcc(struct v3hil *h, int vcc_mv);

/* Start/stop JTAG controller */
typedef enum {
	V3HIL_JTAG_JTAG = 0,
	V3HIL_JTAG_SPYBIWIRE = 1
} v3hil_jtag_type_t;

int v3hil_start_jtag(struct v3hil *h, v3hil_jtag_type_t);
int v3hil_stop_jtag(struct v3hil *h);

/* Synchronize JTAG and reset the chip. This is the only operation which
 * can be done pre-configuration.
 */
int v3hil_sync(struct v3hil *h);

/* Run the chip identification procedure. chip will be filled out if
 * this is successful. This calls v3hil_sync().
 */
int v3hil_identify(struct v3hil *h);

/* Configure for the current chip */
int v3hil_configure(struct v3hil *h);

/* Read/write memory. LSB of address and size are ignored. Number of
 * bytes read is returned, which may be less than requested if a memory
 * map boundary is crossed.
 */
int v3hil_read(struct v3hil *h, address_t addr,
	       uint8_t *mem, address_t size);
int v3hil_write(struct v3hil *h, address_t addr,
		const uint8_t *mem, address_t size);

/* Erase flash. If address is specified, a segment erase is performed.
 * Otherwise, ADDRESS_NONE indicates that a main memory erase should be
 * performed.
 */
int v3hil_erase(struct v3hil *h, address_t segment);

/* Read/write register cache. */
int v3hil_update_regs(struct v3hil *h);
int v3hil_flush_regs(struct v3hil *h);

/* Restore context (run) and save context (halt). */
int v3hil_context_restore(struct v3hil *h, int free);
int v3hil_context_save(struct v3hil *h);

/* Single-step the CPU. You must handle the register cache yourself. */
int v3hil_single_step(struct v3hil *h);

#endif
