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
	"No error",								// 0
	"Could not initialize device interface",				// 1
	"Could not close device interface",					// 2
	"Invalid parameter(s)",							// 3
	"Could not find device (or device not supported)",			// 4
	"Unknown device",							// 5
	"Could not read device memory",						// 6
	"Could not write device memory",					// 7
	"Could not read device configuration fuses",				// 8
	"Incorrectly configured device; device derivative not supported",	// 9

	"Could not set device Vcc",						// 10
	"Could not reset device",						// 11
	"Could not preserve/restore device memory",				// 12
	"Could not set device operating frequency",				// 13
	"Could not erase device memory",					// 14
	"Could not set device breakpoint",					// 15
	"Could not single step device",						// 16
	"Could not run device (to breakpoint)",					// 17
	"Could not determine device state",					// 18
	"Could not open Enhanced Emulation Module",				// 19

	"Could not read Enhanced Emulation Module register",			// 20
	"Could not write Enhanced Emulation Module register",			// 21
	"Could not close Enhanced Emulation Module",				// 22
	"File open error",							// 23
	"Could not determine file type",					// 24
	"Unexpected end of file encountered",					// 25
	"File input/output error",						// 26
	"File data error",							// 27
	"Verification error",							// 28
	"Could not blow device security fuse",					// 29

	"Could not access device - security fuse is blown",			// 30
	"Error within Intel Hex file",						// 31
	"Could not write device Register",					// 32
	"Could not read device Register",					// 33
	"Not supported by selected Interface",					// 34
	"Could not communicate with FET",					// 35
	"No external power supply detected",					// 36
	"External power too low",						// 37
	"External power detected",						// 38
	"External power too high",						// 39

	"Hardware Self Test Error",						// 40
	"Fast Flash Routine experienced a timeout",				// 41
	"Could not create thread for polling",					// 42
	"Could not initialize Enhanced Emulation Module",			// 43
	"Insufficient resources",						// 44
	"No clock control emulation on connected device",			// 45
	"No state storage buffer implemented on connected device",		// 46
	"Could not read trace buffer",						// 47
	"Enable the variable watch function",					// 48
	"No trigger sequencer implemented on connected device",			// 49

	"Could not read sequencer state - Sequencer is disabled",		// 50
	"Could not remove trigger - Used in sequencer",				// 51
	"Could not set combination - Trigger is used in sequencer",		// 52
	"System Protection Module A is enabled - Device locked",		// 53
	"Invalid SPMA key was passed to the target device - Device locked",	// 54
	"Device does not accept any further SPMA keys - Device locked",		// 55
	"MSP-FET430UIF Firmware erased - Bootloader active",			// 56
	"Could not find MSP-FET430UIF on specified COM port",			// 57
	"MSP-FET430UIF is already in use",					// 58
	"Enhanced Emulation Module polling thread is already active",		// 59

	"Could not terminate Enhanced Emulation Module polling thread",		// 60
	"Could not unlock BSL memory segments",					// 61
	"Could not perform access, BSL memory segments are protected",		// 62
	"FOUND_OTHER_DEVICE (errcode=63)", /* XXX */				// 63
	"Wrong password provided to open JTAG",					// 64
	"Internal error",							// 65
	"Only one UIF must be connected during update to v3",			// 66
	"Invalid error number",							// 67
};

const char *fet_error(int code)
{
	if (code < 0 || code >= ARRAY_LEN(error_strings))
		return "Unknown error";

	return error_strings[code];
}
