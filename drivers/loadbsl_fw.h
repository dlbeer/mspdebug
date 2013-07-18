/* MSPDebug - debugging tool for the eZ430
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

#ifndef LOADBSL_FW_H_
#define LOADBSL_FW_H_

#include <stdint.h>

/* USB BSL firmware image */
struct loadbsl_fw {
	const uint8_t		*data;
	const uint32_t		size;
	const uint32_t		prog_addr;
	const uint32_t		entry_point;
};

extern const struct loadbsl_fw loadbsl_fw_usb5xx;

#endif
