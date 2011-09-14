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

#ifndef CMDDB_H_
#define CMDDB_H_

typedef int (*cmddb_func_t)(char **arg);

struct cmddb_record {
	const char		*name;
        cmddb_func_t            func;
	const char		*help;
};

/* Fetch a command record */
int cmddb_get(const char *name, struct cmddb_record *r);

/* Enumerate all command records.
 *
 * Returns 0, or -1 if an error occurs during enumeration.
 */
typedef int (*cmddb_enum_func_t)(void *user_data,
				 const struct cmddb_record *r);

int cmddb_enum(cmddb_enum_func_t func, void *user_data);

#endif
