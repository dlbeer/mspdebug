/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2011 Daniel Beer
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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "output.h"
#include "dynload.h"
#include "tilib.h"
#include "tilib_defs.h"
#include "threads.h"

#ifdef WIN32
static const char tilib_filename[] = "MSP430.DLL";
#else
static const char tilib_filename[] = "libmsp430.so";
#endif

struct tilib_device {
	struct device		base;

	dynload_handle_t	hnd;

	threads_lock_t		mb_lock;
	uint32_t		mailbox;

	uint16_t		bp_handles[DEVICE_MAX_BREAKPOINTS];

	/* MSP430.h */
	STATUS_T WINAPI (*MSP430_Initialize)(char *port, long *version);
	STATUS_T WINAPI (*MSP430_VCC)(long voltage);
	STATUS_T WINAPI (*MSP430_Configure)(long mode, long value);
	STATUS_T WINAPI (*MSP430_OpenDevice)(char *Device, char *Password,
					     long PwLength, long DeviceCode,
					     long setId);
	STATUS_T WINAPI (*MSP430_GetFoundDevice)(char *FoundDevice,
						 long count);
	STATUS_T WINAPI (*MSP430_Close)(long vccOff);
	STATUS_T WINAPI (*MSP430_Memory)(long address, char *buffer,
					 long count, long rw);
	STATUS_T WINAPI (*MSP430_Reset)(long method, long execute,
					long releaseJTAG);
	STATUS_T WINAPI (*MSP430_Erase)(long type, long address, long length);
	STATUS_T WINAPI (*MSP430_Error_Number)(void);
	const char * WINAPI (*MSP430_Error_String)(long errNumber);

	/* MSP430_Debug.h */
	STATUS_T WINAPI (*MSP430_Registers)(long *registers, long mask,
					    long rw);
	STATUS_T WINAPI (*MSP430_Run)(long mode, long releaseJTAG);
        STATUS_T WINAPI (*MSP430_State)(long* state, long stop,
					long* pCPUCycles);

	/* MSP430_EEM.h */
	STATUS_T WINAPI (*MSP430_EEM_Init)(DLL430_EVENTNOTIFY_FUNC callback,
					   long clientHandle,
					   MessageID_t *pMsgIdBuffer);
	STATUS_T WINAPI (*MSP430_EEM_SetBreakpoint)(uint16_t *pwBpHandle,
						    BpParameter_t *pBpBuffer);

	/* MSP430_FET.h */
	STATUS_T WINAPI (*MSP430_FET_FwUpdate)(char* lpszFileName,
					   DLL430_FET_NOTIFY_FUNC callback,
					   long clientHandle);

};

#define MID_SINGLE_STEP		0x01
#define MID_BREAKPOINT		0x02
#define MID_STORAGE		0x04
#define MID_STATE		0x08
#define MID_WARNING		0x10
#define MID_CPU_STOPPED		0x20

#define MID_HALT_ANY		(MID_BREAKPOINT | MID_CPU_STOPPED)

const static MessageID_t my_message_ids = {
	.uiMsgIdSingleStep	= MID_SINGLE_STEP,
	.uiMsgIdBreakpoint	= MID_BREAKPOINT,
	.uiMsgIdStorage		= MID_STORAGE,
	.uiMsgIdState		= MID_STATE,
	.uiMsgIdWarning		= MID_WARNING,
	.uiMsgIdCPUStopped	= MID_CPU_STOPPED
};

static void event_notify(unsigned int msg_id, unsigned int w_param,
			 long l_param, long client_handle)
{
	struct tilib_device *dev = (struct tilib_device *)client_handle;

	threads_lock_acquire(&dev->mb_lock);
	dev->mailbox |= msg_id;
	threads_lock_release(&dev->mb_lock);
}

static uint32_t event_fetch(struct tilib_device *dev)
{
	uint32_t ret;

	threads_lock_acquire(&dev->mb_lock);
	ret = dev->mailbox;
	dev->mailbox = 0;
	threads_lock_release(&dev->mb_lock);

	return ret;
}

static void *get_func(dynload_handle_t hnd, const char *name)
{
	void *ret = dynload_sym(hnd, name);

	if (!ret) {
		printc_err("tilib: can't find symbol \"%s\": %s\n",
			   name, dynload_error());
		return NULL;
	}

	return ret;
}

static int get_all_funcs(struct tilib_device *dev)
{
	dev->MSP430_Initialize = get_func(dev->hnd, "MSP430_Initialize");
	if (!dev->MSP430_Initialize)
		return -1;

	dev->MSP430_VCC = get_func(dev->hnd, "MSP430_VCC");
	if (!dev->MSP430_VCC)
		return -1;

	dev->MSP430_Configure = get_func(dev->hnd, "MSP430_Configure");
	if (!dev->MSP430_Configure)
		return -1;

	dev->MSP430_OpenDevice = get_func(dev->hnd, "MSP430_OpenDevice");
	if (!dev->MSP430_OpenDevice)
		return -1;

	dev->MSP430_GetFoundDevice = get_func(dev->hnd,
		"MSP430_GetFoundDevice");
	if (!dev->MSP430_GetFoundDevice)
		return -1;

	dev->MSP430_Close = get_func(dev->hnd, "MSP430_Close");
	if (!dev->MSP430_Close)
		return -1;

	dev->MSP430_Memory = get_func(dev->hnd, "MSP430_Memory");
	if (!dev->MSP430_Memory)
		return -1;

	dev->MSP430_Reset = get_func(dev->hnd, "MSP430_Reset");
	if (!dev->MSP430_Reset)
		return -1;

	dev->MSP430_Erase = get_func(dev->hnd, "MSP430_Erase");
	if (!dev->MSP430_Erase)
		return -1;

	dev->MSP430_Error_Number = get_func(dev->hnd, "MSP430_Error_Number");
	if (!dev->MSP430_Error_Number)
		return -1;

	dev->MSP430_Error_String = get_func(dev->hnd, "MSP430_Error_String");
	if (!dev->MSP430_Error_String)
		return -1;

	dev->MSP430_Registers = get_func(dev->hnd, "MSP430_Registers");
	if (!dev->MSP430_Registers)
		return -1;

	dev->MSP430_Run = get_func(dev->hnd, "MSP430_Run");
	if (!dev->MSP430_Run)
		return -1;

	dev->MSP430_State = get_func(dev->hnd, "MSP430_State");
	if (!dev->MSP430_State)
		return -1;

	dev->MSP430_EEM_Init = get_func(dev->hnd, "MSP430_EEM_Init");
	if (!dev->MSP430_EEM_Init)
		return -1;

	dev->MSP430_EEM_SetBreakpoint = get_func(dev->hnd,
		"MSP430_EEM_SetBreakpoint");
	if (!dev->MSP430_EEM_SetBreakpoint)
		return -1;

	dev->MSP430_FET_FwUpdate = get_func(dev->hnd, "MSP430_FET_FwUpdate");
	if (!dev->MSP430_FET_FwUpdate)
		return -1;

	return 0;
}

static void report_error(struct tilib_device *dev, const char *what)
{
	long err = dev->MSP430_Error_Number();
	const char *desc = dev->MSP430_Error_String(err);

	printc_err("tilib: %s: %s (error = %d)\n", what, desc, err);
}

static int tilib_readmem(device_t dev_base, address_t addr,
			 uint8_t *mem, address_t len)
{
	struct tilib_device *dev = (struct tilib_device *)dev_base;

	if (dev->MSP430_Memory(addr, (char *)mem, len, READ) < 0) {
		report_error(dev, "MSP430_Memory");
		return -1;
	}

	return 0;
}

static int tilib_writemem(device_t dev_base, address_t addr,
			  const uint8_t *mem, address_t len)
{
	struct tilib_device *dev = (struct tilib_device *)dev_base;

	if (dev->MSP430_Memory(addr, (char *)mem, len, WRITE) < 0) {
		report_error(dev, "MSP430_Memory");
		return -1;
	}

	return 0;
}

static long ti_erase_type(device_erase_type_t e)
{
	switch (e) {
	case DEVICE_ERASE_ALL: return ERASE_ALL;
	case DEVICE_ERASE_MAIN: return ERASE_MAIN;
	case DEVICE_ERASE_SEGMENT: return ERASE_SEGMENT;
	}

	return 0;
}

static int tilib_erase(device_t dev_base, device_erase_type_t type,
		       address_t address)
{
	struct tilib_device *dev = (struct tilib_device *)dev_base;

	if (dev->MSP430_Erase(ti_erase_type(type), address, 0) < 0) {
		report_error(dev, "MSP430_Erase");
		return -1;
	}

	return 0;
}

static int tilib_getregs(device_t dev_base, address_t *regs)
{
	struct tilib_device *dev = (struct tilib_device *)dev_base;
	long regbuf[DEVICE_NUM_REGS];
	int i;

	if (dev->MSP430_Registers(regbuf, 0xffff, READ) < 0) {
		report_error(dev, "MSP430_Registers");
		return -1;
	}

	for (i = 0; i < DEVICE_NUM_REGS; i++)
		regs[i] = regbuf[i];

	return 0;
}

static int tilib_setregs(device_t dev_base, const address_t *regs)
{
	struct tilib_device *dev = (struct tilib_device *)dev_base;
	long regbuf[DEVICE_NUM_REGS];
	int i;

	for (i = 0; i < DEVICE_NUM_REGS; i++)
		regbuf[i] = regs[i];

	if (dev->MSP430_Registers(regbuf, 0xffff, WRITE) < 0) {
		report_error(dev, "MSP430_Registers");
		return -1;
	}

	return 0;
}

static int refresh_bps(struct tilib_device *dev)
{
	int i;

	for (i = 0; i < dev->base.max_breakpoints; i++) {
		struct device_breakpoint *bp = &dev->base.breakpoints[i];
		BpParameter_t param = {0};

		if (!(bp->flags & DEVICE_BP_DIRTY))
			continue;

		if (bp->flags & DEVICE_BP_ENABLED) {
			param.bpMode = BP_CODE;
			param.lAddrVal = bp->addr;
			param.bpType = BP_MAB;
			param.lReg = 0; /* not used */
			param.bpAccess = BP_FETCH;
			param.bpAction = BP_BRK;
			param.bpOperat = BP_EQUAL;
			param.lMask = 0; /* what's this? */
			param.lRangeEndAdVa = 0; /* not used */
			param.bpRangeAction = 0; /* not used */
			param.bpCondition = BP_NO_COND;
			param.lCondMdbVal = 0;
			param.bpCondAccess = BP_FETCH;
			param.lCondMask = 0; /* what's this? */
			param.bpCondOperat = BP_EQUAL;
			param.wExtCombine = 0; /* not used? */
		} else {
			param.bpMode = BP_CLEAR;
		}

		if (dev->MSP430_EEM_SetBreakpoint(&dev->bp_handles[i],
			&param) < 0) {
			report_error(dev, "MSP430_EEM_SetBreakpoint");
			return -1;
		}

		bp->flags &= ~DEVICE_BP_DIRTY;
	}

	return 0;
}

static int do_halt(struct tilib_device *dev)
{
	long state;
	long cycles;

	if (dev->MSP430_State(&state, 1, &cycles) < 0) {
		report_error(dev, "MSP430_State");
		return -1;
	}

	/* Is this a blocking call? */
	return 0;
}

static int do_step(struct tilib_device *dev)
{
	if (dev->MSP430_Run(SINGLE_STEP, 0) < 0) {
		report_error(dev, "MSP430_Run");
		return -1;
	}

	return 0;
}

static int tilib_ctl(device_t dev_base, device_ctl_t op)
{
	struct tilib_device *dev = (struct tilib_device *)dev_base;

	switch (op) {
        case DEVICE_CTL_RESET:
		if (dev->MSP430_Reset(RST_RESET, 0, 0) < 0) {
			report_error(dev, "MSP430_Reset");
			return -1;
		}
		return 0;

        case DEVICE_CTL_RUN:
		if (refresh_bps(dev) < 0)
			return -1;

		if (dev->MSP430_Run(RUN_TO_BREAKPOINT, 0) < 0) {
			report_error(dev, "MSP430_Run");
			return -1;
		}
		break;

        case DEVICE_CTL_HALT:
		return do_halt(dev);

        case DEVICE_CTL_STEP:
		return do_step(dev);
	}

	return 0;
}

static device_status_t tilib_poll(device_t dev_base)
{
	struct tilib_device *dev = (struct tilib_device *)dev_base;

        ctrlc_reset();
        if ((usleep(50000) < 0) || ctrlc_check())
                return DEVICE_STATUS_INTR;

	if (event_fetch(dev) & MID_HALT_ANY)
		return DEVICE_STATUS_HALTED;

	return DEVICE_STATUS_RUNNING;
}

static void tilib_destroy(device_t dev_base)
{
	struct tilib_device *dev = (struct tilib_device *)dev_base;

	printc_dbg("MSP430_Run\n");
	if (dev->MSP430_Run(FREE_RUN, 1) < 0)
		report_error(dev, "MSP430_Run");

	printc_dbg("MSP430_Close\n");
	dev->MSP430_Close(0);
	dynload_close(dev->hnd);
	threads_lock_destroy(&dev->mb_lock);
	free(dev);
}

static void fw_progress(unsigned int msg_id, unsigned long w_param,
			unsigned long l_param, long client_handle)
{
	struct tilib_device *dev = (struct tilib_device *)client_handle;

	switch (msg_id) {
	case BL_DATA_BLOCK_PROGRAMMED:
		if (w_param > 100)
			w_param = 100;

		printc("   %3d percent done\n", w_param);
		break;

	case BL_UPDATE_ERROR:
		report_error(dev, "BL_UPDATE_ERROR");
		break;

	case BL_WAIT_FOR_TIMEOUT:
		printc("Waiting for bootloader to timeout...\n");
		break;

	case BL_INIT:
		printc("Initializing bootloader...\n");
		break;

	case BL_ERASE_INT_VECTORS:
		printc("Erasing interrupt vectors...\n");
		break;

	case BL_ERASE_FIRMWARE:
		printc("Erasing firmware...\n");
		break;

	case BL_PROGRAM_FIRMWARE:
		printc("Programming new firmware...\n");
		break;

	case BL_EXIT:
		printc("Done, finishing...\n");
		break;

	case BL_UPDATE_DONE:
		printc("Update complete\n");
		break;
	}
}

static int do_fw_update(struct tilib_device *dev)
{
	printc("Starting firmware update (this may take some time)...\n");
	if (dev->MSP430_FET_FwUpdate(NULL, fw_progress, (long)dev) < 0) {
		report_error(dev, "MSP430_FET_FwUpdate");
		return -1;
	}

	return 0;
}

static int do_init(struct tilib_device *dev, const struct device_args *args)
{
	long version;
	char buf[1024];
	union DEVICE_T device;

	/* Not sure if the path is actually modified by MSP430_Initialize,
	 * but the argument isn't const, so probably safest to copy it.
	 */
	strncpy(buf, args->path, sizeof(buf));
	buf[sizeof(buf) - 1] = 0;

	printc_dbg("MSP430_Initialize: %s\n", buf);
	if (dev->MSP430_Initialize(buf, &version) < 0) {
		report_error(dev, "MSP430_Initialize");
		return -1;
	}

	if (version < 0) {
		printc("FET firmware update is required.\n");

		if (args->flags & DEVICE_FLAG_DO_FWUPDATE) {
			if (do_fw_update(dev) < 0) {
				dev->MSP430_Close(0);
				return -1;
			}
		} else {
			printc("Re-run with --allow-fw-update to perform "
			       "a firmware update.\n");
			dev->MSP430_Close(0);
			return -1;
		}
	} else {
		printc_dbg("Firmware version is %d\n", version);
	}

	printc_dbg("MSP430_VCC: %d mV\n", args->vcc_mv);
	if (dev->MSP430_VCC(args->vcc_mv) < 0) {
		report_error(dev, "MSP430_VCC");
		dev->MSP430_Close(0);
		return -1;
	}

	/* Without this delay, MSP430_OpenDevice will often hang. */
	usleep(1000000);

	printc_dbg("MSP430_OpenDevice\n");
	if (dev->MSP430_OpenDevice("DEVICE_UNKNOWN", "", 0, 0, 0) < 0) {
		report_error(dev, "MSP430_OpenDevice");
		dev->MSP430_Close(0);
		return -1;
	}

	printc_dbg("MSP430_GetFoundDevice\n");
	if (dev->MSP430_GetFoundDevice(device.buffer,
				       sizeof(device.buffer)) < 0) {
		report_error(dev, "MSP430_GetFoundDevice");
		dev->MSP430_Close(0);
		return -1;
	}

	printc_dbg("Device: %s (id = 0x%04x)\n", device.string, device.id);
	printc_dbg("%d breakpoints available\n", device.nBreakpoints);
	dev->base.max_breakpoints = device.nBreakpoints;
	if (dev->base.max_breakpoints > DEVICE_MAX_BREAKPOINTS)
		dev->base.max_breakpoints = DEVICE_MAX_BREAKPOINTS;

	printc_dbg("MSP430_EEM_Init\n");
	threads_lock_init(&dev->mb_lock);
	if (dev->MSP430_EEM_Init(event_notify, (long)dev,
				 (MessageID_t *)&my_message_ids) < 0) {
		report_error(dev, "MSP430_EEM_Init");
		dev->MSP430_Close(0);
		threads_lock_destroy(&dev->mb_lock);
		return -1;
	}

	return 0;
}

static device_t tilib_open(const struct device_args *args)
{
	struct tilib_device *dev;

	if (!(args->flags & DEVICE_FLAG_TTY)) {
		printc_err("This driver does not support raw USB access.\n");
		return NULL;
	}

	dev = malloc(sizeof(*dev));
	if (!dev) {
		printc_err("tilib: can't allocate memory: %s\n",
			   last_error());
		return NULL;
	}

	memset(dev, 0, sizeof(*dev));
	dev->base.type = &device_tilib;

	dev->hnd = dynload_open(tilib_filename);
	if (!dev->hnd) {
		printc_err("tilib: can't find %s: %s\n",
			   tilib_filename, dynload_error());
		free(dev);
		return NULL;
	}

	if (get_all_funcs(dev) < 0) {
		dynload_close(dev->hnd);
		free(dev);
		return NULL;
	}

	if (do_init(dev, args) < 0) {
		printc_err("tilib: device initialization failed\n");
		dynload_close(dev->hnd);
		free(dev);
		return NULL;
	}

	return (device_t)dev;
}

const struct device_class device_tilib = {
	.name		= "tilib",
	.help		= "TI MSP430 library",
	.open		= tilib_open,
	.destroy	= tilib_destroy,
	.readmem	= tilib_readmem,
	.writemem	= tilib_writemem,
	.erase		= tilib_erase,
	.getregs	= tilib_getregs,
	.setregs	= tilib_setregs,
	.ctl		= tilib_ctl,
	.poll		= tilib_poll
};
