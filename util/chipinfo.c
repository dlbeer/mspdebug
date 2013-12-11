/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2013 Daniel Beer
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

#include <stddef.h>
#include <string.h>
#include "chipinfo.h"
#include "../chipinfo.db"

static int is_match(const struct chipinfo_id *a,
		    const struct chipinfo_id *b,
		    const struct chipinfo_id *mask)
{
	if ((a->ver_id ^ b->ver_id) & mask->ver_id)
		return 0;
	if ((a->ver_sub_id ^ b->ver_sub_id) & mask->ver_sub_id)
		return 0;
	if ((a->revision ^ b->revision) & mask->revision)
		return 0;
	if ((a->fab ^ b->fab) & mask->fab)
		return 0;
	if ((a->self ^ b->self) & mask->self)
		return 0;
	if ((a->config ^ b->config) & mask->config)
		return 0;
	if ((a->fuses ^ b->fuses) & mask->fuses)
		return 0;

	return 1;
}

const struct chipinfo *chipinfo_find_by_id(const struct chipinfo_id *id)
{
	const struct chipinfo *i;

	for (i = chipinfo_db; i->name; i++)
		if (is_match(&i->id, id, &i->id_mask))
			return i;

	return NULL;
}

const struct chipinfo *chipinfo_find_by_name(const char *name)
{
	const struct chipinfo *i;

	for (i = chipinfo_db; i->name; i++)
		if (!strcasecmp(name, i->name))
			return i;

	return NULL;
}

const struct chipinfo_memory *chipinfo_find_mem_by_name
	(const struct chipinfo *info, const char *name)
{
	const struct chipinfo_memory *m;

	for (m = info->memory; m->name; m++)
		if (!strcasecmp(m->name, name))
			return m;

	return NULL;
}

const struct chipinfo_memory *chipinfo_find_mem_by_addr
	(const struct chipinfo *info, uint32_t offset)
{
	const struct chipinfo_memory *m;
	const struct chipinfo_memory *best = NULL;

	for (m = info->memory; m->name; m++) {
		if (!m->mapped)
			continue;

		if (m->offset + m->size <= offset)
			continue;

		if (!best || (m->offset < best->offset))
			best = m;
	}

	return best;
}

const char *chipinfo_copyright(void)
{
	return "Chip info database from MSP430.dll v"
		CI_DLL430_VERSION_STRING " Copyright (C) 2013 TI, Inc.\n";
}
