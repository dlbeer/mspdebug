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

#ifndef PROG_H_
#define PROG_H_

#include "binfile.h"

#define PROG_BUFSIZE    4096

struct prog_data {
	char		section[64];

	uint8_t         buf[PROG_BUFSIZE];
	address_t       addr;
	int             len;

	int		flags;
	int             have_erased;

	address_t	total_written;
};

#define PROG_WANT_ERASE		0x01
#define PROG_VERIFY		0x02

void prog_init(struct prog_data *data, int flags);
int prog_feed(struct prog_data *data, const struct binfile_chunk *ch);
int prog_flush(struct prog_data *data);

#endif
