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
#include <string.h>
#include "device.h"
#include "util.h"

/* This table of device IDs is sourced mainly from the MSP430 Memory
 * Programming User's Guide (SLAU265).
 *
 * The table should be kept sorted by device ID.
 */

static struct {
	u_int16_t	id;
	const char	*id_text;
} id_table[] = {
	{0x1132, "F1122"},
	{0x1132, "F1132"},
	{0x1232, "F1222"},
	{0x1232, "F1232"},
	{0xF112, "F11x"},   /* obsolete */
	{0xF112, "F11x1"},  /* obsolete */
	{0xF112, "F11x1A"}, /* obsolete */
	{0xF123, "F122"},
	{0xF123, "F123x"},
	{0xF143, "F14x"},
	{0xF149, "F13x"},
	{0xF149, "F14x1"},
	{0xF149, "F149"},
	{0xF169, "F16x"},
	{0xF16C, "F161x"},
	{0xF201, "F20x3"},
	{0xF213, "F21x1"},
	{0xF227, "F22xx"},
	{0xF249, "F24x"},
	{0xF26F, "F261x"},
	{0xF413, "F41x"},
	{0xF427, "FE42x"},
	{0xF427, "FW42x"},
	{0xF427, "F415"},
	{0xF427, "F417"},
	{0xF427, "F42x0"},
	{0xF439, "FG43x"},
	{0xF449, "F43x"},
	{0xF449, "F44x"},
	{0xF46F, "FG46xx"},
	{0xF46F, "F471xx"}
};

int device_id_text(u_int16_t id, char *out, int max_len)
{
	int i = 0;
	int len;

	while (i < ARRAY_LEN(id_table) && id_table[i].id != id)
		i++;

	if (i >= ARRAY_LEN(id_table))
		return -1;

	len = snprintf(out, max_len, "MSP430%s", id_table[i++].id_text);
	out += len;
	max_len -= len;

	while (id_table[i].id == id) {
		len = snprintf(out, max_len, "/MSP430%s", id_table[i++].id_text);
		out += len;
		max_len -= len;
	}

	return 0;
}
