/* MSPDebug - debugging tool for the eZ430
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

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "symmap.h"

int symmap_check(FILE *in)
{
	char buf[128];
	int i;
	int spc_count = 0;

	rewind(in);
	if (!fgets(buf, sizeof(buf), in))
		return 0;

	for (i = 0; buf[i]; i++) {
		if (buf[i] == '\r' || buf[i] == '\n')
			break;

		if (buf[i] < 32 || buf[i] > 126)
			return 0;

		if (isspace(buf[i]))
			spc_count++;
	}

	return spc_count >= 2;
}

int symmap_syms(FILE *in)
{
	rewind(in);
	char buf[128];

	while (fgets(buf, sizeof(buf), in)) {
		char *addr = strtok(buf, " \t\r\n");
		char *name;

		strtok(NULL, " \t\r\n");
		name = strtok(NULL, " \t\r\n");

		if (addr && name) {
			address_t addr_val = strtoul(addr, NULL, 16);

			if (stab_set(name, addr_val) < 0)
				return -1;
		}
	}

	return 0;
}
