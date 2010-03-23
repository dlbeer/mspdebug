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

#ifndef DEVICE_H_
#define DEVICE_H_

#include <sys/types.h>
#include "transport.h"

#define DEVICE_NUM_REGS		16

typedef enum {
	DEVICE_CTL_RESET,
	DEVICE_CTL_RUN,
	DEVICE_CTL_HALT,
	DEVICE_CTL_RUN_BP,
	DEVICE_CTL_STEP,
	DEVICE_CTL_ERASE
} device_ctl_t;

typedef enum {
	DEVICE_STATUS_HALTED,
	DEVICE_STATUS_RUNNING,
	DEVICE_STATUS_INTR,
	DEVICE_STATUS_ERROR
} device_status_t;

struct device {
	void (*close)(void);
	int (*control)(device_ctl_t action);
	device_status_t (*wait)(int blocking);
	int (*breakpoint)(u_int16_t addr);
	int (*getregs)(u_int16_t *regs);
	int (*setregs)(const u_int16_t *regs);
	int (*readmem)(u_int16_t addr, u_int8_t *mem, int len);
	int (*writemem)(u_int16_t addr, const u_int8_t *mem, int len);
};

/* MSP430 FET protocol implementation. */
#define FET_PROTO_SPYBIWIRE	0x01
#define FET_PROTO_RF2500	0x02

const struct device *fet_open(const struct fet_transport *transport,
			      int proto_flags, int vcc_mv);

/* MSP430 FET Bootloader implementation. */
const struct device *bsl_open(const char *device);

/* Dummy/simulation implementation. */
const struct device *sim_open(void);

#endif
