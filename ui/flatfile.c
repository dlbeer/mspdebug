/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2012 Daniel Beer
 * Copyright (C) 2012 Ingo van Lil
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

#include "flatfile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "device.h"
#include "expr.h"
#include "output_util.h"

enum operation {
	LOAD,
	SAVE,
	VERIFY
};

static int read_flatfile(const char *path, uint8_t **buf, address_t *len)
{
	FILE *in;
	char *fullpath = expand_tilde(path);
	size_t count;

	if (!fullpath)
		return -1;

	in = fopen(fullpath, "rb");
	free(fullpath);

	if (!in) {
		printc_err("%s: %s\n", path, last_error());
		return -1;
	}

	if (fseek(in, 0, SEEK_END) < 0) {
		printc_err("%s: can't seek to end: %s\n", path, last_error());
		fclose(in);
		return -1;
	}

	*len = ftell(in);
	rewind(in);

	if (!*len) {
		*buf = malloc(1);
		count = 1;
	} else {
		*buf = malloc(*len);
		if (!*buf) {
			printc_err("flatfile: can't allocate memory\n");
			fclose(in);
			return -1;
		}

		count = fread(*buf, *len, 1, in);
	}

	fclose(in);

	if (count != 1) {
		printc_err("%s: failed to read: %s\n", path, last_error());
		free(*buf);
		*buf = NULL;
		return -1;
	}
	return 0;
}

static int write_flatfile(const char *path, uint8_t *buf, address_t len)
{
	FILE *out;
	char *fullpath = expand_tilde(path);
	const char *errmsg = NULL;

	if (!fullpath)
		return -1;

	out = fopen(fullpath, "wb");
	free(fullpath);

	if (!out) {
		printc_err("%s: %s\n", path, last_error());
		return -1;
	}

	if (len && (fwrite(buf, len, 1, out) != 1))
		errmsg = last_error();

	if (fclose(out) != 0 && !errmsg)
		errmsg = last_error();

	if (errmsg) {
		printc_err("%s: failed to write: %s\n", path, errmsg);
		return -1;
	}
	return 0;
}

static int do_flatfile(enum operation op, const char *path, address_t addr, address_t len)
{
	uint8_t *in_buf = NULL;
	uint8_t *out_buf = NULL;

	int ret = -1;

	if (op == LOAD || op == VERIFY) {
		ret = read_flatfile(path, &in_buf, &len);
		if (ret != 0)
			goto out;
	}

	if (device_ctl(DEVICE_CTL_HALT) < 0)
		goto out;

	if (op == LOAD) {
		if (device_writemem(addr, in_buf, len) != 0)
			goto out;
	} else {
		out_buf = malloc(len);
		if (!out_buf) {
			printc_err("flatfile: can't allocate memory\n");
			goto out;
		}
		if (device_readmem(addr, out_buf, len) != 0)
			goto out;
	}

	if (device_ctl(DEVICE_CTL_RESET) < 0)
		printc_err("warning: flatfile: "
			   "failed to reset after programming\n");

	if (op == VERIFY) {
		int i;
		for (i = 0; i < len; i++) {
			if (out_buf[i] != in_buf[i]) {
				printc("\x1b[1mERROR:\x1b[0m "
				       "mismatch at %04x (read %02x, "
				       "expected %02x)\n",
				       addr + i,
				       out_buf[i], in_buf[i]);
				goto out;
			}
		}
		ret = 0;
	} else if (op == SAVE) {
		ret = write_flatfile(path, out_buf, len);
	}

	if (ret == 0)
		printc("Done, %d bytes total\n", len);

out:
	free(in_buf);
	free(out_buf);
	return ret;
}

int cmd_load_raw(char **arg)
{
	const char *path, *addr_text;
	address_t addr;

	path = get_arg(arg);
	if (!path) {
		printc_err("load_raw: need file name argument\n");
		return -1;
	}

	addr_text = get_arg(arg);
	if (!addr_text) {
		printc_err("load_raw: need flash address argument\n");
		return -1;
	} else if (expr_eval(addr_text, &addr) < 0) {
		return -1;
	}

	return do_flatfile(LOAD, path, addr, 0);
}

int cmd_verify_raw(char **arg)
{
	const char *path, *addr_text;
	address_t addr;

	path = get_arg(arg);
	if (!path) {
		printc_err("verify_raw: need file name argument\n");
		return -1;
	}

	addr_text = get_arg(arg);
	if (!addr_text) {
		printc_err("verify_raw: need flash address argument\n");
		return -1;
	} else if (expr_eval(addr_text, &addr) < 0) {
		return -1;
	}

	return do_flatfile(VERIFY, path, addr, 0);
}

int cmd_save_raw(char **arg)
{
	const char *addr_text, *len_text, *path;
	address_t addr, len;

	addr_text = get_arg(arg);
	if (!addr_text) {
		printc_err("save_raw: need flash address argument\n");
		return -1;
	} else if (expr_eval(addr_text, &addr) < 0) {
		return -1;
	}

	len_text = get_arg(arg);
	if (!len_text) {
		printc_err("save_raw: need length argument\n");
		return -1;
	} else  if (expr_eval(len_text, &len) < 0)
		return -1;

	path = get_arg(arg);
	if (!path) {
		printc_err("save_raw: need file name argument\n");
		return -1;
	}

	return do_flatfile(SAVE, path, addr, len);
}
