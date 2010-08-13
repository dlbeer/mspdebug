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

#include "cmddb.h"
#include "util.h"

#include "devcmd.h"
#include "gdb.h"
#include "rtools.h"
#include "sym.h"
#include "stdcmd.h"

const struct cmddb_record commands[] = {
	{
		.name = "help",
		.func = cmd_help,
		.help =
"help [command]\n"
"    Without arguments, displays a list of commands. With a command\n"
"    name as an argument, displays help for that command.\n"
	},
	{
		.name = "opt",
		.func = cmd_opt,
		.help =
"opt [name] [value]\n"
"    Query or set option variables. With no arguments, displays all\n"
"    available options.\n"
	},
	{
		.name = "read",
		.func = cmd_read,
		.help =
"read <filename>\n"
"    Read commands from a file and evaluate them.\n"
	},
	{
		.name = "setbreak",
		.func = cmd_setbreak,
		.help =
"setbreak <addr> [index]\n"
"    Set a breakpoint. If no index is specified, the first available\n"
"    slot will be used.\n"
	},
	{
		.name = "delbreak",
		.func = cmd_delbreak,
		.help =
"delbreak [index]\n"
"    Delete a breakpoint. If no index is specified, then all active\n"
"    breakpoints are cleared.\n"
	},
	{
		.name = "break",
		.func = cmd_break,
		.help =
"break\n"
"    List active breakpoints.\n"
	},
	{
		.name = "regs",
		.func = cmd_regs,
		.help =
"regs\n"
"    Read and display the current register contents.\n"
	},
	{
		.name = "prog",
		.func = cmd_prog,
		.help =
"prog <filename>\n"
"    Erase the device and flash the data contained in a binary file.\n"
"    This command also loads symbols from the file, if available.\n"
	},
	{
		.name = "md",
		.func = cmd_md,
		.help =
"md <address> [length]\n"
"    Read the specified number of bytes from memory at the given\n"
"    address, and display a hexdump.\n"
	},
	{
		.name = "mw",
		.func = cmd_mw,
		.help =
"mw <address> bytes ...\n"
"    Write a sequence of bytes to a memory address. Byte values are\n"
"    two-digit hexadecimal numbers.\n"
	},
	{
		.name = "reset",
		.func = cmd_reset,
		.help =
 "reset\n"
 "    Reset (and halt) the CPU.\n"
	},
	{
		.name = "erase",
		.func = cmd_erase,
		.help =
"erase\n"
"    Erase the device under test.\n"
	},
	{
		.name = "step",
		.func = cmd_step,
		.help =
"step [count]\n"
"    Single-step the CPU, and display the register state.\n"
	},
	{
		.name = "run",
		.func = cmd_run,
		.help =
"run\n"
"    Run the CPU to until a breakpoint is reached or the command is\n"
"    interrupted.\n"
	},
	{
		.name = "set",
		.func = cmd_set,
		.help =
"set <register> <value>\n"
"    Change the value of a CPU register.\n"
	},
	{
		.name = "dis",
		.func = cmd_dis,
		.help =
"dis <address> [length]\n"
"    Disassemble a section of memory.\n"
	},
	{
		.name = "hexout",
		.func = cmd_hexout,
		.help =
"hexout <address> <length> <filename.hex>\n"
"    Save a region of memory into a HEX file.\n"
	},
	{
		.name = "gdb",
		.func = cmd_gdb,
		.help =
		"gdb [port]\n"
		"    Run a GDB remote stub on the given TCP/IP port.\n"
	},
	{
		.name = "=",
		.func = cmd_eval,
		.help =
"= <expression>\n"
"    Evaluate an expression using the symbol table.\n"
	},
	{
		.name = "sym",
		.func = cmd_sym,
		.help =
"sym clear\n"
"    Clear the symbol table.\n"
"sym set <name> <value>\n"
"    Set or overwrite the value of a symbol.\n"
"sym del <name>\n"
"    Delete a symbol from the symbol table.\n"
"sym import <filename>\n"
"    Load symbols from the given file.\n"
"sym import+ <filename>\n"
"    Load additional symbols from the given file.\n"
"sym export <filename>\n"
"    Save the current symbols to a BSD-style symbol file.\n"
"sym find <regex>\n"
"    Search for symbols by regular expression.\n"
"sym rename <regex> <string>\n"
"    Replace every occurance of a pattern in symbol names.\n"
	},
	{
		.name = "isearch",
		.func = cmd_isearch,
		.help =
"isearch <address> <length> [options ...]\n"
"    Search for an instruction matching certain search terms. These\n"
"    terms may be any of the following:\n"
"        opcode <opcode>\n"
"        byte|word|aword\n"
"        jump|single|double|noarg\n"
"        src <value>\n"
"        dst <value>\n"
"        srcreg <register>\n"
"        dstreg <register>\n"
"        srcmode R|I|S|&|@|+|#\n"
"        dstmode R|I|S|&|@|+|#\n"
"    For single-operand instructions, the operand is considered the\n"
"    destination operand.\n"
	},
	{
		.name = "cgraph",
		.func = cmd_cgraph,
		.help =
"cgraph <address> <length> [function]\n"
"    Analyse the range given and produce a call graph. Displays a summary\n"
"    of all functions if no function address is given.\n"
	}
};

int cmddb_get(const char *name, struct cmddb_record *ret)
{
	int i;

	for (i = 0; i < ARRAY_LEN(commands); i++) {
		const struct cmddb_record *r = &commands[i];

		if (!strcasecmp(r->name, name)) {
			memcpy(ret, r, sizeof(*ret));
			return 0;
		}
	}

	return -1;
}

int cmddb_enum(cmddb_enum_func_t func, void *user_data)
{
	int i;

	for (i = 0; i < ARRAY_LEN(commands); i++)
		if (func(user_data, &commands[i]) < 0)
			return -1;

	return 0;
}
