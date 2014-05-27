/* MSPDebug - debugging tool for MSP430 MCUs
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "opdb.h"
#include "stab.h"

/* Buffer in which the demangler result will be constructed. */
struct dmbuf {
	char		*out;
	size_t		max_len;
	size_t		len;
};

/* Add a chunk of text to the buffer */
static int dm_append(struct dmbuf *d, const char *text, size_t len)
{
	size_t i;

	if (d->len + len + 1 > d->max_len)
		return -1;

	for (i = 0; i < len; i++) {
		if (!text[i])
			return -1;

		d->out[d->len++] = text[i];
	}

	d->out[d->len] = 0;
	return 0;
}

/* Demangle a single component (possibly part of a nested name). */
static int dm_component(struct dmbuf *d, const char *text, const char **out)
{
	char *next;
	unsigned int len = strtoul(text, &next, 10);

	if (next == text)
		return -1;

	if (dm_append(d, next, len) < 0)
		return -1;

	if (out)
		*out = next + len;

	return 0;
}

/* Demangler interface */
int demangle(const char *raw, char *out, size_t max_len)
{
	struct dmbuf d;

	d.out = out;
	d.max_len = max_len;
	d.len = 0;

	if (*raw != '_' || raw[1] != 'Z')
		return -1;
	raw += 2;

	if (*raw == 'N') {
		const char *next;

		/* Skip CV qualifiers */
		raw++;
		while (*raw == 'v' || *raw == 'V' || *raw == 'K')
			raw++;

		next = raw;

		while (*next != 'C' && *next != 'D' && *next != 'E') {
			const char *comp = next;

			if (d.len > 0 && dm_append(&d, "::", 2) < 0)
				return -1;
			if (dm_component(&d, comp, &next) < 0)
				return -1;

			if (*next == 'C' || *next == 'D') {
				/* Constructor/Destructor */
				if (dm_append(&d, "::", 2) < 0)
					return -1;
				if (*next == 'D' && dm_append(&d, "~", 1) < 0)
					return -1;
				if (dm_component(&d, comp, NULL) < 0)
					return -1;
			}
		}
	} else {
		if (dm_component(&d, raw, NULL) < 0)
			return -1;
	}

	return d.len;
}
