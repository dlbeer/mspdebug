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

#ifndef TILIB_DEFS_H_
#define TILIB_DEFS_H_

/* This header file contains various constants used by the TI MSP430
 * library. The original copyright notice is:
 *
 * Copyright (C) 2004 - 2011 Texas Instruments Incorporated -
 *    http://www.ti.com/
 *
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *    Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>

/* this is the definition for the DLL functions return value */
typedef long STATUS_T;

typedef long LONG;
typedef unsigned long ULONG;
typedef char CHAR;
typedef uint16_t WORD;
typedef uint8_t BYTE;

enum READ_WRITE {
	WRITE = 0,
	READ = 1,
};

enum RESET_METHOD {
	PUC_RESET   = (1 << 0), /**< Power up clear (i.e., a "soft") reset */
	RST_RESET   = (1 << 1), /**< RST/NMI (i.e., "hard") reset */
	VCC_RESET   = (1 << 2), /**< Cycle Vcc (i.e., a "power on") reset */
	FORCE_RESET = (1 << 3)
};

/* FLASH erase type. */
enum ERASE_TYPE {
        ERASE_SEGMENT = 0, /**< Erase a segment */
        ERASE_MAIN = 1, /**< Erase all MAIN memory */
        ERASE_ALL = 2, /**< Erase all MAIN and INFORMATION memory */
};

/* Run modes. */
enum RUN_MODES {
	/* Run the device. Set breakpoints (if any) are disabled */
        FREE_RUN = 1,
        /* A single device instruction is executed. Interrupt
         * processing is supported
         */
        SINGLE_STEP = 2,
	/* Run the device. Set breakpoints (if any) are enabled */
        RUN_TO_BREAKPOINT = 3
};

/* State modes. */
enum STATE_MODES {
	/* The device is stopped */
        STOPPED = 0,
	/* The device is running or is being single stepped */
        RUNNING = 1,
	/* The device is stopped after the single step operation is
         * complete */
        SINGLE_STEP_COMPLETE = 2,
	/* The device is stopped as a result of hitting an enabled
	 * breakpoint */
        BREAKPOINT_HIT = 3,
	/* The device is in LPMx.5 low power mode  */
        LPMX5_MODE = 4,
	/* The device woke up from LPMx.5 low power mode */
        LPMX5_WAKEUP = 5
};

/* Configurations to set with MSP430_Configure. */
enum CONFIG_MODE {
	/* Verify data downloaded to FLASH memories */
        VERIFICATION_MODE = 0,
	/* 4xx emulation mode */
        EMULATION_MODE = 1,
	/* Clock control mode (on emulation stop) */
        CLK_CNTRL_MODE = 2,
	/* Module Clock control mode (on emulation stop) */
        MCLK_CNTRL_MODE = 3,
	/* Flash test mode for Automotive Devices - Marginal Read */
        FLASH_TEST_MODE = 4,
	/* Allows Locked Info Mem Segment A access (if set to '1') */
        LOCKED_FLASH_ACCESS = 5,
	/* Flash Swop mode for Automotive Devices */
        FLASH_SWOP = 6,
	/* Trace mode in EDT file format */
        EDT_TRACE_MODE = 7,
	/* Configure interface protocol: JTAG or Spy-bi-Wire
         * (see enum INTERFACE_TYPE) */
        INTERFACE_MODE = 8,

        /* Configure a value that will be placed on the devices'
         * MemoryDataBus right before the device gets released from JTAG.
         * Useful fro supporting Emulated Hardware Breakpoints.
	 */
        SET_MDB_BEFORE_RUN = 9,

	/*
         * Configure whether RAM content should be preserved/restored
         * in MSP430_Erase() and MSP430_Memory() or not.
         * RAM_PRESERVE_MODE is set to ENABLE by default.
         * Usage Example for initial flash programming:
         * (1) MSP430_Configure(RAM_PRESERVE_MODE, DISABLE);
         * (2) MSP430_Erase(ERASE_ALL,..,..);
         * (3) MSP430_Memory(..., ..., ..., WRITE );
         * (4) MSP430_Memory(..., ..., ..., READ );
         * ..... Flash Programming/Download finished
         * (n) MSP430_Configure(RAM_PRESERVE_MODE, ENABLE);
	 */
	RAM_PRESERVE_MODE = 10,

	/* Configure the DLL to allow read/write/erase access to the 5xx
	 * Bootstrap Loader (BSL) memory segments. */
	UNLOCK_BSL_MODE =11,
	/* just used internal for the device code of L092 and C092 */
	DEVICE_CODE = 12,
	/* set true to write the external SPI image of the L092 */
	WRITE_EXTERNAL_MEMORY = 13,
	/* set DEBUG_LPM_X true to start debugging of LPMx.5 */
	DEBUG_LPM_X = 14
};

typedef void (*DLL430_EVENTNOTIFY_FUNC)
  (unsigned int MsgId, unsigned int wParam, long lParam, long clientHandle);

typedef struct MESSAGE_ID {
	ULONG	uiMsgIdSingleStep;
	ULONG	uiMsgIdBreakpoint;
	ULONG	uiMsgIdStorage;
	ULONG	uiMsgIdState;
	ULONG	uiMsgIdWarning;
	ULONG	uiMsgIdCPUStopped;
} MessageID_t;

typedef enum BpMode {
	BP_CLEAR = 0,
	BP_CODE = 1,
	BP_RANGE = 2,
	BP_COMPLEX = 3
} BpMode_t;

typedef enum BpType {
	BP_MAB = 0,
	BP_MDB = 1,
	BP_REGISTER = 2
} BpType_t;

typedef enum BpAccess {
	BP_FETCH = 0,
	BP_FETCH_HOLD = 1,
	BP_NO_FETCH = 2,
	BP_DONT_CARE = 3,
	BP_NO_FETCH_READ = 4,
	BP_NO_FETCH_WRITE = 5,
	BP_READ = 6,
	BP_WRITE = 7,
	BP_NO_FETCH_NO_DMA = 8,
	BP_DMA = 9,
	BP_NO_DMA = 10,
	BP_WRITE_NO_DMA = 11,
	BP_NO_FETCH_READ_NO_DMA = 12,
	BP_READ_NO_DMA = 13,
	BP_READ_DMA = 14,
	BP_WRITE_DMA = 15
} BpAccess_t;

typedef enum BpOperat {
	BP_EQUAL = 0,
	BP_GREATER = 1,
	BP_LOWER = 2,
	BP_UNEQUAL = 3
} BpOperat_t;

typedef enum BpRangeAction {
	BP_INSIDE = 0,
	BP_OUTSIDE = 1
} BpRangeAction_t;

typedef enum BpCondition {
	BP_NO_COND = 0,
	BP_COND = 1
} BpCondition_t;

typedef enum BpAction {
	BP_NONE = 0,
	BP_BRK = 1,
	BP_STO = 2,
	BP_BRK_STO = 3
} BpAction_t;

typedef struct BREAKPOINT {
	/* Breakpoint modes */
	BpMode_t          bpMode;
	/* Breakpoint address/value (ignored for clear breakpoint) */
	LONG              lAddrVal;
	/* Breakpoint type (used for range and complex breakpoints) */
	BpType_t          bpType;
	/* Breakpoint register (used for complex breakpoints with
         * register-write trigger) */
	LONG              lReg;
	/* Breakpoint access (used only for range and complex
	 * breakpoints) */
	BpAccess_t        bpAccess;
	/* Breakpoint action (break/storage) (used for range and complex
	 * breakpoints) */
	BpAction_t        bpAction;
	/* Breakpoint operator (used for complex breakpoints) */
	BpOperat_t        bpOperat;
	/* Breakpoint mask (used for complex breakpoints) */
	LONG              lMask;
	/* Range breakpoint end address (used for range breakpoints) */
	LONG              lRangeEndAdVa;
	/* Range breakpoint action (inside/outside) (used for range
	 * breakpoints) */
	BpRangeAction_t   bpRangeAction;
	/* Complex breakpoint: Condition available */
	BpCondition_t     bpCondition;
	/* Complex breakpoint: MDB value (used for complex breakpoints) */
	ULONG             lCondMdbVal;
	/* Complex breakpoint: Access (used for complex breakpoints) */
	BpAccess_t        bpCondAccess;
	/* Complex breakpoint: Mask Value(used for complex breakpoints) */
	LONG              lCondMask;
	/* Complex breakpoint: Operator (used for complex breakpoints) */
	BpOperat_t        bpCondOperat;
	/* Combine breakpoint: Reference of a combination handle */
	WORD              wExtCombine;
} BpParameter_t;

typedef void (*DLL430_FET_NOTIFY_FUNC) (unsigned int MsgId,
                                        unsigned long wParam,
                                        unsigned long lParam,
                                        long clientHandle);

typedef enum UPDATE_STATUS_MESSAGES {
	/* Initializing Update Bootloader */
	BL_INIT = 0,
	/* Erasing mapped interrupt vectors */
	BL_ERASE_INT_VECTORS = 1,
	/* Erasing firmware memory section */
	BL_ERASE_FIRMWARE = 2,
	/* Program new firmware */
	BL_PROGRAM_FIRMWARE = 3,
	/* One data block of the new firmware was successfully programmed */
	BL_DATA_BLOCK_PROGRAMMED = 4,
	/* Exit Update Bootlader and reboot firmware */
	BL_EXIT = 5,
	/* Update was successfully finished */
	BL_UPDATE_DONE = 6,
	/* An error occured during firmware update */
	BL_UPDATE_ERROR = 7,
	/* An error occured during firmware update */
	BL_WAIT_FOR_TIMEOUT = 8
} UPDATE_STATUS_MESSAGES_t;

union DEVICE_T {
	/* this buffer holds the complete device information */
	/* and is overlayed by the following information structure */
	CHAR buffer[110];
	struct {  /* actually 106 Bytes */
		/* The value 0xaa55. */
		WORD  endian;
		/* Identification number. */
		WORD  id;
		/* Identification string. */
		BYTE  string[32];
		/* MAIN MEMORY (FLASH) starting address. */
		WORD  mainStart;
		/* INFORMATION MEMORY (FLASH) starting address. */
		WORD  infoStart;
		/* RAM ending address. */
		WORD  ramEnd;
		/* Number of breakpoints. */
		WORD  nBreakpoints;
		/* Emulation level. */
		WORD  emulation;
		/* Clock control level. */
		WORD  clockControl;
		/* LCD starting address. */
		WORD  lcdStart;
		/* LCD ending address. */
		WORD  lcdEnd;
		/* Vcc minimum during operation [mVolts]. */
		WORD  vccMinOp;
		/* Vcc maximum during operation [mVolts]. */
		WORD  vccMaxOp;
		/* Device has TEST/VPP. */
		WORD  hasTestVpp;
		/* RAM starting address. */
		WORD  ramStart;
		/* RAM2 starting address. */
		WORD  ram2Start;
		/* RAM2 ending address. */
		WORD  ram2End;
		/* INFO ending address. */
		WORD  infoEnd;
		/* MAIN ending address. */
		ULONG mainEnd;
		/* BSL starting  address. */
		WORD  bslStart;
		/* BSL ending address. */
		WORD  bslEnd;
		/* Number of CPU Register Trigger. */
		WORD  nRegTrigger;
		/* Number of EEM Trigger Combinations. */
		WORD  nCombinations;
		/* The MSP430 architecture (non-X, X or Xv2). */
		BYTE  cpuArch;
		/* The JTAG ID - value returned on an instruction shift. */
		BYTE  jtagId;
		/* The CoreIP ID. */
		WORD  coreIpId;
		/* The Device-ID Pointer. */
		ULONG deviceIdPtr;
		/* The EEM Version Number. */
		WORD  eemVersion;
		/*  Breakpoint Modes */
		WORD nBreakpointsOptions;
		WORD nBreakpointsReadWrite;
		WORD nBreakpointsDma;
		/* Trigger Mask for Breakpoint */
		WORD TrigerMask;
		/* Register Trigger modes */
		WORD nRegTriggerOperations;
		/* MSP430 has Stage Storage */
		WORD nStateStorage ;
		/* Numbr of cycle counters of MSP430 */
		WORD nCycleCounter;
		/* Cycle couter modes */
		WORD nCycleCounterOperations;
		/* Msp430 has Sqeuncer */
		WORD nSequencer;
		/* Msp430 has FRAM Memroy */
		WORD HasFramMemroy;
	} __attribute__((packed));
};

#endif
