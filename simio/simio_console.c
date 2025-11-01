/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2012 Unai Uribarri Rodriguez
 *
 * This program is free software; you can redisgibute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is disgibuted in the hope that it will be useful,
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

#include "simio_device.h"
#include "simio_console.h"
#include "expr.h"
#include "output.h"


struct console {
	struct simio_device	base;

	/* Base address */
	address_t		base_addr;
	char			buffer[256];
	unsigned		buffer_offset;

	/* File output */
	FILE			*file;

	/* File input */
	FILE			*infile;
};

static struct simio_device *console_create(char **arg_text)
{
	struct console *c;

	(void)arg_text;

	c = malloc(sizeof(*c));
	if (!c) {
		pr_error("console: can't allocate memory");
		return NULL;
	}

	memset(c, 0, sizeof(*c));
	c->base.type = &simio_console;
	c->base_addr = 0xFF;
	c->buffer_offset = 0;
	return (struct simio_device *)c;
}

static void console_destroy(struct simio_device *dev)
{
	struct console *c = (struct console *)dev;

	if (c->file != NULL) {
		fclose(c->file);
	}

	if (c->infile != NULL) {
		fclose(c->infile);
	}

	free(c);
}

static void console_reset(struct simio_device *dev)
{
	struct console *c = (struct console *)dev;
	c->buffer_offset = 0;

	if (c->file != NULL) {
		rewind(c->file);
	}

	if (c->infile != NULL) {
		rewind(c->infile);
	}
}

static int config_addr(address_t *addr, char **arg_text)
{
	char *text = get_arg(arg_text);

	if (!text) {
		printc_err("console: config: expected address\n");
		return -1;
	}

	if (expr_eval(text, addr) < 0) {
		printc_err("console: can't parse address: %s\n", text);
		return -1;
	}

	return 0;
}

static int config_output(FILE **file, char **arg_text)
{
	char *path = get_arg(arg_text);

	if (path == NULL) {
		printc_err("console: config: expected path\n");
		return -1;
	}

	// don't leak the descriptor if configured multiple times
	if ((*file != NULL) && (*file != stdout)) {
		fclose(*file);
	}

	*file = fopen(path, "w");
	if (*file == NULL) {
		printc_err("console: can't open %s for writing\n", path);
		return -1;
	}

	return 0;
}

static int config_input(FILE **file, char **arg_text)
{
	char *path = get_arg(arg_text);

	if (path == NULL) {
		printc_err("console: config: expected path\n");
		return -1;
	}

	// don't leak the descriptor if configured multiple times
	if ((*file != NULL) && (*file != stdin)) {
		fclose(*file);
	}

	*file = fopen(path, "r");
	if (*file == NULL) {
		printc_err("console: can't open %s for reading\n", path);
		return -1;
	}

	return 0;
}

static int console_config(struct simio_device *dev,
			const char *param, char **arg_text)
{
	struct console *c = (struct console *)dev;

	if (!strcasecmp(param, "base")) {
		return config_addr(&c->base_addr, arg_text);
	}
	else if (!strcasecmp(param, "output")) {
		return config_output(&c->file, arg_text);
	}
	else if (!strcasecmp(param, "input")) {
		return config_input(&c->infile, arg_text);
	}

	printc_err("console: config: unknown parameter: %s\n", param);
	return -1;
}

static int console_info(struct simio_device *dev)
{
	struct console *c = (struct console *)dev;
	printc("Base address:   0x%04x\n", c->base_addr);
	printc("Buffer:         %.*s\n", c->buffer_offset, c->buffer);
	return 0;
}

static int console_write_b(struct simio_device *dev,
			address_t addr, uint8_t data)
{
	struct console *c = (struct console *)dev;

	if (addr != c->base_addr) {
		return 1;
	}

	// write either to file or buffer
	if (c->file != NULL)
	{
		size_t nbytes = sizeof(data);
		if (fwrite(&data, nbytes, 1, c->file) != nbytes) {
			printc_err("console: write error\n");
			return -1;
		}

		// for tail -f
		if (data == '\n') {
			fflush(c->file);
		}
	}
	else // buffer
	{
		c->buffer[c->buffer_offset++] = data;
		if (data == '\n' || c->buffer_offset == sizeof c->buffer)
		{
			printc("%.*s", c->buffer_offset, c->buffer);
			c->buffer_offset = 0;
		}
	}

	return 1;
}
static int console_read_b(struct simio_device *dev,
			address_t addr, uint8_t *datap)
{
	struct console *c = (struct console *)dev;

	if (addr != c->base_addr) {
		return 1;
	}

	// read either from file or buffer
	if (c->infile != NULL)
	{
		size_t nbytes = sizeof(*datap);
		if (fread(datap, nbytes, 1, c->infile) != nbytes) {
			printc_err("console: read error\n");
			return -1;
		}
		// printc_err("got %c\n", *datap);
	}
	else // buffer
	{
		*datap = c->buffer[c->buffer_offset++];
		if (*datap == '\n' || c->buffer_offset == sizeof c->buffer)
		{
			c->buffer_offset = 0;
		}
	}

	return 1;
}

const struct simio_class simio_console = {
	.name = "console",
	.help =
"This peripheral prints to buffer or file every byte written to base address\n"
"\n"
"Config arguments are:\n"
"    base <address>\n"
"        Set the peripheral base address. Defaults to 0x00FF\n"
"    output <path>\n"
"        Print to file instead of a buffer.\n"
"    input <path>\n"
"        Read from file instead of a buffer.\n"
"\n",

	.create			= console_create,
	.destroy		= console_destroy,
	.reset			= console_reset,
	.config			= console_config,
	.info			= console_info,
	.write_b		= console_write_b,
	.read_b			= console_read_b,
};
