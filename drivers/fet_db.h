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

#ifndef FET_DB_H_
#define FET_DB_H_

#include <stdint.h>

#define FET_DB_MSG28_LEN        0x12
#define FET_DB_MSG29_PARAMS     3
#define FET_DB_MSG29_LEN        0x4a
#define FET_DB_MSG2B_LEN        0x4a

struct fet_db_record {
	const char      *name;

	uint8_t         msg28_data[FET_DB_MSG28_LEN];
	int             msg29_params[FET_DB_MSG29_PARAMS];
	uint8_t         msg29_data[FET_DB_MSG29_LEN];
	uint8_t         msg2b_data[FET_DB_MSG2B_LEN];
	int             msg2b_len;
};

/* Find a record in the database by its response to message 0x28. The
 * first two bytes _must_ match, and the remaining bytes should match
 * as much as possible.
 */
const struct fet_db_record *fet_db_find_by_msg28(uint8_t *data, int len);

/* Find a record in the database by name. The search is case-insensitive.
 */
const struct fet_db_record *fet_db_find_by_name(const char *name);

/* Call the given enumeration function for all records in the database.
 */
typedef int (*fet_db_enum_func_t)(void *user_data,
				  const struct fet_db_record *rec);

int fet_db_enum(fet_db_enum_func_t func, void *user_data);

#endif
