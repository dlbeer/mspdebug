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

#ifndef PARSE_H_
#define PARSE_H_

#include <sys/types.h>

typedef int (*command_func_t)(char **arg);

struct command {
	const char	*name;
	int		(*func)(char **arg);
	const char	*help;
};

/* The global command table, defined in main.c */
extern const struct command all_commands[];

/* Retrieve the next word from a pointer to the rest of a command
 * argument buffer. Returns NULL if no more words.
 */
char *get_arg(char **text);

/* Process a command, returning -1 on error or 0 if executed
 * successfully.
 *
 * The interactive argument specifies whether or not the command
 * should be executed in an interactive context.
 */
int process_command(char *arg, int interactive);

/* Run the reader loop, exiting when the user presses Ctrl+D.
 *
 * Commands executed by the reader loop are executed in interactive
 * context.
 */
void reader_loop(void);

/* Help command. Displays a command list with no argument, or help
 * for a particular command.
 */
int cmd_help(char **arg);

/* Colourized output has been requested by the user. */
int colorize(const char *text);

/* Return non-zero if executing in an interactive context. */
int is_interactive(void);

/* Parse an address expression, storing the result in the integer
 * pointed to. Returns 0 if parsed successfully, -1 if not.
 */
int addr_exp(const char *text, int *value);

/* Options interface. Options may be declared by any module and
 * registered with the parser.
 *
 * They can then be manipulated by the "set" command (function
 * declared below.
 */

typedef enum {
	OPTION_BOOLEAN,
	OPTION_NUMERIC,
	OPTION_TEXT
} option_type_t;

struct option {
	const char      *name;
	option_type_t   type;
	const char      *help;

	union {
		char            text[128];
		int             numeric;
	}               data;

	struct option   *next;
};

/* Add an option to the parser's list. Options can't be removed from
 * the list once added.
 */
void register_option(struct option *o);

/* Command function for settings options. With no arguments, displays
 * a list of available options.
 */
int cmd_opt(char **arg);

/* Initialise the parser, and register built-ins. */
void parse_init(void);

void hexdump(int addr, const u_int8_t *data, int len);

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

void print_devid(const u_int16_t id);

int open_serial(const char *device, int rate);
int read_with_timeout(int fd, u_int8_t *data, int len);
int write_all(int fd, const u_int8_t *data, int len);

void ctrlc_reset(void);
int ctrlc_check(void);

#endif
