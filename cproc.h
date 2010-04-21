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

#ifndef CPROC_H_
#define CPROC_H_

/* Command processor.
 *
 * This contains a list of all defined commands and options, plus modification
 * flags.
 */
struct cproc;
typedef struct cproc *cproc_t;

/* Command definitions.
 *
 * Each command is specified as a tuple of name, command function and help
 * text. When the command is invoked, the command function is called with
 * a mutable pointer to the command line text, and a pointer to the command
 * processor.
 */
typedef int (*cproc_command_func_t)(cproc_t cp, char **arg);

struct cproc_command {
	const char              *name;
	cproc_command_func_t    func;
	const char              *help;
};

/* Option definitions.
 *
 * Options come in three different types, and can be manipulated with the
 * in-built "set" function.
 *
 * Default values may be specified by filling out the value portion in the
 * option's definition.
 */
typedef enum {
	CPROC_OPTION_STRING,
	CPROC_OPTION_NUMERIC,
	CPROC_OPTION_BOOL
} cproc_option_type_t;

struct cproc_option {
	const char              *name;
	cproc_option_type_t     type;
	const char              *help;

	union {
		char                    text[128];
		int                     numeric;
	}                       data;
};

/* Commmand processor modification flags.
 *
 * Within the context of a command processor, various data items may be
 * marked as having been modified. These flags can be checked, and a prompt
 * invoked to ask the user to confirm before proceeding with a destructive
 * operation.
 *
 * The same prompting occurs when the user elects to quit the command
 * processor.
 */

#define CPROC_MODIFY_SYMS       0x01

/* Create/destroy a command processor. The init function returns 0 if
 * successful, or -1 if an error occurs.
 */
cproc_t cproc_new(void);
void cproc_destroy(cproc_t cp);

/* Register commands and options with the command processor. These functions
 * return 0 on success or -1 if an error occurs (failure to allocate memory).
 */
int cproc_register_commands(cproc_t cp, const struct cproc_command *cmd,
			    int count);
int cproc_register_options(cproc_t cp, const struct cproc_option *opt,
			   int count);

/* Print a line of text on the command processor's standard output.
 *
 * ANSI colour codes can be embedded in the output text, and will be stripped
 * out if colour is disabled.
 */
void cproc_printf(cproc_t cp, const char *fmt, ...);

/* This should be called before a destructive operation to give the user
 * a chance to abort. If it returns 1, then the operation should be aborted.
 *
 * The flags argument should be a bitwise combination representing the bits
 * modify_flags that will be affected by the operation.
 */
void cproc_modify(cproc_t cp, int flags);
void cproc_unmodify(cproc_t cp, int flags);
int cproc_prompt_abort(cproc_t cp, int flags);

/* Retrieve option values. These functions return 0 on success or -1 if
 * an error occurs.
 */
int cproc_get_int(cproc_t cp, const char *opt, int *value);
int cproc_get_string(cproc_t cp, const char *opt, char *value, int max_len);

/* Run the reader loop */
void cproc_reader_loop(cproc_t cp);

/* Commands can be fed directly to the processor either one at a time,
 * or by specifying a file to read from.
 */
int cproc_process_command(cproc_t cp, char *cmd);
int cproc_process_file(cproc_t cp, const char *filename);

#endif
