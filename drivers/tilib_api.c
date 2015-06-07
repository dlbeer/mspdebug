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

#include <stddef.h>
#include "util/output.h"
#include "tilib_api.h"
#include "dynload.h"

static dynload_handle_t lib_handle;
const struct tilib_api_table *tilib_api;

#if defined(__Windows__) || defined(__CYGWIN__)
static const char tilib_filename[] = "MSP430.DLL";
#define TIDLL __stdcall
#else
static const char tilib_filename[] = "libmsp430.so";
#define TIDLL
#endif

static void *get_func(const char *name)
{
	void *ret = dynload_sym(lib_handle, name);

	if (!ret) {
		printc_err("tilib_api: can't find symbol \"%s\": %s\n",
			   name, dynload_error());
		return NULL;
	}

	return ret;
}

/************************************************************************
 * Old API (pre-SLAC460L)
 */

struct tilib_old_api {
	/* MSP430.h */
	STATUS_T TIDLL (*MSP430_Initialize)(char *port, long *version);
	STATUS_T TIDLL (*MSP430_VCC)(long voltage);
	STATUS_T TIDLL (*MSP430_Configure)(long mode, long value);
	STATUS_T TIDLL (*MSP430_OpenDevice)(char *Device, char *Password,
					     long PwLength, long DeviceCode,
					     long setId);
	STATUS_T TIDLL (*MSP430_GetFoundDevice)(char *FoundDevice,
						 long count);
	STATUS_T TIDLL (*MSP430_Close)(long vccOff);
	STATUS_T TIDLL (*MSP430_Memory)(long address, char *buffer,
					 long count, long rw);
	STATUS_T TIDLL (*MSP430_Reset)(long method, long execute,
					long releaseJTAG);
	STATUS_T TIDLL (*MSP430_Erase)(long type, long address, long length);
	STATUS_T TIDLL (*MSP430_Secure)(void);
	STATUS_T TIDLL (*MSP430_Error_Number)(void);
	const char *TIDLL (*MSP430_Error_String)(long errNumber);

	STATUS_T TIDLL (*MSP430_GetNumberOfUsbIfs)(long* number);
	STATUS_T TIDLL (*MSP430_GetNameOfUsbIf)(long idx, char **name,
						 long *status);

	/* MSP430_Debug.h */
	STATUS_T TIDLL (*MSP430_Registers)(long *registers, long mask,
					    long rw);
	STATUS_T TIDLL (*MSP430_Run)(long mode, long releaseJTAG);
	STATUS_T TIDLL (*MSP430_State)(long *state, long stop,
					long *pCPUCycles);

	/* MSP430_EEM.h */
	STATUS_T TIDLL (*MSP430_EEM_Init)(DLL430_EVENTNOTIFY_FUNC callback,
					   long clientHandle,
					   MessageID_t *pMsgIdBuffer);
	STATUS_T TIDLL (*MSP430_EEM_SetBreakpoint)(uint16_t *pwBpHandle,
						    BpParameter_t *pBpBuffer);

	/* MSP430_FET.h */
	STATUS_T TIDLL (*MSP430_FET_FwUpdate)(char* lpszFileName,
					   DLL430_FET_NOTIFY_FUNC callback,
					   long clientHandle);
};

static struct tilib_old_api old;

static STATUS_T old_Initialize(char *port, long *version)
{
	return old.MSP430_Initialize(port, version);
}

static STATUS_T old_VCC(long voltage)
{
	return old.MSP430_VCC(voltage);
}

static STATUS_T old_Configure(long mode, long value)
{
	return old.MSP430_Configure(mode, value);
}

static STATUS_T old_OpenDevice(char *Device, char *Password,
			       long PwLength, long DeviceCode,
			       long setId)
{
	return old.MSP430_OpenDevice(Device, Password, PwLength,
				     DeviceCode, setId);
}

static STATUS_T old_GetFoundDevice(char *FoundDevice, long count)
{
	return old.MSP430_GetFoundDevice(FoundDevice, count);
}

static STATUS_T old_Close(long vccOff)
{
	return old.MSP430_Close(vccOff);
}

static STATUS_T old_Memory(long address, char *buffer, long count, long rw)
{
	return old.MSP430_Memory(address, buffer, count, rw);
}

static STATUS_T old_Reset(long method, long execute, long releaseJTAG)
{
	return old.MSP430_Reset(method, execute, releaseJTAG);
}

static STATUS_T old_Erase(long type, long address, long length)
{
	return old.MSP430_Erase(type, address, length);
}

static STATUS_T old_Secure(void)
{
	return old.MSP430_Secure();
}

static STATUS_T old_Error_Number(void)
{
	return old.MSP430_Error_Number();
}

static const char *old_Error_String(long errNumber)
{
	return old.MSP430_Error_String(errNumber);
}

static STATUS_T old_GetNumberOfUsbIfs(long* number)
{
	return old.MSP430_GetNumberOfUsbIfs(number);
}

static STATUS_T old_GetNameOfUsbIf(long idx, char **name, long *status)
{
	return old.MSP430_GetNameOfUsbIf(idx, name, status);
}

static STATUS_T old_Registers(long *registers, long mask, long rw)
{
	return old.MSP430_Registers(registers, mask, rw);
}

static STATUS_T old_Run(long mode, long releaseJTAG)
{
	return old.MSP430_Run(mode, releaseJTAG);
}

static STATUS_T old_State(long *state, long stop, long *pCPUCycles)
{
	return old.MSP430_State(state, stop, pCPUCycles);
}

static STATUS_T old_EEM_Init(DLL430_EVENTNOTIFY_FUNC callback,
			     long clientHandle, MessageID_t *pMsgIdBuffer)
{
	return old.MSP430_EEM_Init(callback, clientHandle, pMsgIdBuffer);
}

static STATUS_T old_EEM_SetBreakpoint(uint16_t *pwBpHandle,
				      BpParameter_t *pBpBuffer)
{
	return old.MSP430_EEM_SetBreakpoint(pwBpHandle, pBpBuffer);
}

static STATUS_T old_FET_FwUpdate(char* lpszFileName,
				 DLL430_FET_NOTIFY_FUNC callback,
				 long clientHandle)
{
	return old.MSP430_FET_FwUpdate(lpszFileName, callback, clientHandle);
}

static const struct tilib_api_table old_tab = {
	.MSP430_Initialize		= old_Initialize,
	.MSP430_VCC			= old_VCC,
	.MSP430_Configure		= old_Configure,
	.MSP430_OpenDevice		= old_OpenDevice,
	.MSP430_GetFoundDevice		= old_GetFoundDevice,
	.MSP430_Close			= old_Close,
	.MSP430_Memory			= old_Memory,
	.MSP430_Reset			= old_Reset,
	.MSP430_Erase			= old_Erase,
	.MSP430_Secure			= old_Secure,
	.MSP430_Error_Number		= old_Error_Number,
	.MSP430_Error_String		= old_Error_String,
	.MSP430_GetNumberOfUsbIfs	= old_GetNumberOfUsbIfs,
	.MSP430_GetNameOfUsbIf		= old_GetNameOfUsbIf,
	.MSP430_Registers		= old_Registers,
	.MSP430_Run			= old_Run,
	.MSP430_State			= old_State,
	.MSP430_EEM_Init		= old_EEM_Init,
	.MSP430_EEM_SetBreakpoint	= old_EEM_SetBreakpoint,
	.MSP430_FET_FwUpdate		= old_FET_FwUpdate,
};

static int init_old_api(void)
{
	if (!(old.MSP430_Initialize = get_func("MSP430_Initialize")))
		return -1;
	if (!(old.MSP430_VCC = get_func("MSP430_VCC")))
		return -1;
	if (!(old.MSP430_Configure = get_func("MSP430_Configure")))
		return -1;
	if (!(old.MSP430_OpenDevice = get_func("MSP430_OpenDevice")))
		return -1;
	if (!(old.MSP430_GetFoundDevice = get_func("MSP430_GetFoundDevice")))
		return -1;
	if (!(old.MSP430_Close = get_func("MSP430_Close")))
		return -1;
	if (!(old.MSP430_Memory = get_func("MSP430_Memory")))
		return -1;
	if (!(old.MSP430_Reset = get_func("MSP430_Reset")))
		return -1;
	if (!(old.MSP430_Erase = get_func("MSP430_Erase")))
		return -1;
	if (!(old.MSP430_Secure = get_func("MSP430_Secure")))
		return -1;
	if (!(old.MSP430_Error_Number = get_func("MSP430_Error_Number")))
		return -1;
	if (!(old.MSP430_Error_String = get_func("MSP430_Error_String")))
		return -1;
	if (!(old.MSP430_GetNumberOfUsbIfs =
		    get_func("MSP430_GetNumberOfUsbIfs")))
		return -1;
	if (!(old.MSP430_GetNameOfUsbIf = get_func("MSP430_GetNameOfUsbIf")))
		return -1;
	if (!(old.MSP430_Registers = get_func("MSP430_Registers")))
		return -1;
	if (!(old.MSP430_Run = get_func("MSP430_Run")))
		return -1;
	if (!(old.MSP430_State = get_func("MSP430_State")))
		return -1;
	if (!(old.MSP430_EEM_Init = get_func("MSP430_EEM_Init")))
		return -1;
	if (!(old.MSP430_EEM_SetBreakpoint =
			get_func("MSP430_EEM_SetBreakpoint")))
		return -1;
	if (!(old.MSP430_FET_FwUpdate = get_func("MSP430_FET_FwUpdate")))
		return -1;

	tilib_api = &old_tab;
	return 0;
}

/************************************************************************
 * Top-level init
 */

int tilib_api_init(void)
{
	lib_handle = dynload_open(tilib_filename);
	if (!lib_handle) {
		printc_err("tilib_api: can't find %s: %s\n",
			   tilib_filename, dynload_error());
		return -1;
	}

	if (init_old_api() < 0) {
		dynload_close(lib_handle);
		return -1;
	}

	return 0;
}

void tilib_api_exit(void)
{
	dynload_close(lib_handle);
}
