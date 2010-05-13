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

#ifndef SIM_H_
#define SIM_H_

#include "device.h"

/* These function pointers should be supplied in order to allow
 * the simulator to perform IO operations. If they're left blank, IO
 * addresses just map to RAM.
 */
typedef int (*sim_fetch_func_t)(void *user_data,
				uint16_t pc, uint16_t addr,
				int is_byte, uint16_t *data);

typedef void (*sim_store_func_t)(void *user_data,
				 uint16_t pc, uint16_t addr,
				 int is_byte, uint16_t data);

/* Dummy/simulation implementation. */
device_t sim_open(sim_fetch_func_t fetch, sim_store_func_t store,
		  void *user_data);

#endif
