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

#include <ctype.h>
#include <stdlib.h>
#include "srec.h"
#include "util.h"
#include "output.h"

int srec_check(FILE *in)
{
	char buf[128];
	int i;

	rewind(in);
	if (!fgets(buf, sizeof(buf), in))
		return 0;

	if (buf[0] != 'S')
		return 0;

	for (i = 1; buf[i] && !isspace(buf[i]); i++)
		if (!ishex(buf[i]))
			return 0;

	for (; buf[i]; i++)
		if (!isspace(buf[i]))
			return 0;

	return 1;
}

int srec_extract(FILE *in, binfile_imgcb_t cb, void *user_data)
{
	char buf[128];
	int lno = 0;

	rewind(in);
	while (fgets(buf, sizeof(buf), in)) {
		uint8_t bytes[128];
		uint8_t cksum = 0;
		int count = 0;
		int i;

		lno++;

		if (buf[0] != 'S') {
			printc_err("srec: garbage on line %d\n", lno);
			return -1;
		}

		for (i = 2; ishex(buf[i]) && ishex(buf[i + 1]); i += 2) {
			if (count >= sizeof(bytes)) {
				printc_err("srec: too many bytes on "
					"line %d\n", lno);
				return -1;
			}

			bytes[count++] = (hexval(buf[i]) << 4) |
				hexval(buf[i + 1]);
		}

		while (buf[i]) {
			if (!isspace(buf[i])) {
				printc_err("srec: trailing garbage on "
					"line %d\n", lno);
				return -1;
			}

			i++;
		}

		if (count < 2) {
			printc_err("srec: too few bytes on line %d\n",
				lno);
			return -1;
		}

		if (bytes[0] + 1 != count) {
			printc_err("srec: byte count mismatch on "
				"line %d\n", lno);
			return -1;
		}

		for (i = 0; i + 1 < count; i++)
			cksum += bytes[i];
		cksum = ~cksum;
		if (cksum != bytes[count - 1]) {
			printc_err("srec: checksum error on line %d "
				"(calc = 0x%02x, read = 0x%02x)\n",
				lno, cksum, bytes[count - 1]);
			return -1;
		}

		if (buf[1] >= '1' && buf[1] <= '3') {
			int addrbytes = buf[1] - '1' + 2;
			address_t addr = 0;
			struct binfile_chunk ch = {0};

			for (i = 0; i < addrbytes; i++)
				addr = (addr << 8) | bytes[i + 1];

			if (count < addrbytes + 2) {
				printc_err("srec: too few address bytes "
					"on line %d\n", lno);
				return -1;
			}

			ch.addr = addr;
			ch.data = bytes + addrbytes + 1;
			ch.len = count - 2 - addrbytes;

			if (cb(user_data, &ch) < 0) {
				printc_err("srec: error on line %d\n", lno);
				return -1;
			}
		}
	}

	return 0;
}
