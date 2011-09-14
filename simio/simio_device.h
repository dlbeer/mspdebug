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

#ifndef SIMIO_DEVICE_H_
#define SIMIO_DEVICE_H_

#include <stdint.h>
#include "util.h"
#include "list.h"

/* Each system clock has a unique index. After each instruction, step()
 * is invoked on each device with an array of clock transition counts.
 */
typedef enum {
	SIMIO_MCLK = 0,
	SIMIO_SMCLK,
	SIMIO_ACLK,
	SIMIO_NUM_CLOCKS
} simio_clock_t;

/* Access to special function registers is provided by these functions. The
 * modify function does:
 *
 *     SFR = (SFR & ~mask) | bits
 */
#define SIMIO_IE1		0x00
#define SIMIO_IFG1		0x01
#define SIMIO_IE2		0x02
#define SIMIO_IFG2		0x03

uint8_t simio_sfr_get(address_t which);
void simio_sfr_modify(address_t which, uint8_t mask, uint8_t bits);

struct simio_class;

/* Device base class.
 *
 * The node and name fields will be filled out by the IO simulator - they're
 * used for keeping track of the device list. The node member MUST be the
 * first in the struct.
 */
struct simio_device {
	struct list_node		node;

	char				name[64];
	const struct simio_class	*type;
};

struct simio_class {
	const char          *name;
	const char          *help;

	/* Instantiate a new device, with the given arguments. This may
	 * fail, in which case NULL should be returned.
	 */
	struct simio_device *(*create)(char **arg_text);
	void (*destroy)(struct simio_device *dev);

	/* These methods are invoked via the command interface to modify
	 * device data and show status.
	 */
	int (*config)(struct simio_device *dev, const char *param,
		      char **arg_text);
	int (*info)(struct simio_device *dev);

	/* System reset hook. */
	void (*reset)(struct simio_device *dev);

	/* Programmed IO functions return 1 to indicate an unhandled
	 * request. This scheme allows stacking.
	 */
	int (*write)(struct simio_device *dev,
		     address_t addr, uint16_t data);
	int (*read)(struct simio_device *dev,
		    address_t addr, uint16_t *data);
	int (*write_b)(struct simio_device *dev,
		       address_t addr, uint8_t data);
	int (*read_b)(struct simio_device *dev,
		      address_t addr, uint8_t *data);

	/* Check and acknowledge interrupts. Each device may produce
	 * a single interrupt request at any time.
	 */
	int (*check_interrupt)(struct simio_device *dev);
	void (*ack_interrupt)(struct simio_device *dev, int irq);

	/* Run the clocks for this device. The counters array has one
	 * array per clock, and gives the number of cycles elapsed since
	 * the last call to this method.
	 */
	void (*step)(struct simio_device *dev,
		     uint16_t status_register, const int *clocks);
};

#endif
