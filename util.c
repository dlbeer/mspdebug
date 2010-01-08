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

#include <stdio.h>
#include "util.h"

void hexdump(int addr, const u_int8_t *data, int len)
{
	int offset = 0;

	while (offset < len) {
		int i, j;

		/* Address label */
		printf("    %04x:", offset + addr);

		/* Hex portion */
		for (i = 0; i < 16 && offset + i < len; i++)
			printf(" %02x", data[offset + i]);
		for (j = i; j < 16; j++)
			printf("   ");

		/* Printable characters */
		printf(" |");
		for (j = 0; j < i; j++) {
			int c = data[offset + j];

			printf("%c", (c >= 32 && c <= 126) ? c : '.');
		}
		for (; j < 16; j++)
			printf(" ");
		printf("|\n");

		offset += i;
	}
}
