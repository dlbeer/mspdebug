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

#ifndef OPDB_H_
#define OPDB_H_

#include "util.h"

typedef enum {
	OPDB_TYPE_BOOLEAN,
	OPDB_TYPE_NUMERIC,
	OPDB_TYPE_STRING
} opdb_type_t;

union opdb_value {
	char            string[128];
	address_t       numeric;
	int             boolean;
};

struct opdb_key {
	const char		*name;
	const char		*help;
	opdb_type_t		type;
	union opdb_value	defval;
};

/* Reset all options to default values. This should be called on start-up
 * to initialize the database.
 */
void opdb_reset(void);

/* Enumerate all option key/value pairs */
typedef int (*opdb_enum_func_t)(void *user_data, const struct opdb_key *key,
				const union opdb_value *value);

int opdb_enum(opdb_enum_func_t func, void *user_data);

/* Retrieve information about an option. Returns 0 if found, -1 otherwise. */
int opdb_get(const char *name, struct opdb_key *key,
	     union opdb_value *value);

int opdb_set(const char *name, const union opdb_value *value);

/* Get wrappers */
const char *opdb_get_string(const char *name);
int opdb_get_boolean(const char *name);
address_t opdb_get_numeric(const char *name);

/* Check flash unlock bits, as configured by the user */
typedef enum {
	FPERM_LOCKED_FLASH = 0x01,
	FPERM_BSL = 0x02
} fperm_t;

fperm_t opdb_read_fperm(void);

#endif
