/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2012 Daniel Beer
 * Copyright (C) 2012 Peter Bägel
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

/* jtag functions are taken from TIs SLAA149–September 2002
 *
 * 2012-10-03 Peter Bägel (DF5EQ)
 */

#ifndef JTAGLIB_H_
#define JTAGLIB_H_

#include <stdint.h>
#include "jtdev.h"
#include "util.h"

/*===== public symbols ====================================================== */

/* flash erasing modes */
#define JTAG_ERASE_MASS 0xA506
#define JTAG_ERASE_MAIN 0xA504
#define JTAG_ERASE_SGMT 0xA502

/*===== public functions =====================================================*/

unsigned int jtag_init           (struct jtdev *p);
unsigned int jtag_get_device     (struct jtdev *p);
unsigned int jtag_chip_id        (struct jtdev *p);
uint16_t     jtag_read_mem       (struct jtdev *p, unsigned int format,
				  address_t address);
void         jtag_read_mem_quick (struct jtdev *p, address_t start_address,
				  unsigned int word_count, uint16_t *data);
void         jtag_write_mem      (struct jtdev *p, unsigned int format,
				  address_t address, uint16_t data);
void         jtag_write_mem_quick(struct jtdev *p, address_t start_address,
				  unsigned int word_count,
				  const uint16_t *data);
int          jtag_is_fuse_blown  (struct jtdev *p);
unsigned int jtag_execute_puc    (struct jtdev *p);
void         jtag_release_device (struct jtdev *p, address_t address);
int          jtag_verify_mem     (struct jtdev *p, address_t start_address,
				  unsigned int word_count,
				  const uint16_t *data);
int          jtag_erase_check    (struct jtdev *p, address_t start_address,
				  unsigned int word_count);
void         jtag_write_flash    (struct jtdev *p, address_t start_address,
				  unsigned int word_count,
				  const uint16_t *data);
void         jtag_erase_flash    (struct jtdev *p, unsigned int erase_mode,
				  address_t erase_address);
address_t    jtag_read_reg       (struct jtdev *p, int reg);
void         jtag_write_reg      (struct jtdev *p, int reg, address_t value);

#endif
