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

#ifndef SIMIO_CPU_H_
#define SIMIO_CPU_H_

/* This file describes the interface between the CPU simulator and the IO
 * simulator. It gives prototypes for functions which should be periodically
 * called by the CPU simulator.
 */

#include <stdint.h>
#include "util.h"

/* This function should be called when the CPU is reset, to also reset
 * the IO simulator.
 */
void simio_reset(void);

/* These functions should be called to perform programmed IO requests. A
 * return value of 0 indicates success, 1 is an unhandled request, and -1
 * is an error which should cause execution to stop.
 */
int simio_write(address_t addr, uint16_t data);
int simio_read(address_t addr, uint16_t *data);
int simio_write_b(address_t addr, uint8_t data);
int simio_read_b(address_t addr, uint8_t *data);

/* Check for an interrupt before executing an instruction. It returns -1 if
 * no interrupt is pending, otherwise the number of the highest priority
 * pending interrupt.
 */
int simio_check_interrupt(void);

/* When the CPU begins to handle an interrupt, it needs to notify the IO
 * simulation. Some interrupt flags are cleared automatically when handled.
 */
void simio_ack_interrupt(int irq);

/* This should be called after executing an instruction to advance the system
 * clocks.
 *
 * The status_register value should be the value of SR _before_ the
 * instruction was executed.
 */
void simio_step(uint16_t status_register, int cycles);

#endif
