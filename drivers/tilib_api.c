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
#include <string.h>
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
 * New API (post-SLAC460L)
 */

struct new_messageid {
	uint32_t uiMsgIdSingleStep;
	uint32_t uiMsgIdBreakpoint;
	uint32_t uiMsgIdStorage;
	uint32_t uiMsgIdState;
	uint32_t uiMsgIdWarning;
	uint32_t uiMsgIdCPUStopped;
};

static void o2n_messageid(struct new_messageid *dst,
			  const MessageID_t *src)
{
	dst->uiMsgIdSingleStep = src->uiMsgIdSingleStep;
	dst->uiMsgIdBreakpoint = src->uiMsgIdBreakpoint;
	dst->uiMsgIdStorage = src->uiMsgIdStorage;
	dst->uiMsgIdState = src->uiMsgIdState;
	dst->uiMsgIdWarning = src->uiMsgIdWarning;
	dst->uiMsgIdCPUStopped = src->uiMsgIdCPUStopped;
}

struct new_breakpoint {
	BpMode_t          bpMode;
	int32_t           lAddrVal;
	BpType_t          bpType;
	int32_t           lReg;
	BpAccess_t        bpAccess;
	BpAction_t        bpAction;
	BpOperat_t        bpOperat;
	int32_t           lMask;
	int32_t           lRangeEndAdVa;
	BpRangeAction_t   bpRangeAction;
	BpCondition_t     bpCondition;
	uint32_t          lCondMdbVal;
	BpAccess_t        bpCondAccess;
	int32_t           lCondMask;
	BpOperat_t        bpCondOperat;
	uint16_t          wExtCombine;
};

static void o2n_breakpoint(struct new_breakpoint *dst,
			   const BpParameter_t *src)
{
	dst->bpMode = src->bpMode;
	dst->lAddrVal = src->lAddrVal;
	dst->bpType = src->bpType;
	dst->lReg = src->lReg;
	dst->bpAccess = src->bpAccess;
	dst->bpAction = src->bpAction;
	dst->bpOperat = src->bpOperat;
	dst->lMask = src->lMask;
	dst->lRangeEndAdVa = src->lRangeEndAdVa;
	dst->bpRangeAction = src->bpRangeAction;
	dst->bpCondition = src->bpCondition;
	dst->lCondMdbVal = src->lCondMdbVal;
	dst->bpCondAccess = src->bpCondAccess;
	dst->lCondMask = src->lCondMask;
	dst->bpCondOperat = src->bpCondOperat;
	dst->wExtCombine = src->wExtCombine;
}

union new_device {
	uint8_t buffer[112];
	struct {
		uint16_t  endian;
		uint16_t  id;
		uint8_t   string[32];
		uint16_t  mainStart;
		uint16_t  infoStart;
		uint16_t  ramEnd;
		uint16_t  nBreakpoints;
		uint16_t  emulation;
		uint16_t  clockControl;
		uint16_t  lcdStart;
		uint16_t  lcdEnd;
		uint16_t  vccMinOp;
		uint16_t  vccMaxOp;
		uint16_t  hasTestVpp;
		uint16_t  ramStart;
		uint16_t  ram2Start;
		uint16_t  ram2End;
		uint16_t  infoEnd;
		uint32_t  mainEnd;
		uint16_t  bslStart;
		uint16_t  bslEnd;
		uint16_t  nRegTrigger;
		uint16_t  nCombinations;
		uint8_t   cpuArch;
		uint8_t   jtagId;
		uint16_t  coreIpId;
		uint32_t  deviceIdPtr;
		uint16_t  eemVersion;
		uint16_t  nBreakpointsOptions;
		uint16_t  nBreakpointsReadWrite;
		uint16_t  nBreakpointsDma;
		uint16_t  TrigerMask;
		uint16_t  nRegTriggerOperations;
		uint16_t  nStateStorage;
		uint16_t  nCycleCounter;
		uint16_t  nCycleCounterOperations;
		uint16_t  nSequencer;
		uint16_t  HasFramMemroy;
		uint16_t  mainSegmentSize;
	} __attribute__((packed));
};

static void n2o_device(union DEVICE_T *dst, const union new_device *src)
{
	dst->endian = src->endian;
	dst->id = src->id;
	memcpy(dst->string, src->string, sizeof(dst->string));
	dst->mainStart = src->mainStart;
	dst->infoStart = src->infoStart;
	dst->ramEnd = src->ramEnd;
	dst->nBreakpoints = src->nBreakpoints;
	dst->emulation = src->emulation;
	dst->clockControl = src->clockControl;
	dst->lcdStart = src->lcdStart;
	dst->lcdEnd = src->lcdEnd;
	dst->vccMinOp = src->vccMinOp;
	dst->vccMaxOp = src->vccMaxOp;
	dst->hasTestVpp = src->hasTestVpp;
	dst->ramStart = src->ramStart;
	dst->ram2Start = src->ram2Start;
	dst->ram2End = src->ram2End;
	dst->infoEnd = src->infoEnd;
	dst->mainEnd = src->mainEnd;
	dst->bslStart = src->bslStart;
	dst->bslEnd = src->bslEnd;
	dst->nRegTrigger = src->nRegTrigger;
	dst->nCombinations = src->nCombinations;
	dst->cpuArch = src->cpuArch;
	dst->jtagId = src->jtagId;
	dst->coreIpId = src->coreIpId;
	dst->deviceIdPtr = src->deviceIdPtr;
	dst->eemVersion = src->eemVersion;
	dst->nBreakpointsOptions = src->nBreakpointsOptions;
	dst->nBreakpointsReadWrite = src->nBreakpointsReadWrite;
	dst->nBreakpointsDma = src->nBreakpointsDma;
	dst->TrigerMask = src->TrigerMask;
	dst->nRegTriggerOperations = src->nRegTriggerOperations;
	dst->nStateStorage = src->nStateStorage;
	dst->nCycleCounter = src->nCycleCounter;
	dst->nCycleCounterOperations = src->nCycleCounterOperations;
	dst->nSequencer = src->nSequencer;
	dst->HasFramMemroy = src->HasFramMemroy;
	/* dst->mainSegmentSize = src->mainSegmentSize; */
}

typedef void (*new_notify_func_t)
    (uint32_t MsgId, uint32_t wParam, uint32_t lParam, int32_t clientHandle);

struct tilib_new_api {
	/* MSP430.h */
	int32_t TIDLL (*MSP430_Initialize)(char *port, int32_t *version);
	int32_t TIDLL (*MSP430_VCC)(int32_t voltage);
	int32_t TIDLL (*MSP430_Configure)(int32_t mode, int32_t value);
	int32_t TIDLL (*MSP430_OpenDevice)(char *Device, char *Password,
					    int32_t PwLength, int32_t DeviceCode,
					    int32_t setId);
	int32_t TIDLL (*MSP430_GetFoundDevice)(uint8_t *FoundDevice,
						int32_t count);
	int32_t TIDLL (*MSP430_Close)(int32_t vccOff);
	int32_t TIDLL (*MSP430_Memory)(int32_t address, char *buffer,
					int32_t count, int32_t rw);
	int32_t TIDLL (*MSP430_Reset)(int32_t method, int32_t execute,
				       int32_t releaseJTAG);
	int32_t TIDLL (*MSP430_Erase)(int32_t type, int32_t address,
				       int32_t length);
	int32_t TIDLL (*MSP430_Secure)(void);
	int32_t TIDLL (*MSP430_Error_Number)(void);
	const char *TIDLL (*MSP430_Error_String)(int32_t errNumber);

	int32_t TIDLL (*MSP430_GetNumberOfUsbIfs)(int32_t* number);
	int32_t TIDLL (*MSP430_GetNameOfUsbIf)(int32_t idx, char **name,
						int32_t *status);
	int32_t TIDLL (*MSP430_LoadDeviceDb)(const char *f); // needed for slac460s

	/* MSP430_Debug.h */
	int32_t TIDLL (*MSP430_Registers)(int32_t *registers, int32_t mask,
					   int32_t rw);
	int32_t TIDLL (*MSP430_Run)(int32_t mode, int32_t releaseJTAG);
	int32_t TIDLL (*MSP430_State)(int32_t *state, int32_t stop,
				       int32_t *pCPUCycles);

	/* MSP430_EEM.h */
	int32_t TIDLL (*MSP430_EEM_Init)(new_notify_func_t callback,
					  int32_t clientHandle,
					  struct new_messageid *pMsgIdBuffer);
	int32_t TIDLL (*MSP430_EEM_SetBreakpoint)(uint16_t *pwBpHandle,
					   struct new_breakpoint *pBpBuffer);

	/* MSP430_FET.h */
	int32_t TIDLL (*MSP430_FET_FwUpdate)(char* lpszFileName,
					      new_notify_func_t callback,
					      int32_t clientHandle);

	/* Callback thunk data */
	DLL430_EVENTNOTIFY_FUNC		cb_event;
	DLL430_FET_NOTIFY_FUNC		cb_fw;
};

static struct tilib_new_api napi;

static STATUS_T new_Initialize(char *port, long *version)
{
	int32_t nv;
	int r;

	r = napi.MSP430_Initialize(port, &nv);
	if (r < 0)
		return r;

	if (napi.MSP430_LoadDeviceDb)
	    napi.MSP430_LoadDeviceDb(NULL);

	*version = nv;
	return 0;
}

static STATUS_T new_VCC(long voltage)
{
	return napi.MSP430_VCC(voltage);
}

static STATUS_T new_Configure(long mode, long value)
{
	return napi.MSP430_Configure(mode, value);
}

static STATUS_T new_OpenDevice(char *Device, char *Password,
			       long PwLength, long DeviceCode,
			       long setId)
{
	return napi.MSP430_OpenDevice(Device, Password, PwLength,
				     DeviceCode, setId);
}

static STATUS_T new_GetFoundDevice(char *FoundDevice, long count)
{
	union new_device ndev;
	union DEVICE_T odev;
	int r;

	r = napi.MSP430_GetFoundDevice(ndev.buffer, sizeof(ndev.buffer));
	if (r < 0)
		return r;

	memset(&odev, 0, sizeof(odev));
	n2o_device(&odev, &ndev);

	if (count > sizeof(odev.buffer))
		count = sizeof(odev.buffer);
	memcpy(FoundDevice, odev.buffer, count);
	return 0;
}

static STATUS_T new_Close(long vccOff)
{
	return napi.MSP430_Close(vccOff);
}

static STATUS_T new_Memory(long address, char *buffer, long count, long rw)
{
	return napi.MSP430_Memory(address, buffer, count, rw);
}

static STATUS_T new_Reset(long method, long execute, long releaseJTAG)
{
	return napi.MSP430_Reset(method, execute, releaseJTAG);
}

static STATUS_T new_Erase(long type, long address, long length)
{
	return napi.MSP430_Erase(type, address, length);
}

static STATUS_T new_Secure(void)
{
	return napi.MSP430_Secure();
}

static STATUS_T new_Error_Number(void)
{
	return napi.MSP430_Error_Number();
}

static const char *new_Error_String(long errNumber)
{
	return napi.MSP430_Error_String(errNumber);
}

static STATUS_T new_GetNumberOfUsbIfs(long* number)
{
	int32_t nn;
	int r;

	r = napi.MSP430_GetNumberOfUsbIfs(&nn);
	if (r < 0)
		return r;

	*number = nn;
	return 0;
}

static STATUS_T new_GetNameOfUsbIf(long idx, char **name, long *status)
{
	int32_t ns;
	int r;

	r = napi.MSP430_GetNameOfUsbIf(idx, name, &ns);
	if (r < 0)
		return r;

	*status = ns;
	return 0;
}

static STATUS_T new_Registers(long *registers, long mask, long rw)
{
	int32_t nr[16];
	int i;

	for (i = 0; i < 16; i++)
		if (mask & (1 << i))
			nr[i] = registers[i];

	i = napi.MSP430_Registers(nr, mask, rw);
	if (i < 0)
		return i;

	for (i = 0; i < 16; i++)
		if (mask & (1 << i))
			registers[i] = nr[i];

	return 0;
}

static STATUS_T new_Run(long mode, long releaseJTAG)
{
	return napi.MSP430_Run(mode, releaseJTAG);
}

static STATUS_T new_State(long *state, long stop, long *pCPUCycles)
{
	int32_t ns;
	int32_t nc;
	int r;

	r = napi.MSP430_State(&ns, stop, &nc);
	if (r < 0)
		return r;

	*state = ns;
	*pCPUCycles = nc;
	return 0;
}

static void new_event(uint32_t MsgId, uint32_t wParam,
		      uint32_t lParam, int32_t clientHandle)
{
	napi.cb_event(MsgId, wParam, lParam, clientHandle);
}

static STATUS_T new_EEM_Init(DLL430_EVENTNOTIFY_FUNC callback,
			     long clientHandle, MessageID_t *pMsgIdBuffer)
{
	struct new_messageid nm;

	napi.cb_event = callback;
	o2n_messageid(&nm, pMsgIdBuffer);

	return napi.MSP430_EEM_Init(new_event, clientHandle, &nm);
}

static STATUS_T new_EEM_SetBreakpoint(uint16_t *pwBpHandle,
				      BpParameter_t *pBpBuffer)
{
	struct new_breakpoint np;

	o2n_breakpoint(&np, pBpBuffer);
	return napi.MSP430_EEM_SetBreakpoint(pwBpHandle, &np);
}

static void new_fw(uint32_t MsgId, uint32_t wParam,
		   uint32_t lParam, int32_t clientHandle)
{
	napi.cb_fw(MsgId, wParam, lParam, clientHandle);
}

static STATUS_T new_FET_FwUpdate(char* lpszFileName,
				 DLL430_FET_NOTIFY_FUNC callback,
				 long clientHandle)
{
	napi.cb_fw = callback;
	return napi.MSP430_FET_FwUpdate(lpszFileName, new_fw, clientHandle);
}

static const struct tilib_api_table new_tab = {
	.MSP430_Initialize		= new_Initialize,
	.MSP430_VCC			= new_VCC,
	.MSP430_Configure		= new_Configure,
	.MSP430_OpenDevice		= new_OpenDevice,
	.MSP430_GetFoundDevice		= new_GetFoundDevice,
	.MSP430_Close			= new_Close,
	.MSP430_Memory			= new_Memory,
	.MSP430_Reset			= new_Reset,
	.MSP430_Erase			= new_Erase,
	.MSP430_Secure			= new_Secure,
	.MSP430_Error_Number		= new_Error_Number,
	.MSP430_Error_String		= new_Error_String,
	.MSP430_GetNumberOfUsbIfs	= new_GetNumberOfUsbIfs,
	.MSP430_GetNameOfUsbIf		= new_GetNameOfUsbIf,
	.MSP430_Registers		= new_Registers,
	.MSP430_Run			= new_Run,
	.MSP430_State			= new_State,
	.MSP430_EEM_Init		= new_EEM_Init,
	.MSP430_EEM_SetBreakpoint	= new_EEM_SetBreakpoint,
	.MSP430_FET_FwUpdate		= new_FET_FwUpdate,
};

static int init_new_api(void)
{
	if (!(napi.MSP430_Initialize = get_func("MSP430_Initialize")))
		return -1;
	if (!(napi.MSP430_VCC = get_func("MSP430_VCC")))
		return -1;
	if (!(napi.MSP430_Configure = get_func("MSP430_Configure")))
		return -1;
	if (!(napi.MSP430_OpenDevice = get_func("MSP430_OpenDevice")))
		return -1;
	if (!(napi.MSP430_GetFoundDevice = get_func("MSP430_GetFoundDevice")))
		return -1;
	if (!(napi.MSP430_Close = get_func("MSP430_Close")))
		return -1;
	if (!(napi.MSP430_Memory = get_func("MSP430_Memory")))
		return -1;
	if (!(napi.MSP430_Reset = get_func("MSP430_Reset")))
		return -1;
	if (!(napi.MSP430_Erase = get_func("MSP430_Erase")))
		return -1;
	if (!(napi.MSP430_Secure = get_func("MSP430_Secure")))
		return -1;
	if (!(napi.MSP430_Error_Number = get_func("MSP430_Error_Number")))
		return -1;
	if (!(napi.MSP430_Error_String = get_func("MSP430_Error_String")))
		return -1;
	if (!(napi.MSP430_GetNumberOfUsbIfs =
		    get_func("MSP430_GetNumberOfUsbIfs")))
		return -1;
	napi.MSP430_LoadDeviceDb = dynload_sym(lib_handle, "MSP430_LoadDeviceDb");
	if (!(napi.MSP430_GetNameOfUsbIf = get_func("MSP430_GetNameOfUsbIf")))
		return -1;
	if (!(napi.MSP430_Registers = get_func("MSP430_Registers")))
		return -1;
	if (!(napi.MSP430_Run = get_func("MSP430_Run")))
		return -1;
	if (!(napi.MSP430_State = get_func("MSP430_State")))
		return -1;
	if (!(napi.MSP430_EEM_Init = get_func("MSP430_EEM_Init")))
		return -1;
	if (!(napi.MSP430_EEM_SetBreakpoint =
			get_func("MSP430_EEM_SetBreakpoint")))
		return -1;
	if (!(napi.MSP430_FET_FwUpdate = get_func("MSP430_FET_FwUpdate")))
		return -1;

	tilib_api = &new_tab;
	return 0;
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
	int ret;

	lib_handle = dynload_open(tilib_filename);
	if (!lib_handle) {
		printc_err("tilib_api: can't find %s: %s\n",
			   tilib_filename, dynload_error());
		return -1;
	}

	if (dynload_sym(lib_handle, "MSP430_HIL_MEMAP")) {
		printc_dbg("Using new (SLAC460L+) API\n");
		ret = init_new_api();
	} else {
		printc_dbg("Using old API\n");
		ret = init_old_api();
	}

	if (ret < 0) {
		dynload_close(lib_handle);
		return -1;
	}

	return 0;
}

void tilib_api_exit(void)
{
	dynload_close(lib_handle);
}
