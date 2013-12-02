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

#include <string.h>
#include "opdb.h"
#include "util.h"

static const struct opdb_key keys[] = {
	{
		.name = "color",
		.type = OPDB_TYPE_BOOLEAN,
		.help = "Colorize debugging output.\n",
		.defval = {
			.boolean = 0
		}
	},
	{
		.name = "gdb_loop",
		.type = OPDB_TYPE_BOOLEAN,
		.help =
"Automatically restart the GDB server after disconnection. If this\n"
"option is set, then the GDB server keeps running until an error occurs,\n"
"or the user interrupts with Ctrl+C.\n",
		.defval = {
			.boolean = 0
		}
	},
	{
		.name = "quiet",
		.type = OPDB_TYPE_BOOLEAN,
		.help = "Supress debugging output.\n",
		.defval = {
			.boolean = 0
		}
	},
	{
		.name = "iradix",
		.type = OPDB_TYPE_NUMERIC,
		.help = "Default input radix.\n",
		.defval = {
			.numeric = 10
		}
	},
	{
		.name = "fet_block_size",
		.type = OPDB_TYPE_NUMERIC,
		.help =
"Size of buffer used for memory transfers to and from the FET device.\n"
"Increasing this value will result in faster transfers, but may cause\n"
"problems with some chips.\n",
		.defval = {
			.numeric = 64
		}
	},
	{
		.name = "gdbc_xfer_size",
		.type = OPDB_TYPE_NUMERIC,
		.help =
"Maximum size of memory transfers for the GDB client. Increasing this\n"
"value will result in faster transfers, but may cause problems with some\n"
"servers.\n",
		.defval = {
			.numeric = 64
		}
	},
	{
		.name = "enable_locked_flash_access",
		.type = OPDB_TYPE_BOOLEAN,
		.help =
"If set, some drivers will allow erase/program access to the info A\n"
"segment. If in doubt, do not enable this.\n",
		.defval = {
			.boolean = 0
		}
	},
	{
		.name = "enable_bsl_access",
		.type = OPDB_TYPE_BOOLEAN,
		.help =
"If set, some drivers will allow erase/program access to flash\n"
"BSL memory. If in doubt, do not enable this.\n"
	},
	{
		.name = "gdb_default_port",
		.type = OPDB_TYPE_NUMERIC,
		.help =
"Default TCP port for GDB server, if no argument is given.\n",
		.defval = {
			.numeric = 2000
		}
	},
	{
		.name = "enable_fuse_blow",
		.type = OPDB_TYPE_BOOLEAN,
		.help =
"If set, some drivers will allow the JTAG security fuse to be blown.\n"
"\n"
"\x1b[1mWARNING: this is an irreversible operation!\x1b[0m\n"
"\n"
"If in doubt, do not enable this option.\n"
	}
};

static union opdb_value values[ARRAY_LEN(keys)];

static int opdb_find(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_LEN(keys); i++) {
		const struct opdb_key *key = &keys[i];

		if (!strcasecmp(key->name, name))
			return i;
	}

	return -1;
}

void opdb_reset(void)
{
	int i;

	for (i = 0; i < ARRAY_LEN(keys); i++) {
		const struct opdb_key *key = &keys[i];
		union opdb_value *value = &values[i];

		memcpy(value, &key->defval, sizeof(*value));
	}
}

int opdb_enum(opdb_enum_func_t func, void *user_data)
{
	int i;

	for (i = 0; i < ARRAY_LEN(keys); i++) {
		const struct opdb_key *key = &keys[i];
		const union opdb_value *value = &values[i];

		if (func(user_data, key, value) < 0)
			return -1;
	}

	return 0;
}

int opdb_get(const char *name, struct opdb_key *key,
	     union opdb_value *value)
{
	int i;

	i = opdb_find(name);
	if (i < 0)
		return -1;

	if (key)
		memcpy(key, &keys[i], sizeof(*key));
	if (value)
		memcpy(value, &values[i], sizeof(*value));

	return 0;
}

int opdb_set(const char *name, const union opdb_value *value)
{
	int i;
	union opdb_value *v;

	i = opdb_find(name);
	if (i < 0)
		return -1;

	v = &values[i];
	memcpy(v, value, sizeof(values[i]));
	if (keys[i].type == OPDB_TYPE_STRING)
		v->string[sizeof(v->string) - 1] = 0;

	return 0;
}

const char *opdb_get_string(const char *name)
{
	int idx = opdb_find(name);

	if (idx < 0)
		return "";

	return values[idx].string;
}

int opdb_get_boolean(const char *name)
{
	int idx = opdb_find(name);

	if (idx < 0)
		return 0;

	return values[idx].boolean;
}

address_t opdb_get_numeric(const char *name)
{
	int idx = opdb_find(name);

	if (idx < 0)
		return 0;

	return values[idx].numeric;
}

fperm_t opdb_read_fperm(void)
{
	fperm_t ret = 0;

	if (opdb_get_boolean("enable_locked_flash_access"))
		ret |= FPERM_LOCKED_FLASH;
	if (opdb_get_boolean("enable_bsl_access"))
		ret |= FPERM_BSL;

	return ret;
}
