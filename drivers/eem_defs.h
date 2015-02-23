/*
 * EEM_defs.h
 * 
 * <FILEBRIEF>
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/ 
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


#ifndef _EEM_DEFS_H_
#define _EEM_DEFS_H_

#define FALSE       0
#define TRUE        1

#define WRITE       0
#define READ        1

#define TRIGFLAG    0x8E
#define EEMVER      0x86
/* Definition for EEM General Clock Control Register */
#define GENCLKCTRL  0x88
/* Definitions for EEM General Clock Control Register */
#define MCLK_SEL0   0x0000
#define SMCLK_SEL0  0x0000
#define ACLK_SEL0   0x0000
#define MCLK_SEL3   0x6000
#define SMCLK_SEL3  0x0C00
#define ACLK_SEL3   0x0180
#define STOP_MCLK   0x0008
#define STOP_SMCLK  0x0004
#define STOP_ACLK   0x0002
/* Definition for EEM Module Clock Control Register 0 */
#define MODCLKCTRL0 0x8A
/* Definition for EEM General Debug Control Register */
#define GENCTRL     0x82
/* Definitions for EEM General Debug Control Register */
#define EEM_EN         0x0001
#define CLEAR_STOP     0x0002
#define EMU_CLK_EN     0x0004
#define EMU_FEAT_EN    0x0008
#define DEB_TRIG_LATCH 0x0010
#define EEM_RST        0x0040
#define E_STOPPED      0x0080
/* Defintions for Trigger Block base addresses */
#define TB0         0x0000
#define TB1         0x0008
#define TB2         0x0010
#define TB3         0x0018
#define TB4         0x0020
#define TB5         0x0028
#define TB6         0x0030
#define TB7         0x0038
#define TB8         0x0040
#define TB9         0x0048
/* Definitions for Trigger Block register addresses */
#define MBTRIGxVAL  0x0000
#define MBTRIGxCTL  0x0002
#define MBTRIGxMSK  0x0004
#define MBTRIGxCMB  0x0006
/* Definitions for MAB/MDB Trigger Control Register */
#define MAB    0x0000
#define MDB    0x0001
#define TRIG_0 0x0000 // Instruction Fetch
#define TRIG_1 0x0002 // Instruction Fetch Hold
#define TRIG_2 0x0004 // No Instruction Fetch
#define TRIG_3 0x0006 // Don't care
#define TRIG_4 0x0020 // No Instruction Fetch & Read
#define TRIG_5 0x0022 // No Instruction Fetch & Write
#define TRIG_6 0x0024 // Read
#define TRIG_7 0x0026 // Write
#define TRIG_8 0x0040 // No Instruction Fetch & No DMA Access
#define TRIG_9 0x0042 // DMA Access (Read or Write)
#define TRIG_A 0x0044 // No DMA Access
#define TRIG_B 0x0046 // Write & No DMA Access
#define TRIG_C 0x0060 // No Instruction Fetch & Read & No DMA Access
#define TRIG_D 0x0062 // Read & No DMA Access
#define TRIG_E 0x0064 // Read & DMA Access
#define TRIG_F 0x0066 // Write & DMA Access
#define CMP_EQUAL     0x0000
#define CMP_GREATER   0x0008
#define CMP_LESS      0x0010
#define CMP_NOT_EQUAL 0x0018
/* Definitions for MAB/MDB Trigger Mask Register */
#define NO_MASK       0x00000
#define MASK_ALL      0xFFFFF
#define MASK_XADDR    0xF0000
#define MASK_HBYTE    0x0FF00
#define MASK_LBYTE    0x000FF
/* Definitions for MAB/MDB Combination Register & Reaction Registers*/
#define EN0     0x0001
#define EN1     0x0002
#define EN2     0x0004
#define EN3     0x0008
#define EN4     0x0010
#define EN5     0x0020
#define EN6     0x0040
#define EN7     0x0080
#define EN8     0x0100
#define EN9     0x0200

#define STOR_CTL      0x9E
/* Definitions for State Storage Control Register */
#define VAR_WATCH0          0x0000 // Two
#define VAR_WATCH1          0x2000 // Four
#define VAR_WATCH2          0x4000 // Six
#define VAR_WATCH3          0x6000 // Eight
#define STOR_FULL           0x0200
#define STOR_WRIT           0x0100
#define STOR_TEST           0x0080
#define STOR_RST            0x0040
#define STOR_STOP_ON_TRIG   0x0020
#define STOR_START_ON_TRIG  0x0010
#define STOR_ONE_SHOT       0x0008
#define STOR_MODE0          0x0000 // Store on enabled triggers
#define STOR_MODE1          0x0002 // Store on Instruction Fetch
#define STOR_MODE2          0x0004 // Variable Watch
#define STOR_MODE3          0x0006 // Store all bus cycles
#define STOR_EN             0x0001 // enable state storage

/* Definitions for Reaction Registers */
#define STOR_REACT    0x98
#define BREAKREACT    0x80
#define EVENT_REACT   0x94

#define EVENT_CTRL    0x96
#define EVENT_TRIG    0x0001

/* Definitions for Cycle Counters */
#define CCNT0CTL      0xB0
#define CCNT0L        0xB2
#define CCNT0H        0xB4
#define CCNT1CTL      0xB8
#define CCNT1L        0xBA
#define CCNT1H        0xBC
#define CCNT1REACT    0xBE

/* Definitions for Cycle Counter Control Register */
#define CCNTMODE0     0x0000 // Counter stopped
#define CCNTMODE1     0x0001 // Increment on reaction
#define CCNTMODE4     0x0004 // Increment on instruction fetch cycles
#define CCNTMODE5     0x0005 // Increment on all bus cycles (including DMA cycles)
#define CCNTMODE6     0x0006 // Increment on all CPU bus cycles (excluding DMA cycles)
#define CCNTMODE7     0x0007 // Increment on all DMA bus cycles
#define CCNT_RST      0x0040 
#define CCNTSTT0      0x0000 // Start when CPU released from JTAG/EEM
#define CCNTSTT1      0x0100 // Start on reaction CCNT1REACT (only CCNT1)
#define CCNTSTT2      0x0200 // Start when other (second) counter is started (only if available)
#define CCNTSTT3      0x0300 // Start immediately
#define CCNTSTP0      0x0000 // Stop when CPU is stopped by EEM or under JTAG control
#define CCNTSTP1      0x0400 // Stop on reaction CCNT1REACT (only CCNT1)
#define CCNTSTP2      0x0800 // Stop when other (second) counter is started (only if available)
#define CCNTSTP3      0x0C00 // No stop event
#define CCNTCLR0      0x0000 // No clear event
#define CCNTCLR1      0x1000 // Clear on reaction CCNT1REACT (only CCNT1)
#define CCNTCLR2      0x2000 // Clear when other (second) counter is started (only if available)
#define CCNTCLR3      0x3000 // Reserved

#define GCC_NONE      0x0000 // No clock control
#define GCC_STANDARD  0x0001 // Standard clock control
#define GCC_EXTENDED  0x0002 // Extended clock control

#endif
