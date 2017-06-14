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
#include "tilib.h"
#include "tilib_api.h"
#include "thread.h"
#include "ctrlc.h"
#include "opdb.h"

struct tilib_device {
	struct device		base;

	thread_lock_t		mb_lock;
	uint32_t		mailbox;

	uint16_t		bp_handles[DEVICE_MAX_BREAKPOINTS];

	char			uifPath[1024];
	fperm_t			active_fperm;
};

static struct tilib_device *client_handle;

#define MID_SINGLE_STEP		0x01
#define MID_BREAKPOINT		0x02
#define MID_STORAGE		0x04
#define MID_STATE		0x08
#define MID_WARNING		0x10
#define MID_CPU_STOPPED		0x20

#define MID_HALT_ANY		(MID_BREAKPOINT | MID_CPU_STOPPED)

static const MessageID_t my_message_ids = {
	.uiMsgIdSingleStep	= MID_SINGLE_STEP,
	.uiMsgIdBreakpoint	= MID_BREAKPOINT,
	.uiMsgIdStorage		= MID_STORAGE,
	.uiMsgIdState		= MID_STATE,
	.uiMsgIdWarning		= MID_WARNING,
	.uiMsgIdCPUStopped	= MID_CPU_STOPPED
};

static void event_notify(unsigned int msg_id, unsigned int w_param,
			 long l_param, long client_handle_unused)
{
	(void)w_param;
	(void)l_param;
	(void)client_handle_unused;

	thread_lock_acquire(&client_handle->mb_lock);
	client_handle->mailbox |= msg_id;
	thread_lock_release(&client_handle->mb_lock);
}

static uint32_t event_fetch(struct tilib_device *dev)
{
	uint32_t ret;

	thread_lock_acquire(&dev->mb_lock);
	ret = dev->mailbox;
	dev->mailbox = 0;
	thread_lock_release(&dev->mb_lock);

	return ret;
}

static void report_error(struct tilib_device *dev, const char *what)
{
	long err = tilib_api->MSP430_Error_Number();
	const char *desc = tilib_api->MSP430_Error_String(err);

	printc_err("tilib: %s: %s (error = %ld)\n", what, desc, err);
}

static int refresh_fperm(struct tilib_device *dev)
{
	fperm_t fp = opdb_read_fperm();
	fperm_t delta = dev->active_fperm ^ fp;

	if (delta & FPERM_LOCKED_FLASH) {
		int opt = (fp & FPERM_LOCKED_FLASH) ? 1 : 0;

		printc_dbg("%s locked flash access\n",
			   opt ? "Enabling" : "Disabling");
		if (tilib_api->MSP430_Configure
				(LOCKED_FLASH_ACCESS, opt) < 0) {
			report_error(dev, "MSP430_Configure "
				     "(LOCKED_FLASH_ACCESS)\n");
			return -1;
		}
	}

	if (delta & FPERM_BSL) {
		int opt = (fp & FPERM_BSL) ? 1 : 0;

		printc_dbg("%s BSL access\n",
			   opt ? "Enabling" : "Disabling");
		if (tilib_api->MSP430_Configure
				(UNLOCK_BSL_MODE, opt) < 0) {
			report_error(dev, "MSP430_Configure "
				     "(UNLOCK_BSL_MODE)\n");
			return -1;
		}
	}

	dev->active_fperm = fp;
	return 0;
}

static int tilib_readmem(device_t dev_base, address_t addr,
			 uint8_t *mem, address_t len)
{
	struct tilib_device *dev = (struct tilib_device *)dev_base;

	if (tilib_api->MSP430_Memory(addr, (char *)mem, len, READ) < 0) {
		report_error(dev, "MSP430_Memory");
		return -1;
	}

	return 0;
}

static int tilib_writemem(device_t dev_base, address_t addr,
			  const uint8_t *mem, address_t len)
{
	struct tilib_device *dev = (struct tilib_device *)dev_base;

	refresh_fperm(dev);

	if (tilib_api->MSP430_Memory(addr,
				(char *)mem, len, WRITE) < 0) {
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

	if (type == DEVICE_ERASE_MAIN)
		address = 0xfffe;

	refresh_fperm(dev);

	/* We need to pass a non-zero length if we've selected segment
	 * erase.
	 */
	if (tilib_api->MSP430_Erase(ti_erase_type(type),
			address, 1) < 0) {
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

	if (tilib_api->MSP430_Registers(regbuf, 0xffff, READ) < 0) {
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

	if (tilib_api->MSP430_Registers(regbuf, 0xffff, WRITE) < 0) {
		report_error(dev, "MSP430_Registers");
		return -1;
	}

	return 0;
}

static void load_break(BpParameter_t *param, address_t addr)
{
	param->bpMode = BP_CODE;
	param->lAddrVal = addr;
	param->bpType = BP_MAB;
	param->lReg = 0; /* not used */
	param->bpAccess = BP_FETCH;
	param->bpAction = BP_BRK;
	param->bpOperat = BP_EQUAL;
	param->lMask = 0; /* what's this? */
	param->lRangeEndAdVa = 0; /* not used */
	param->bpRangeAction = 0; /* not used */
	param->bpCondition = BP_NO_COND;
	param->lCondMdbVal = 0;
	param->bpCondAccess = BP_FETCH;
	param->lCondMask = 0; /* what's this? */
	param->bpCondOperat = BP_EQUAL;
	param->wExtCombine = 0; /* not used? */
}

static void load_complex(BpParameter_t *param, address_t addr,
			 BpAccess_t acc)
{
	param->bpMode = BP_COMPLEX;
	param->lAddrVal = addr;
	param->bpType = BP_MAB;
	param->lReg = 0; /* not used (only for register-write) */
	param->bpAccess = acc;
	param->bpAction = BP_BRK;
	param->bpOperat = BP_EQUAL;
	param->lMask = 0xffffff;
	param->lRangeEndAdVa = 0; /* not used */
	param->bpRangeAction = 0; /* not used */
	param->bpCondition = BP_NO_COND;
	param->lCondMdbVal = 0;
	param->bpCondAccess = acc;
	param->lCondMask = 0; /* what's this? */
	param->bpCondOperat = BP_EQUAL;
	param->wExtCombine = 0; /* not used? */
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
			switch (bp->type) {
			case DEVICE_BPTYPE_BREAK:
				load_break(&param, bp->addr);
				break;

			case DEVICE_BPTYPE_WATCH:
				load_complex(&param, bp->addr, BP_NO_FETCH);
				break;

			case DEVICE_BPTYPE_READ:
				load_complex(&param, bp->addr,
					     BP_READ_DMA);
				break;

			case DEVICE_BPTYPE_WRITE:
				load_complex(&param, bp->addr,
					     BP_WRITE_DMA);
				break;
			}
		} else if (!dev->bp_handles[i]) {
			bp->flags &= ~DEVICE_BP_DIRTY;
			continue;
		} else {
			param.bpMode = BP_CLEAR;
		}

		if (tilib_api->MSP430_EEM_SetBreakpoint
			    (&dev->bp_handles[i], &param) < 0) {
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

	if (tilib_api->MSP430_State(&state, 1, &cycles) < 0) {
		report_error(dev, "MSP430_State");
		return -1;
	}

	/* Is this a blocking call? */
	return 0;
}

static int do_step(struct tilib_device *dev)
{
	if (tilib_api->MSP430_Run(SINGLE_STEP, 0) < 0) {
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
		if (tilib_api->MSP430_Reset(RST_RESET, 0, 0) < 0) {
			report_error(dev, "MSP430_Reset");
			return -1;
		}
		return 0;

        case DEVICE_CTL_RUN:
		if (refresh_bps(dev) < 0)
			return -1;

		if (tilib_api->MSP430_Run(RUN_TO_BREAKPOINT, 0) < 0) {
			report_error(dev, "MSP430_Run");
			return -1;
		}
		break;

        case DEVICE_CTL_HALT:
		return do_halt(dev);

        case DEVICE_CTL_STEP:
		return do_step(dev);

	case DEVICE_CTL_SECURE:
		if (tilib_api->MSP430_Secure() < 0) {
			report_error(dev, "MSP430_Secure");
			return -1;
		}
		return 0;
	}

	return 0;
}

static device_status_t tilib_poll(device_t dev_base)
{
	struct tilib_device *dev = (struct tilib_device *)dev_base;

        if (delay_ms(50) < 0)
                return DEVICE_STATUS_INTR;

	if (event_fetch(dev) & MID_HALT_ANY)
		return DEVICE_STATUS_HALTED;

	return DEVICE_STATUS_RUNNING;
}

static void tilib_destroy(device_t dev_base)
{
	struct tilib_device *dev = (struct tilib_device *)dev_base;

	printc_dbg("MSP430_Run\n");
	if (tilib_api->MSP430_Run(FREE_RUN, 1) < 0)
		report_error(dev, "MSP430_Run");

	printc_dbg("MSP430_Close\n");
	tilib_api->MSP430_Close(0);
	tilib_api_exit();
	thread_lock_destroy(&dev->mb_lock);
	free(dev);
}

static void fw_progress(unsigned int msg_id, unsigned long w_param,
			unsigned long l_param, long client_handle_unused)
{
	(void)l_param;
	(void)client_handle_unused;

	switch (msg_id) {
	case BL_DATA_BLOCK_PROGRAMMED:
		if (w_param > 100)
			w_param = 100;

		printc("   %3lu percent done\n", w_param);
		break;

	case BL_UPDATE_ERROR:
		report_error(client_handle, "BL_UPDATE_ERROR");
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

static int do_fw_update(struct tilib_device *dev, const char *filename)
{
	printc("Starting firmware update (this may take some time)...\n");
	if (tilib_api->MSP430_FET_FwUpdate((char *)filename,
				     fw_progress, (long)dev) < 0) {
		report_error(dev, "MSP430_FET_FwUpdate");
		return -1;
	}

	return 0;
}

static int do_init(struct tilib_device *dev, const struct device_args *args)
{
	long version;
	union DEVICE_T device;

	printc_dbg("MSP430_Initialize: %s\n", dev->uifPath);
	if (tilib_api->MSP430_Initialize(dev->uifPath, &version) < 0) {
		report_error(dev, "MSP430_Initialize");
		return -1;
	}

	if (args->require_fwupdate) {
		printc("Updating firmware using %s\n",
		       args->require_fwupdate);

		if (do_fw_update(dev, args->require_fwupdate) < 0) {
			tilib_api->MSP430_Close(0);
			return -1;
		}
	} else if (version < 0) {
		printc("FET firmware update is required.\n");

		if (args->flags & DEVICE_FLAG_DO_FWUPDATE) {
			if (do_fw_update(dev, NULL) < 0) {
				tilib_api->MSP430_Close(0);
				return -1;
			}
		} else {
			printc("Re-run with --allow-fw-update to perform "
			       "a firmware update.\n");
			tilib_api->MSP430_Close(0);
			return -1;
		}
	} else {
		printc_dbg("Firmware version is %ld\n", version);
	}

	printc_dbg("MSP430_VCC: %d mV\n", args->vcc_mv);
	if (tilib_api->MSP430_VCC(args->vcc_mv) < 0) {
		report_error(dev, "MSP430_VCC");
		tilib_api->MSP430_Close(0);
		return -1;
	}

	/* Without this delay, MSP430_OpenDevice will often hang. */
	delay_s(1);

	printc_dbg("MSP430_OpenDevice\n");
	if (tilib_api->MSP430_OpenDevice("DEVICE_UNKNOWN", "", 0, 0, 0) < 0) {
		report_error(dev, "MSP430_OpenDevice");
		tilib_api->MSP430_Close(0);
		return -1;
	}

	printc_dbg("MSP430_GetFoundDevice\n");
	if (tilib_api->MSP430_GetFoundDevice(device.buffer,
				       sizeof(device.buffer)) < 0) {
		report_error(dev, "MSP430_GetFoundDevice");
		tilib_api->MSP430_Close(0);
		return -1;
	}

	printc_dbg("Device: %s (id = 0x%04x)\n", device.string, device.id);
	printc_dbg("%d breakpoints available\n", device.nBreakpoints);
	dev->base.max_breakpoints = device.nBreakpoints;
	if (dev->base.max_breakpoints > DEVICE_MAX_BREAKPOINTS)
		dev->base.max_breakpoints = DEVICE_MAX_BREAKPOINTS;

	printc_dbg("MSP430_EEM_Init\n");
	thread_lock_init(&dev->mb_lock);
	if (tilib_api->MSP430_EEM_Init(event_notify, (long)dev,
				 (MessageID_t *)&my_message_ids) < 0) {
		report_error(dev, "MSP430_EEM_Init");
		tilib_api->MSP430_Close(0);
		thread_lock_destroy(&dev->mb_lock);
		return -1;
	}

	return 0;
}

static int do_findUif(struct tilib_device *dev)
{
	// Find the first uif and store the path name into dev->uifPath
	long attachedUifCount = 0;
	long uifIndex = 0;

	printc_dbg("MSP430_GetNumberOfUsbIfs\n");
	if (tilib_api->MSP430_GetNumberOfUsbIfs(&attachedUifCount) < 0) {
		report_error(dev, "MSP430_GetNumberOfUsbIfs");
		return -1;
	}

	for (uifIndex = 0; uifIndex < attachedUifCount; uifIndex++)
	{
		char *name = NULL;
		long status = 0;

		printc_dbg("MSP430_GetNameOfUsbIf\n");
		if (tilib_api->MSP430_GetNameOfUsbIf(uifIndex,
				&name, &status) < 0) {
			report_error(dev, "MSP430_GetNameOfUsbIf");
			return -1;
		}

		if (status == 0) /* status == 1 when fet is in use */
		{
			// This fet is unused
			strncpy(dev->uifPath, name, sizeof(dev->uifPath));
			printc_dbg("Found FET: %s\n", dev->uifPath);
			return 0;
		}
	}

	printc_err("No unused FET found.\n");

	return -1;
}

static device_t tilib_open(const struct device_args *args)
{
	struct tilib_device *dev;

	dev = malloc(sizeof(*dev));
	if (!dev) {
		printc_err("tilib: can't allocate memory: %s\n",
			   last_error());
		return NULL;
	}

	memset(dev, 0, sizeof(*dev));
	dev->base.type = &device_tilib;

	if (tilib_api_init() < 0) {
		free(dev);
		return NULL;
	}
	client_handle = dev;

	/* Copy the args->path to the dev->uifPath buffer
	 * we may need to change it for automatic detection, and
	 * not sure if the path is actually modified by MSP430_Initialize,
	 * but the argument isn't const, so probably safest to copy it.
	 */

	if ((args->flags & DEVICE_FLAG_TTY)) {
		strncpy(dev->uifPath, args->path, sizeof(dev->uifPath));
		dev->uifPath[sizeof(dev->uifPath) - 1] = 0;
	} else {
		// No path was supplied, use the first UIF we can find
		if (do_findUif(dev) < 0) {
			tilib_api_exit();
			free(dev);
			return NULL;
		}
	}

	if (do_init(dev, args) < 0) {
		printc_err("tilib: device initialization failed\n");
		tilib_api_exit();
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
	.poll		= tilib_poll,
	.getconfigfuses = NULL
};
