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

/* Driver for parallel port interface like the Olimex MSP430-JTAG
 * Starting point was the goodfet driver
 *
 * 2012-10-03 Peter Bägel (DF5EQ)
 */

#ifndef PIF_H_
#define PIF_H_

#include "device.h"

/* pif implementation */
extern const struct device_class device_pif;
/* share with gpio implementation */
extern const struct device_class device_gpio;
extern const struct device_class device_bp;
extern const struct device_class device_ftdi_bitbang;

#endif
