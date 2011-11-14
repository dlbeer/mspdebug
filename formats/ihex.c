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
#include "ihex.h"
#include "output.h"

int ihex_check(FILE *in)
{
	rewind(in);
	return fgetc(in) == ':';
}

static int feed_line(uint8_t *data, int nbytes, binfile_imgcb_t cb,
		     void *user_data, address_t *segment_offset)
{
	uint8_t cksum = 0;
	address_t address;
	uint8_t type;
	uint8_t *payload;
	int data_len;
	int i;
	struct binfile_chunk ch = {0};

	if (nbytes < 5)
		return 0;

	/* Verify checksum */
	for (i = 0; i + 1 < nbytes; i++)
		cksum += data[i];
	cksum = ~(cksum - 1) & 0xff;

	if (data[nbytes - 1] != cksum) {
		printc_err("ihex: invalid checksum: %02x "
			"(calculated %02x)\n", data[nbytes - 1], cksum);
		return -1;
	}

	/* Extract other bits */
	type = data[3];
	address = (data[1] << 8) | data[2];
	payload = data + 4;
	data_len = nbytes - 5;

	switch (type) {
	case 0:
		ch.addr = address + *segment_offset;
		ch.data = payload;
		ch.len = data_len;
		return cb(user_data, &ch);

	case 1:
	case 3:
		/* These can be safely ignored */
		break;

	case 2:
		if (data_len != 2) {
			printc_err("ihex: invalid 02 record\n");
			return -1;
		}

		*segment_offset = (address_t)((payload[0] << 8) |
						payload[1]) << 4;
		break;

	case 4:
		if (data_len != 2) {
			printc_err("ihex: invalid 04 record\n");
			return -1;
		}

		*segment_offset = (address_t)((payload[0] << 8) |
						payload[1]) << 16;
		break;

	default:
		printc_err("warning: ihex: unknown record type: "
			"0x%02x\n", type);
		break;
	}

	return 0;
}

int ihex_extract(FILE *in, binfile_imgcb_t cb, void *user_data)
{
	char buf[128];
	int lno = 0;
	address_t segment_offset = 0;

	rewind(in);
	while (fgets(buf, sizeof(buf), in)) {
		int len = strlen(buf);
		int i;
		uint8_t data[64];
		int nbytes;

		lno++;
		if (buf[0] != ':') {
			printc_err("ihex: line %d: invalid start "
				"marker\n", lno);
			continue;
		}

		/* Trim trailing whitespace */
		while (len && isspace(buf[len - 1]))
			len--;
		buf[len] = 0;

		/* Decode hex digits */
		nbytes = (len - 1) / 2;
		for (i = 0; i < nbytes; i++) {
			char d[] = {buf[i * 2 + 1], buf[i * 2 + 2], 0};

			data[i] = strtoul(d, NULL, 16);
		}

		/* Handle the line */
		if (feed_line(data, nbytes, cb, user_data,
			      &segment_offset) < 0) {
			printc_err("ihex: error on line %d\n", lno);
			return -1;
		}
	}

	return 0;
}
