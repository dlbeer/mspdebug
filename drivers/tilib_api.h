/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2015 Daniel Beer
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

#ifndef TILIB_API_H_
#define TILIB_API_H_

#include "tilib_defs.h"

struct tilib_api_table {
	/* MSP430.h */
	STATUS_T (*MSP430_Initialize)(char *port, long *version);
	STATUS_T (*MSP430_VCC)(long voltage);
	STATUS_T (*MSP430_Configure)(long mode, long value);
	STATUS_T (*MSP430_OpenDevice)(char *Device, char *Password,
					     long PwLength, long DeviceCode,
					     long setId);
	STATUS_T (*MSP430_GetFoundDevice)(char *FoundDevice,
						 long count);
	STATUS_T (*MSP430_Close)(long vccOff);
	STATUS_T (*MSP430_Memory)(long address, char *buffer,
					 long count, long rw);
	STATUS_T (*MSP430_Reset)(long method, long execute,
					long releaseJTAG);
	STATUS_T (*MSP430_Erase)(long type, long address, long length);
	STATUS_T (*MSP430_Secure)(void);
	STATUS_T (*MSP430_Error_Number)(void);
	const char *(*MSP430_Error_String)(long errNumber);

	STATUS_T (*MSP430_GetNumberOfUsbIfs)(long* number);
	STATUS_T (*MSP430_GetNameOfUsbIf)(long idx, char **name,
						 long *status);

	/* MSP430_Debug.h */
	STATUS_T (*MSP430_Registers)(long *registers, long mask,
					    long rw);
	STATUS_T (*MSP430_Run)(long mode, long releaseJTAG);
	STATUS_T (*MSP430_State)(long *state, long stop,
					long *pCPUCycles);

	/* MSP430_EEM.h */
	STATUS_T (*MSP430_EEM_Init)(DLL430_EVENTNOTIFY_FUNC callback,
					   long clientHandle,
					   MessageID_t *pMsgIdBuffer);
	STATUS_T (*MSP430_EEM_SetBreakpoint)(uint16_t *pwBpHandle,
						    BpParameter_t *pBpBuffer);

	/* MSP430_FET.h */
	STATUS_T (*MSP430_FET_FwUpdate)(char* lpszFileName,
					   DLL430_FET_NOTIFY_FUNC callback,
					   long clientHandle);
};

extern const struct tilib_api_table *tilib_api;

int tilib_api_init(void);
void tilib_api_exit(void);

#endif
