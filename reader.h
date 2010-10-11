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

#ifndef READER_H_
#define READER_H_

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

#define MODIFY_SYMS       0x01

void mark_modified(int flags);
void unmark_modified(int flags);

/* This should be called before a destructive operation to give the user
 * a chance to abort. If it returns 1, then the operation should be aborted.
 *
 * The flags argument should be a bitwise combination representing the bits
 * modify_flags that will be affected by the operation.
 */
int prompt_abort(int flags);

/* Run the reader loop */
void reader_loop(void);

/* Cause the reader loop to exit */
void reader_exit(void);

/* Set up the command to be repeated. When the user presses enter without
 * typing anything, the last executed command is repeated, by default.
 *
 * Using this function, a command can specify an alternate command for
 * the next execution.
 */
void reader_set_repeat(const char *fmt, ...);

/* Commands can be fed directly to the processor either one at a time,
 * or by specifying a file to read from.
 *
 * If show is non-zero, commands will be printed as they are executed.
 */
int process_command(char *cmd);
int process_file(const char *filename, int show);

#endif
