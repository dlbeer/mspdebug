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

#include "util.h"
#include "binfile.h"
#include "ihex.h"
#include "elf32.h"
#include "symmap.h"
#include "titext.h"
#include "srec.h"
#include "coff.h"
#include "output.h"

struct file_format {
	int (*check)(FILE *in);
	int (*extract)(FILE *in, binfile_imgcb_t cb, void *user_data);
	int (*syms)(FILE *in);
};

static const struct file_format formats[] = {
	{
		.check = elf32_check,
		.extract = elf32_extract,
		.syms = elf32_syms
	},
	{
		.check = ihex_check,
		.extract = ihex_extract
	},
	{
		.check = symmap_check,
		.syms = symmap_syms
	},
	{
		.check = titext_check,
		.extract = titext_extract
	},
	{
		.check = srec_check,
		.extract = srec_extract
	},
	{
		.check = coff_check,
		.extract = coff_extract,
		.syms = coff_syms
	}
};

static const struct file_format *identify(FILE *in)
{
	int i;

	for (i = 0; i < ARRAY_LEN(formats); i++) {
		const struct file_format *f = &formats[i];

		if (f->check(in) > 0)
			return f;
	}

	return NULL;
}

int binfile_info(FILE *in)
{
	const struct file_format *fmt = identify(in);
	int flags = 0;

	if (fmt) {
		if (fmt->extract)
			flags |= BINFILE_HAS_TEXT;
		if (fmt->syms)
			flags |= BINFILE_HAS_SYMS;
	}

	return flags;
}

int binfile_extract(FILE *in, binfile_imgcb_t cb, void *user_data)
{
	const struct file_format *fmt = identify(in);

	if (!fmt) {
		printc_err("binfile: unknown file format\n");
		return -1;
	}

	if (!fmt->extract) {
		printc_err("binfile: this format contains no code\n");
		return -1;
	}

	return fmt->extract(in, cb, user_data);
}

int binfile_syms(FILE *in)
{
	const struct file_format *fmt = identify(in);

	if (!fmt) {
		printc_err("binfile: unknown file format\n");
		return -1;
	}

	if (!fmt->syms) {
		printc_err("binfile: this format contains no symbols\n");
		return -1;
	}

	return fmt->syms(in);
}
