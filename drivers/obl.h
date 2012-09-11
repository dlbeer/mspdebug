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

#ifndef OBL_H_
#define OBL_H_

#include "transport.h"

/* Fetch the version of installed Olimex firmware. Returns 0 on success
 * or -1 if an error occurs.
 */
int obl_get_version(transport_t trans, uint32_t *ver_ret);

/* Perform a firmware update using the given image file. */
int obl_update(transport_t trans, const char *image_filename);

/* Perform a device reset. This will return almost immediately, but it
 * will take 15 seconds for the reset to complete. During this time, the
 * underlying device will disappear and reappear, so it must be
 * reopened.
 */
int obl_reset(transport_t trans);

#endif
