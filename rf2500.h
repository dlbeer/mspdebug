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

#ifndef RF2500_H_
#define RF2500_H_

#include "transport.h"

/* Search the USB bus for the first eZ430-RF2500, and initialize it. If
 * successful, 0 is returned and the fet_* functions are ready for use.
 * If an error occurs, -1 is returned.
 *
 * A particular device may be specified in bus:dev form.
 */
transport_t rf2500_open(const char *dev_path, const char *requested_serial);

#endif
