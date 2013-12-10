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
#include "flatfile.h"
#include "gdb.h"
#include "rtools.h"
#include "sym.h"
#include "stdcmd.h"
#include "simio.h"
#include "aliasdb.h"
#include "power.h"

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
		.name = "setwatch",
		.func = cmd_setwatch,
		.help =
"setwatch <addr> [index]\n"
"    Set a watchpoint. If no index is specified. the first available\n"
"    slot will be used.\n"
	},
	{
		.name = "setwatch_r",
		.func = cmd_setwatch_r,
		.help =
"setwatch_r <addr> [index]\n"
"    Set a read-only watchpoint.\n"
	},
	{
		.name = "setwatch_w",
		.func = cmd_setwatch_w,
		.help =
"setwatch_w <addr> [index]\n"
"    Set a write-only watchpoint.\n"
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
		.name = "load",
		.func = cmd_load,
		.help =
"load <filename>\n"
"    Flash the data contained in a binary file. Does not load symbols\n"
"    or erase the device.\n"
	},
	{
		.name = "verify",
		.func = cmd_verify,
		.help =
"verify <filename>\n"
"    Compare the contents of the given binary file to the device memory.\n"
	},
	{
		.name = "load_raw",
		.func = cmd_load_raw,
		.help =
"load_raw <filename> <address>\n"
"    Write the data contained in a raw binary file to the given memory\n"
"    address.\n"
	},
	{
		.name = "verify_raw",
		.func = cmd_verify_raw,
		.help =
"verify_raw <filename> <address>\n"
"    Compare the contents of a raw binary file to the device memory at\n"
"    the given address.\n"
	},
	{
		.name = "save_raw",
		.func = cmd_save_raw,
		.help =
"save_raw <address> <length> <filename>\n"
"    Save a region of memory to a raw binary file.\n"
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
		.name = "blow_jtag_fuse",
		.func = cmd_blow_jtag_fuse,
		.help =
"blow-jtag-fuse\n"
"    Blow the device's JTAG fuse.\n"
"\n"
"    \x1b[1mWARNING: this is an irreversible operation!\x1b[0m\n"
	},
	{
		.name = "erase",
		.func = cmd_erase,
		.help =
"erase [all|segment] [address]\n"
"erase segrange <address> <size> <seg-size>\n"
"    Erase the device under test. With no arguments, erases all of main\n"
"    memory. Specify arguments to perform a mass erase, or to erase\n"
"    individual segments. The \"segrange\" mode is used to erase an\n"
"    address range via a series of segment erases.\n"
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
	},
	{
		.name = "exit",
		.func = cmd_exit,
		.help =
"exit\n"
"    Exit from MSPDebug.\n"
	},
	{
		.name = "simio",
		.func = cmd_simio,
		.help =
"simio add <class> <name> [args ...]\n"
"    Add a new device to the IO simulator's bus.\n"
"simio del <name>\n"
"    Delete a device from the bus.\n"
"simio devices\n"
"    Show all devices attached to the bus.\n"
"simio classes\n"
"    Show the types of devices which may be attached.\n"
"simio help <class>\n"
"    Obtain more information about a device type.\n"
"simio config <name> <param> [args ...]\n"
"    Change settings of an attached device.\n"
"simio info <name>\n"
"    Print status information for an attached device.\n"
	},
	{
		.name = "alias",
		.func = cmd_alias,
		.help =
"alias\n"
"    List all defined aliases.\n"
"alias <name>\n"
"    Remove an alias definition.\n"
"alias <name> <command>\n"
"    Define a new alias.\n"
	},
	{
		.name = "fill",
		.func = cmd_fill,
		.help =
"fill <address> <length> <b0> [b1 b2 ...]\n"
"    Fill the given memory range with a repeated byte sequence.\n"
	},
	{
		.name = "power",
		.func = cmd_power,
		.help =
"power info\n"
"    Show basic power statistics.\n"
"power clear\n"
"    Clear power statistics.\n"
"power all [granularity]\n"
"    Show all power data, optionally specifying a granularity in us.\n"
"power session <N> [granularity]\n"
"    Show data only for the specified session.\n"
"power export-csv <N> <filename>\n"
"    Write session data for the given session to a CSV file.\n"
"power profile\n"
"    List power profile data by symbol.\n"
	},
#ifndef NO_SHELLCMD
	{
		.name = "!",
		.func = cmd_shellcmd,
		.help =
"! [command [args ...]]\n"
"    Invoke an interactive shell, optionally execute command.\n"
	},
#endif /* !NO_SHELLCMD */
};

int cmddb_get(const char *name, struct cmddb_record *ret)
{
	int len = strlen(name);
	int i;
	const struct cmddb_record *found = NULL;

	/* First look for an exact match */
	for (i = 0; i < ARRAY_LEN(commands); i++) {
		const struct cmddb_record *r = &commands[i];

		if (!strcasecmp(r->name, name)) {
			found = r;
			goto done;
		}
	}

	/* Allow partial matches if unambiguous */
	for (i = 0; i < ARRAY_LEN(commands); i++) {
		const struct cmddb_record *r = &commands[i];

		if (!strncasecmp(r->name, name, len)) {
			if (found)
				return -1;
			found = r;
		}
	}

	if (!found)
		return -1;

done:
	memcpy(ret, found, sizeof(*ret));
	return 0;
}

int cmddb_enum(cmddb_enum_func_t func, void *user_data)
{
	int i;

	for (i = 0; i < ARRAY_LEN(commands); i++)
		if (func(user_data, &commands[i]) < 0)
			return -1;

	return 0;
}
