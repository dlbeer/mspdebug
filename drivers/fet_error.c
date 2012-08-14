/* MSPDebug - debugging tool for the eZ430
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
 *
 * Various constants and tables come from uif430, written by Robert
 * Kavaler (kavaler@diva.com). This is available under the same license
 * as this program, from www.relavak.com.
 */

#include "util.h"

/* These messages come from uif430 and from MSP430.DLL V3. */
static const char *error_strings[] =
{
	/*   0 */ "No error",
	/*   1 */ "Could not initialize device interface",
	/*   2 */ "Could not close device interface",
	/*   3 */ "Invalid parameter(s)",
	/*   4 */ "Could not find device or device not supported",
	/*   5 */ "Unknown device",
	/*   6 */ "Could not read device memory",
	/*   7 */ "Could not write device memory",
	/*   8 */ "Could not read device configuration fuses",
	/*   9 */ "Incorrectly configured device; device derivative not supported",

	/*  10 */ "Could not set device Vcc",
	/*  11 */ "Could not reset device",
	/*  12 */ "Could not preserve/restore device memory",
	/*  13 */ "Could not set device operating frequency",
	/*  14 */ "Could not erase device memory",
	/*  15 */ "Could not set device breakpoint",
	/*  16 */ "Could not single step device",
	/*  17 */ "Could not run device (to breakpoint)",
	/*  18 */ "Could not determine device state",
	/*  19 */ "Could not open Enhanced Emulation Module",

	/*  20 */ "Could not read Enhanced Emulation Module register",
	/*  21 */ "Could not write Enhanced Emulation Module register",
	/*  22 */ "Could not close Enhanced Emulation Module",
	/*  23 */ "File open error",
	/*  24 */ "File type could not be identified",
	/*  25 */ "File end error",
	/*  26 */ "File input/output error",
	/*  27 */ "File data error",
	/*  28 */ "Verification error",
	/*  29 */ "Could not blow device security fuse",

	/*  30 */ "Security fuse has been blown",
	/*  31 */ "Error within Intel hex file",
	/*  32 */ "Could not write device register",
	/*  33 */ "Could not read device register",
	/*  34 */ "Not supported by selected interface or interface is not initialized",
	/*  35 */ "Interface communication error",
	/*  36 */ "No external power supply detected",
	/*  37 */ "External power too low",
	/*  38 */ "External power detected",
	/*  39 */ "External power too high",

	/*  40 */ "Hardware self test error",
	/*  41 */ "Fast flash routine experienced a timeout",
	/*  42 */ "Could not create thread for polling",
	/*  43 */ "Could not initialize Enhanced Emulation Module",
	/*  44 */ "Insufficent resources",
	/*  45 */ "No clock control emulation on connected device",
	/*  46 */ "No state storage buffer implemented on connected device",
	/*  47 */ "Could not read trace buffer",
	/*  48 */ "Enable the variable watch function",
	/*  49 */ "No trigger sequencer implemented on connected device",

	/*  50 */ "Could not read sequencer state - sequencer is disabled",
	/*  51 */ "Could not remove trigger - used in sequencer",
	/*  52 */ "Could not set combination - trigger is used in sequencer",
	/*  53 */ "System Protection Module A is enabled - device locked",
	/*  54 */ "Invalid SPMA key was passed to the target device - device locked",
	/*  55 */ "Device does not accept any further SPMA keys - device locked",
	/*  56 */ "MSP-FET430UIF Firmware erased - bootloader active",
	/*  57 */ "Could not find MSP-FET430UIF on specified COM port",
	/*  58 */ "MSP-FET430UIF is already in use",
	/*  59 */ "EEM polling thread is already active",

	/*  60 */ "Could not terminate EEM polling thread",
	/*  61 */ "Could not unlock BSL memory segments",
	/*  62 */ "Could not perform access, BSL memory segments are protected",
	/*  63 */ "Another device as selected was found",
	/*  64 */ "Could not enable JTAG wrong password",
	/*  65 */ "Only one UIF must be connected during update to v3",
	/*  66 */ "CDC-USB-FET driver was not installed, please install the driver",
	/*  67 */ "Manual reboot of USB-FET needed! PLEASE unplug and reconnect your USB-FET!",
	/*  68 */ "Internal error",
	/*  69 */ "Invalid error number",
};

const char *fet_error(int code)
{
	if (code < 0 || code >= ARRAY_LEN(error_strings))
		return "Unknown error";

	return error_strings[code];
}
