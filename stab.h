/* MSPDebug - debugging tool for the eZ430
 * Copyright (C) 2009 Daniel Beer
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

#ifndef STAB_H_
#define STAB_H_

#include <sys/types.h>

/* Reset the symbol table (delete all symbols) */
void stab_clear(void);

/* Add a block of text to the string table. On success, returns the new
 * size of the string table. Returns -1 on error. You can fetch the table's
 * current size by calling stab_add_string(NULL, 0);
 */
int stab_add_string(const char *text, int len);

/* Symbol types. Symbols are divided into a fixed number of classes for
 * query purposes.
 */
#define STAB_TYPE_CODE		0x01
#define STAB_TYPE_DATA		0x02
#define STAB_TYPE_ALL		0x03

/* Add a symbol to the table. The name is specified as an offset into
 * the string table.
 *
 * Returns 0 on success, or -1 if an error occurs.
 */
int stab_add_symbol(int name, u_int16_t addr);

/* Parse a symbol name and return an address. The text may be an address,
 * a symbol name or a combination (using + or -).
 *
 * Returns 0 if parsed successfully, -1 if an error occurs.
 */
int stab_parse(const char *text, int *addr);

/* Take an address and find the nearest symbol. */
int stab_find(u_int16_t *addr, const char **name);

#endif
