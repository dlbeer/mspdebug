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
#include "titext.h"
#include "util.h"
#include "output.h"

static int is_address_line(const char *text)
{
	if (*text != '@')
		return 0;

	text++;
	if (!*text || isspace(*text))
		return 0;

	while (*text && !isspace(*text)) {
		if (!ishex(*text))
			return 0;
		text++;
	}

	while (*text) {
		if (!isspace(*text))
			return 0;
		text++;
	}

	return 1;
}

static int is_data_line(const char *text)
{
	while (*text) {
		if (!(ishex(*text) || isspace(*text)))
			return 0;
		text++;
	}

	return 1;
}

int titext_check(FILE *in)
{
	char buf[64];

	rewind(in);
	if (!fgets(buf, sizeof(buf), in))
		return 0;

	return is_address_line(buf);
}

static int process_data_line(address_t address, const char *buf,
			     binfile_imgcb_t cb, void *user_data)
{
	uint8_t data[64];
	int data_len = 0;
	int value = 0;
	int vc = 0;
	struct binfile_chunk ch = {0};

	while (*buf) {
		int c = *(buf++);
		int x;

		if (isspace(c)) {
			if (vc) {
				if (data_len >= sizeof(data))
					goto too_long;
				data[data_len++] = value;
			}

			vc = 0;
		} else {
			if (isdigit(c)) {
				x = c - '0';
			} else if (c >= 'A' && c <= 'F') {
				x = c - 'A' + 10;
			} else if (c >= 'a' && c <= 'f') {
				x = c - 'a' + 10;
			} else  {
				printc_err("titext: unexpected "
					"character: %c\n", c);
				return -1;
			}

			if (vc >= 2) {
				printc_err("titext: too many digits "
					"in hex value\n");
				return -1;
			}

			value = (value << 4) | x;
			vc++;
		}
	}

	if (vc) {
		if (data_len >= sizeof(data))
			goto too_long;
		data[data_len++] = value;
	}

	ch.addr = address;
	ch.data = data;
	ch.len = data_len;
	if (cb(user_data, &ch) < 0)
		return -1;

	return data_len;

 too_long:
	printc_err("titext: too many data bytes\n");
	return -1;
}

int titext_extract(FILE *in, binfile_imgcb_t cb, void *user_data)
{
	address_t address = 0;
	int lno = 0;
	char buf[128];

	rewind(in);
	while (fgets(buf, sizeof(buf), in)) {
		lno++;

		if (is_address_line(buf)) {
			address = strtoul(buf + 1, NULL, 16);
		} else if (is_data_line(buf)) {
			int count = process_data_line(address, buf,
						      cb, user_data);
			if (count < 0) {
				printc_err("titext: data error on line "
					"%d\n", lno);
				return -1;
			}

			address += count;
		}
	}

	return 0;
}
