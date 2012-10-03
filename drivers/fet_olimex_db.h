/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2012 Daniel Beer
 * Copyright (C) 2012 Stanimir Bonev
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

#ifndef FET_OLIMEX_DB_H_
#define FET_OLIMEX_DB_H_

#include <stdint.h>
#include "devicelist.h"

#define FET_OLIMEX_DB_MSG28_LEN        0x12
#define FET_OLIMEX_DB_MSG29_PARAMS     3
#define FET_OLIMEX_DB_MSG29_LEN        0x4a
#define FET_OLIMEX_DB_MSG2B_LEN        0x4a

struct fet_olimex_db_record {
	const char      *name;

	int             msg29_params[FET_OLIMEX_DB_MSG29_PARAMS];
	uint8_t         msg29_data[FET_OLIMEX_DB_MSG29_LEN];
	uint8_t         msg2b_data[FET_OLIMEX_DB_MSG2B_LEN];
	int             msg2b_len;
};

/* Find a record in the database by name. The search is case-insensitive.
 *
 * Returns a device index on success or -1 if the device could not be
 * found.
 */
int fet_olimex_db_find_by_name(const char *name);

/* Call the given enumeration function for all records in the database.
 *
 * If the callback returns -1, enumeration is aborted and the enumerator
 * function returns -1. Otherwise, 0 is returned.
 */
typedef int (*fet_olimex_db_enum_func_t)(void *user_data, const char *name);

int fet_olimex_db_enum(fet_olimex_db_enum_func_t func, void *user_data);

/* Find suitable device index. Given 9 bytes of identification data, return
 * the device index, or -1 if the device can't be identified.
 */
int fet_olimex_db_identify(const uint8_t *data);

/* Convert a device index to a device type. */
devicetype_t fet_olimex_db_index_to_type(int index);

/* Return configuration data for a given device type.
 */
const struct fet_olimex_db_record *fet_db_get_record(devicetype_t type);

#endif
