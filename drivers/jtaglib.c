/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2012 Daniel Beer
 * Copyright (C) 2012-2015 Peter Bägel
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

/* jtag functions are taken from TIs SLAA149–September 2002
  *
 * breakpoint implementation influenced by a posting of Ruisheng Lin
 * to Travis Goodspeed at 2012-09-20 found at:
 * http://sourceforge.net/p/goodfet/mailman/message/29860790/
 *
 * 2012-10-03 Peter Bägel (DF5EQ)
 * 2012-10-03   initial release              Peter Bägel (DF5EQ)
 * 2014-12-26   jtag_single_step added       Peter Bägel (DF5EQ)
 *              jtag_read_reg    corrected
 *              jtag_write_reg   corrected
 * 2015-02-21   jtag_set_breakpoint added    Peter Bägel (DF5EQ)
 *              jtag_cpu_state      added
 */

#include <stdlib.h>
#include "jtaglib.h"
#include "output.h"
#include "eem_defs.h"

/* JTAG identification value for all existing Flash-based MSP430 devices
 */
#define JTAG_ID 0x89

/* Instructions for the JTAG control signal register in reverse bit order
 */
#define IR_CNTRL_SIG_16BIT	0xC8	/* 0x13 */
#define IR_CNTRL_SIG_CAPTURE	0x28	/* 0x14 */
#define IR_CNTRL_SIG_RELEASE	0xA8	/* 0x15 */
/* Instructions for the JTAG data register */
#define IR_DATA_16BIT		0x82	/* 0x41 */
#define IR_DATA_CAPTURE		0x42	/* 0x42 */
#define IR_DATA_QUICK		0xC2	/* 0x43 */
/* Instructions for the JTAG address register */
#define IR_ADDR_16BIT		0xC1	/* 0x83 */
#define IR_ADDR_CAPTURE		0x21	/* 0x84 */
#define IR_DATA_TO_ADDR		0xA1	/* 0x85 */
/* Instructions for the JTAG PSA mode */
#define IR_DATA_PSA		0x22	/* 0x44 */
#define IR_SHIFT_OUT_PSA	0x62	/* 0x46 */
/* Instructions for the JTAG Fuse */
#define IR_PREPARE_BLOW		0x44	/* 0x22 */
#define IR_EX_BLOW		0x24	/* 0x24 */
/* Instructions for the Configuration Fuse */
#define IR_CONFIG_FUSES	0x94
/* Bypass instruction */
#define IR_BYPASS		0xFF	/* 0xFF */
/* Instructions for the EEM */
#define IR_EMEX_DATA_EXCHANGE	0x90 /* 0x09 */
#define IR_EMEX_WRITE_CONTROL	0x30 /* 0x0C */
#define IR_EMEX_READ_CONTROL	0xD0 /* 0x0B */

#define jtag_tms_set(p)		p->f->jtdev_tms(p, 1)
#define jtag_tms_clr(p)		p->f->jtdev_tms(p, 0)
#define jtag_tck_set(p)		p->f->jtdev_tck(p, 1)
#define jtag_tck_clr(p)		p->f->jtdev_tck(p, 0)
#define jtag_tdi_set(p)		p->f->jtdev_tdi(p, 1)
#define jtag_tdi_clr(p)		p->f->jtdev_tdi(p, 0)
#define jtag_tclk_set(p)	p->f->jtdev_tclk(p, 1)
#define jtag_tclk_clr(p)	p->f->jtdev_tclk(p, 0)
#define jtag_rst_set(p)		p->f->jtdev_rst(p, 1)
#define jtag_rst_clr(p)		p->f->jtdev_rst(p, 0)
#define jtag_tst_set(p)		p->f->jtdev_tst(p, 1)
#define jtag_tst_clr(p)		p->f->jtdev_tst(p, 0)

#define jtag_led_green_on(p)	p->f->jtdev_led_green(p, 1)
#define jtag_led_green_off(p)	p->f->jtdev_led_green(p, 0)
#define jtag_led_red_on(p)	p->f->jtdev_led_red(p, 1)
#define jtag_led_red_off(p)	p->f->jtdev_led_red(p, 0)

/* Reset target JTAG interface and perform fuse-HW check */
static void jtag_reset_tap(struct jtdev *p)
{
	int loop_counter;

	jtag_tms_set(p);
	jtag_tck_set(p);

	/* Perform fuse check */
	jtag_tms_clr(p);
	jtag_tms_set(p);
	jtag_tms_clr(p);
	jtag_tms_set(p);

	/* Reset JTAG state machine */
	for (loop_counter = 6; loop_counter > 0; loop_counter--) {
		jtag_tck_clr(p);
		jtag_tck_set(p);

		if (p->failed)
			return;
	}

	/* Set JTAG state machine to Run-Test/IDLE */
	jtag_tck_clr(p);
	jtag_tms_clr(p);
	jtag_tck_set(p);
}

/* This function sets the target JTAG state machine
 * back into the Run-Test/Idle state after a shift access
 */
static void jtag_tclk_prep (struct jtdev *p)
{
	/* JTAG state = Exit-DR */
	jtag_tck_clr(p);
	jtag_tck_set(p);

	/* JTAG state = Update-DR */
	jtag_tms_clr(p);
	jtag_tck_clr(p);
	jtag_tck_set(p);

	/* JTAG state = Run-Test/Idle */
}

/* Shift a value into TDI (MSB first) and simultaneously
 * shift out a value from TDO (MSB first)
 * num_bits: number of bits to shift
 * data_out: data to be shifted out
 * return  : scanned TDO value
 */
static unsigned int jtag_shift( struct jtdev *p,
				unsigned char num_bits,
				unsigned int  data_out )
{
	unsigned int data_in;
	unsigned int mask;
	unsigned int tclk_save;

	tclk_save = p->f->jtdev_tclk_get(p);

	data_in = 0;
	for (mask = 0x0001U << (num_bits - 1); mask != 0; mask >>= 1) {
		if ((data_out & mask) != 0)
			jtag_tdi_set(p);
		else
			jtag_tdi_clr(p);

		if (mask == 1)
			jtag_tms_set(p);

		jtag_tck_clr(p);
		jtag_tck_set(p);

		if (p->f->jtdev_tdo_get(p) == 1)
			data_in |= mask;
	}

	p->f->jtdev_tclk(p, tclk_save);

	/* Set JTAG state back to Run-Test/Idle */
	jtag_tclk_prep(p);

	return data_in;
}

/* Shifts a new instruction into the JTAG instruction register through TDI
 * MSB first, with interchanged MSB/LSB, to use the shifting function
 * instruction: 8 bit instruction
 * return     : scanned TDO value
 */
static unsigned int jtag_ir_shift(struct jtdev *p, unsigned int instruction)
{
	/* JTAG state = Run-Test/Idle */
	jtag_tms_set(p);
	jtag_tck_clr(p);
	jtag_tck_set(p);

	/* JTAG state = Select DR-Scan */
	jtag_tck_clr(p);
	jtag_tck_set(p);

	/* JTAG state = Select IR-Scan */
	jtag_tms_clr(p);
	jtag_tck_clr(p);
	jtag_tck_set(p);

	/* JTAG state = Capture-IR */
	jtag_tck_clr(p);
	jtag_tck_set(p);

	/* JTAG state = Shift-IR, Shift in TDI (8-bit) */
	return jtag_shift(p, 8, instruction);

	/* JTAG state = Run-Test/Idle */
}

/* Shifts a given 8-bit byte into the JTAG data register through TDI.
 * data  : 8 bit data
 * return: scanned TDO value
 */
static unsigned int jtag_dr_shift_8(struct jtdev *p, unsigned int data)
{
	/* JTAG state = Run-Test/Idle */
	jtag_tms_set(p);
	jtag_tck_clr(p);
	jtag_tck_set(p);

	/* JTAG state = Select DR-Scan */
	jtag_tms_clr(p);
	jtag_tck_clr(p);
	jtag_tck_set(p);

	/* JTAG state = Capture-DR */
	jtag_tck_clr(p);
	jtag_tck_set(p);

	/* JTAG state = Shift-DR, Shift in TDI (16-bit) */
	return jtag_shift(p, 8, data);

	/* JTAG state = Run-Test/Idle */
}

/* Shifts a given 16-bit word into the JTAG data register through TDI.
 * data  : 16 bit data
 * return: scanned TDO value
 */
static unsigned int jtag_dr_shift_16(struct jtdev *p, unsigned int data)
{
	/* JTAG state = Run-Test/Idle */
	jtag_tms_set(p);
	jtag_tck_clr(p);
	jtag_tck_set(p);

	/* JTAG state = Select DR-Scan */
	jtag_tms_clr(p);
	jtag_tck_clr(p);
	jtag_tck_set(p);

	/* JTAG state = Capture-DR */
	jtag_tck_clr(p);
	jtag_tck_set(p);

	/* JTAG state = Shift-DR, Shift in TDI (16-bit) */
	return jtag_shift(p, 16, data);

	/* JTAG state = Run-Test/Idle */
}

/* Set target CPU JTAG state machine into the instruction fetch state
 * return: 1 - instruction fetch was set
 *         0 - otherwise
 */
static int jtag_set_instruction_fetch(struct jtdev *p)
{
	unsigned int loop_counter;

	jtag_ir_shift(p, IR_CNTRL_SIG_CAPTURE);
	/* Wait until CPU is in instruction fetch state
	 * timeout after limited attempts
	 */
	for (loop_counter = 50; loop_counter > 0; loop_counter--) {
		if ((jtag_dr_shift_16(p, 0x0000) & 0x0080) == 0x0080)
			return 1;

		jtag_tclk_clr(p); /* The TCLK pulse befor jtag_dr_shift_16 leads to   */
		jtag_tclk_set(p); /* problems at MEM_QUICK_READ, it's from SLAU265 */
	}

	printc_err("jtag_set_instruction_fetch: failed\n");
	p->failed = 1;

	return 0;
}

/* Set the CPU into a controlled stop state */
static void jtag_halt_cpu(struct jtdev *p)
{
	/* Set CPU into instruction fetch mode */
	jtag_set_instruction_fetch(p);

	/* Set device into JTAG mode + read */
	jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);
	jtag_dr_shift_16(p, 0x2401);

	/* Send JMP $ instruction to keep CPU from changing the state */
	jtag_ir_shift(p, IR_DATA_16BIT);
	jtag_dr_shift_16(p, 0x3FFF);
	jtag_tclk_set(p);
	jtag_tclk_clr(p);

	/* Set JTAG_HALT bit */
	jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);
	jtag_dr_shift_16(p, 0x2409);
	jtag_tclk_set(p);
}

/* Release the target CPU from the controlled stop state */
static void jtag_release_cpu(struct jtdev *p)
{
	jtag_tclk_clr(p);

	/* clear the HALT_JTAG bit */
	jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);
	jtag_dr_shift_16(p, 0x2401);
	jtag_ir_shift(p, IR_ADDR_CAPTURE);
	jtag_tclk_set(p);
}

/* Compares the computed PSA (Pseudo Signature Analysis) value to the PSA
 * value shifted out from the target device. It is used for very fast data
 * block write or erasure verification.
 * start_address: start of data
 * length       : number of data
 * data         : pointer to data, 0 for erase check
 * RETURN       : 1 - comparison was successful
 *                0 - otherwise
 */
static int jtag_verify_psa(struct jtdev *p,
			   unsigned int start_address,
			   unsigned int length,
			   const uint16_t *data)
{
	unsigned int psa_value;
	unsigned int index;

	/* Polynom value for PSA calculation */
	unsigned int polynom = 0x0805;
	/* Start value for PSA calculation */
	unsigned int psa_crc = start_address-2;

	jtag_execute_puc(p);
	jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);
	jtag_dr_shift_16(p, 0x2401);
	jtag_set_instruction_fetch(p);
	jtag_ir_shift(p, IR_DATA_16BIT);
	jtag_dr_shift_16(p, 0x4030);
	jtag_tclk_set(p);
	jtag_tclk_clr(p);
	jtag_dr_shift_16(p, start_address-2);
	jtag_tclk_set(p);
	jtag_tclk_clr(p);
	jtag_tclk_set(p);
	jtag_tclk_clr(p);
	jtag_tclk_set(p);
	jtag_tclk_clr(p);
	jtag_ir_shift(p, IR_ADDR_CAPTURE);
	jtag_dr_shift_16(p, 0x0000);
	jtag_ir_shift(p, IR_DATA_PSA);

	for (index = 0; index < length; index++) {
		/* Calculate the PSA value */
		if ((psa_crc & 0x8000) == 0x8000) {
			psa_crc  ^= polynom;
			psa_crc <<= 1;
			psa_crc  |= 0x0001;
		} else
			psa_crc <<= 1;

		if (data == 0)
			/* use erase check mask */
			psa_crc ^= 0xFFFF;
		else
			/* use data */
			psa_crc ^= data[index];

		/* Clock through the PSA */
		jtag_tclk_set(p);
		jtag_tck_clr(p);
		jtag_tms_set(p);
		jtag_tck_set(p); /* Select DR scan */
		jtag_tck_clr(p);
		jtag_tms_clr(p);
		jtag_tck_set(p); /* Capture DR */
		jtag_tck_clr(p);
		jtag_tck_set(p); /* Shift DR */
		jtag_tck_clr(p);
		jtag_tms_set(p);
		jtag_tck_set(p); /* Exit DR */
		jtag_tck_clr(p);
		jtag_tck_set(p);
		jtag_tms_clr(p);
		jtag_tck_clr(p);
		jtag_tck_set(p);
		jtag_tclk_clr(p);
	}

	/* Read out the PSA value */
	jtag_ir_shift(p, IR_SHIFT_OUT_PSA);
	psa_value = jtag_dr_shift_16(p, 0x0000);
	jtag_tclk_set(p);

	return (psa_value == psa_crc) ? 1 : 0;
}

/* Take target device under JTAG control.
 * Disable the target watchdog.
 * return: 0 - fuse is blown
 *        >0 - jtag id
 */
unsigned int jtag_init(struct jtdev *p)
{
	unsigned int jtag_id;

	jtag_rst_clr(p);
	p->f->jtdev_power_on(p);
	jtag_tdi_set(p);
	jtag_tms_set(p);
	jtag_tck_set(p);
	jtag_tclk_set(p);

	jtag_rst_set(p);
	jtag_tst_clr(p);

	jtag_tst_set(p);
	jtag_rst_clr(p);
	jtag_tst_clr(p);

	jtag_tst_set(p);

	p->f->jtdev_connect(p);
	jtag_rst_set(p);
	jtag_reset_tap(p);

	/* Check fuse */
	if (jtag_is_fuse_blown(p)) {
		printc_err("jtag_init: fuse is blown\n");
		p->failed = 1;
		return 0;
	}

	/* Set device into JTAG mode */
	jtag_id = jtag_get_device(p);
	if (jtag_id == 0) {
		printc_err("jtag_init: invalid jtag_id: 0x%02x\n", jtag_id);
		p->failed = 1;
		return 0;
	}

	/* Perform PUC, includes target watchdog disable */
	if (jtag_execute_puc(p) != jtag_id) {
		printc_err("jtag_init: PUC failed\n");
		p->failed = 1;
		return 0;
	}

	return jtag_id;
}

unsigned int jtag_get_device(struct jtdev *p)
{
	unsigned int jtag_id = 0;
	unsigned int loop_counter;

	/* Set device into JTAG mode + read */
	jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);
	jtag_dr_shift_16(p, 0x2401);

	/* Wait until CPU is synchronized,
	 * timeout after a limited number of attempts
	 */
	jtag_id = jtag_ir_shift(p, IR_CNTRL_SIG_CAPTURE);
	for ( loop_counter = 50; loop_counter > 0; loop_counter--) {
		if ( (jtag_dr_shift_16(p, 0x0000) & 0x0200) == 0x0200 ) {
			break;
		}
	}

	if (loop_counter == 0) {
		printc_err("jtag_get_device: timed out\n");
		p->failed = 1;
		/* timeout reached */
		return 0;
	}

	jtag_led_green_on(p);
	return jtag_id;
}

/* Read the target chip id.
 * return: chip id
 */
unsigned int jtag_chip_id(struct jtdev *p)
{
	unsigned short chip_id;

	/* Read id from address 0x0ff0 */
	chip_id = jtag_read_mem(p, 16, 0x0FF0);

	/* High / low byte are stored in reverse order */
	chip_id = (chip_id << 8) + (chip_id >> 8);

	return chip_id;
}

/* Reads one byte/word from a given address
 * format : 8-byte, 16-word
 * address: address of memory
 * return : content of memory
 */
uint16_t jtag_read_mem(struct jtdev *p,
		       unsigned int format,
		       address_t address)
{
	uint16_t content;

	jtag_halt_cpu(p);
	jtag_tclk_clr(p);
	jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);
	if (format == 16) {
		/* set word read */
		jtag_dr_shift_16(p, 0x2409);
	} else {
		/* set byte read */
		jtag_dr_shift_16(p, 0x2419);
	}
	/* set address */
	jtag_ir_shift(p, IR_ADDR_16BIT);
	jtag_dr_shift_16(p, address);
	jtag_ir_shift(p, IR_DATA_TO_ADDR);
	jtag_tclk_set(p);
	jtag_tclk_clr(p);

	/* shift out 16 bits */
	content = jtag_dr_shift_16(p, 0x0000);
	jtag_tclk_set(p); /* is also the first instruction in jtag_release_cpu() */
	jtag_release_cpu(p);
	if (format == 8)
		content &= 0x00ff;

	return content;
}

/* Reads an array of words from target memory
 * address: address to read from
 * length : number of word to read
 * data   : memory to write to
 */
void jtag_read_mem_quick(struct jtdev *p,
			 address_t address,
			 unsigned int length,
			 uint16_t *data)
{
	unsigned int index;

	/* Initialize reading: */
	jtag_write_reg(p, 0,address-4);
	jtag_halt_cpu(p);
	jtag_tclk_clr(p);

	/* set RW to read */
	jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);
	jtag_dr_shift_16(p, 0x2409);
	jtag_ir_shift(p, IR_DATA_QUICK);

	for (index = 0; index < length; index++) {
		jtag_tclk_set(p);
		jtag_tclk_clr(p);
		/* shift out the data from the target */
		data[index] = jtag_dr_shift_16(p, 0x0000);
	}

	jtag_tclk_set(p);
	jtag_release_cpu(p);
}

/* Writes one byte/word at a given address
 * format : 8-byte, 16-word
 * address: address to be written
 * data   : data to write
 */
void jtag_write_mem(struct jtdev *p,
		    unsigned int format,
		    address_t address,
		    uint16_t data)
{
	jtag_halt_cpu(p);
	jtag_tclk_clr(p);
	jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);

	if (format == 16)
		/* Set word write */
		jtag_dr_shift_16(p, 0x2408);
	else
		/* Set byte write */
		jtag_dr_shift_16(p, 0x2418);

	jtag_ir_shift(p, IR_ADDR_16BIT);

	/* Set addr */
	jtag_dr_shift_16(p, address);
	jtag_ir_shift(p, IR_DATA_TO_ADDR);

	/* Shift in 16 bits */
	jtag_dr_shift_16(p, data);
	jtag_tclk_set(p);
	jtag_release_cpu(p);
}

/* Writes an array of words into target memory
 * address: address to write to
 * length : number of word to write
 * data   : data to write
 */
void jtag_write_mem_quick(struct jtdev *p,
			  address_t address,
			  unsigned int length,
			  const uint16_t *data)
{
	unsigned int index;

	/* Initialize writing */
	jtag_write_reg(p, 0, address-4);
	jtag_halt_cpu(p);
	jtag_tclk_clr(p);
	jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);

	/* Set RW to write */
	jtag_dr_shift_16(p, 0x2408);
	jtag_ir_shift(p, IR_DATA_QUICK);

	for (index = 0; index < length; index++) {
		/* Write data */
		jtag_dr_shift_16(p, data[index]);

		/* Increment PC by 2 */
		jtag_tclk_set(p);
		jtag_tclk_clr(p);
	}

	jtag_tclk_set(p);
	jtag_release_cpu(p);
}

/* This function checks if the JTAG access security fuse is blown
 * return: 1 - fuse is blown
 *         0 - otherwise
 */
int jtag_is_fuse_blown (struct jtdev *p)
{
	unsigned int loop_counter;

	/* First trial could be wrong */
	for (loop_counter = 3; loop_counter > 0; loop_counter--) {
		jtag_ir_shift(p, IR_CNTRL_SIG_CAPTURE);
		if (jtag_dr_shift_16(p, 0xAAAA) == 0x5555)
			/* Fuse is blown */
			return 1;
	}

	/* Fuse is not blown */
	return 0;
}

/* Execute a Power-Up Clear (PUC) using JTAG CNTRL SIG register
 * return: JTAG ID
 */
unsigned int jtag_execute_puc(struct jtdev *p)
{
	unsigned int jtag_id;

	jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);

	/* Apply and remove reset */
	jtag_dr_shift_16(p, 0x2C01);
	jtag_dr_shift_16(p, 0x2401);
	jtag_tclk_clr(p);
	jtag_tclk_set(p);
	jtag_tclk_clr(p);
	jtag_tclk_set(p);
	jtag_tclk_clr(p);
	jtag_tclk_set(p);

	/* Read jtag id */
	jtag_id = jtag_ir_shift(p, IR_ADDR_CAPTURE);

	/* Disable watchdog on target device */
	jtag_write_mem(p, 16, 0x0120, 0x5A80);

	return jtag_id;
}

/* Release the target device from JTAG control
 * address: 0xFFFE - perform Reset,
 *                   load Reset Vector into PC
 *          0xFFFF - start execution at current
 *                   PC position
 *          other  - load Address into PC
 */
void jtag_release_device(struct jtdev *p, address_t address)
{
	jtag_led_green_off(p);

	switch (address) {
		case 0xffff: /* Nothing to do */
			break;
		case 0xfffe: /* Perform reset */
			/* delete all breakpoints */
			jtag_set_breakpoint(p,-1,0);
			/* issue reset */
			jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);
			jtag_dr_shift_16(p, 0x2C01);
			jtag_dr_shift_16(p, 0x2401);
			break;
		default: /* Set target CPU's PC */
			jtag_write_reg(p, 0, address);
			break;
	}

	jtag_set_instruction_fetch(p);

	jtag_ir_shift(p, IR_EMEX_DATA_EXCHANGE);
	jtag_dr_shift_16(p, BREAKREACT + READ);
	jtag_dr_shift_16(p, 0x0000);

	jtag_ir_shift(p, IR_EMEX_WRITE_CONTROL);
	jtag_dr_shift_16(p, 0x000f);

	jtag_ir_shift(p, IR_CNTRL_SIG_RELEASE);
}

/* Performs a verification over the given memory range
 * return: 1 -  verification was successful
 *         0 -  otherwise
 */
int jtag_verify_mem(struct jtdev *p,
		    address_t start_address,
		    unsigned int length,
		    const uint16_t *data)
{
	return jtag_verify_psa(p, start_address, length, data);
}

/* Performs an erase check over the given memory range
 * return: 1 - erase check was successful
 *         0 - otherwise
 */
int jtag_erase_check(struct jtdev *p,
		     address_t start_address,
		     unsigned int length)
{
	return jtag_verify_psa(p, start_address, length, NULL);
}

/* Programs/verifies an array of words into a FLASH by using the
 * FLASH controller. The JTAG FLASH register isn't needed.
 * start_address: start in FLASH
 * length       : number of words
 * data         : pointer to data
 */
void jtag_write_flash(struct jtdev *p,
		      address_t start_address,
		      unsigned int length,
		      const uint16_t *data)
{
	unsigned int index;
	unsigned int address;

	jtag_led_red_on(p);

	address = start_address;
	jtag_halt_cpu(p);
	jtag_tclk_clr(p);

	/* Set RW to write */
	jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);
	jtag_dr_shift_16(p, 0x2408);

	/* FCTL1 register */
	jtag_ir_shift(p, IR_ADDR_16BIT);
	jtag_dr_shift_16(p, 0x0128);

	/* Enable FLASH write */
	jtag_ir_shift(p, IR_DATA_TO_ADDR);
	jtag_dr_shift_16(p, 0xA540);
	jtag_tclk_set(p);
	jtag_tclk_clr(p);

	/* FCTL2 register */
	jtag_ir_shift(p, IR_ADDR_16BIT);
	jtag_dr_shift_16(p, 0x012A);

	/* Select MCLK as source, DIV=1 */
	jtag_ir_shift(p, IR_DATA_TO_ADDR);
	jtag_dr_shift_16(p, 0xA540);
	jtag_tclk_set(p);
	jtag_tclk_clr(p);

	/* FCTL3 register */
	jtag_ir_shift(p, IR_ADDR_16BIT);
	jtag_dr_shift_16(p, 0x012C);

	/* Clear FCTL3 register */
	jtag_ir_shift(p, IR_DATA_TO_ADDR);
	jtag_dr_shift_16(p, 0xA500);
	jtag_tclk_set(p);
	jtag_tclk_clr(p);

	for (index = 0; index < length; index++) {
		/* Set RW to write */
		jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);
		jtag_dr_shift_16(p, 0x2408);

		/* Set address */
		jtag_ir_shift(p, IR_ADDR_16BIT);
		jtag_dr_shift_16(p, address);

		/* Set data */
		jtag_ir_shift(p, IR_DATA_TO_ADDR);
		jtag_dr_shift_16(p, data[index]);
		jtag_tclk_set(p);
		jtag_tclk_clr(p);

		/* Set RW to read */
		jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);
		jtag_dr_shift_16(p, 0x2409);

		/* provide TCLKs
		 * min. 33 for F149 and F449
		 */
		p->f->jtdev_tclk_strobe(p, 35);
		address += 2;

		if (p->failed)
			break;
	}

	/* Set RW to write */
	jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);
	jtag_dr_shift_16(p, 0x2408);

	/* FCTL1 register */
	jtag_ir_shift(p, IR_ADDR_16BIT);
	jtag_dr_shift_16(p, 0x0128);

	/* Disable FLASH write */
	jtag_ir_shift(p, IR_DATA_TO_ADDR);
	jtag_dr_shift_16(p, 0xA500);
	jtag_tclk_set(p);
	jtag_release_cpu(p);

	jtag_led_red_off(p);
}

/* Performs a mass erase (with and w/o info memory) or a segment erase of a
 * FLASH module specified by the given mode and address. Large memory devices
 * get additional mass erase operations to meet the spec.
 * erase_mode   : ERASE_MASS, ERASE_MAIN, ERASE_SGMT
 * erase_address: address within the selected segment
 */
void jtag_erase_flash(struct jtdev *p,
		      unsigned int erase_mode,
		      address_t erase_address)
{
	unsigned int number_of_strobes = 4820;	/* default for segment erase */
	unsigned int loop_counter;
	unsigned int max_loop_count = 1;	/* erase cycle repeating for mass erase */

	jtag_led_red_on(p);

	if ((erase_mode == JTAG_ERASE_MASS) ||
	    (erase_mode == JTAG_ERASE_MAIN)) {
		number_of_strobes = 5300;	/* Larger Flash memories require */
		max_loop_count = 19;		/* additional cycles for erase. */
		erase_address = 0xfffe;		/* overwrite given address */
	}

	for (loop_counter = max_loop_count; loop_counter > 0; loop_counter--) {
		jtag_halt_cpu(p);
		jtag_tclk_clr(p);

		/* Set RW to write */
		jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);
		jtag_dr_shift_16(p, 0x2408);

		/* FCTL1 address */
		jtag_ir_shift(p, IR_ADDR_16BIT);
		jtag_dr_shift_16(p, 0x0128);

		/* Enable erase mode */
		jtag_ir_shift(p, IR_DATA_TO_ADDR);
		jtag_dr_shift_16(p, erase_mode);
		jtag_tclk_set(p);
		jtag_tclk_clr(p);

		/* FCTL2 address */
		jtag_ir_shift(p, IR_ADDR_16BIT);
		jtag_dr_shift_16(p, 0x012A);

		/* MCLK is source, DIV=1 */
		jtag_ir_shift(p, IR_DATA_TO_ADDR);
		jtag_dr_shift_16(p, 0xA540);
		jtag_tclk_set(p);
		jtag_tclk_clr(p);

		/* FCTL3 address */
		jtag_ir_shift(p, IR_ADDR_16BIT);
		jtag_dr_shift_16(p, 0x012C);

		/* Clear FCTL3 */
		jtag_ir_shift(p, IR_DATA_TO_ADDR);
		jtag_dr_shift_16(p, 0xA500);
		jtag_tclk_set(p);
		jtag_tclk_clr(p);

		/* Set erase address */
		jtag_ir_shift(p, IR_ADDR_16BIT);
		jtag_dr_shift_16(p, erase_address);

		/* Dummy write to start erase */
		jtag_ir_shift(p, IR_DATA_TO_ADDR);
		jtag_dr_shift_16(p, 0x55AA);
		jtag_tclk_set(p);
		jtag_tclk_clr(p);

		/* Set RW to read */
		jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);
		jtag_dr_shift_16(p, 0x2409);

		/* provide TCLKs */
		p->f->jtdev_tclk_strobe(p, number_of_strobes);

		/* Set RW to write */
		jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);
		jtag_dr_shift_16(p, 0x2408);

		/* FCTL1 address */
		jtag_ir_shift(p, IR_ADDR_16BIT);
		jtag_dr_shift_16(p, 0x0128);

		/* Disable erase */
		jtag_ir_shift(p, IR_DATA_TO_ADDR);
		jtag_dr_shift_16(p, 0xA500);
		jtag_tclk_set(p);
		jtag_release_cpu(p);
	}

	jtag_led_red_off(p);
}

/* Reads a register from the target CPU */
address_t jtag_read_reg(struct jtdev *p, int reg)
{
	unsigned int value;

	/* CPU controls RW & BYTE */
	jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);
	jtag_dr_shift_16(p, 0x3401);

	/* Set CPU into instruction fetch mode */
	jtag_set_instruction_fetch(p);

	jtag_ir_shift(p, IR_DATA_16BIT);

	/* "jmp $-4" instruction */
	/* PC - 4 -> PC          */
	/* needs 2 clock cycles  */
	jtag_dr_shift_16(p, 0x3ffd);
	jtag_tclk_set(p);
	jtag_tclk_clr(p);
	jtag_tclk_set(p);
	jtag_tclk_clr(p);

	/* "mov Rn,&0x01fe" instruction
	 * Rn -> &0x01fe
	 * PC is advanced 4 bytes by this instruction
	 * needs 4 clock cycles
	 * it's a ROM address, write has no effect, but
	 * the registers value is placed on the databus
	 */
	jtag_dr_shift_16(p, 0x4082 | (((unsigned int)reg << 8) & 0x0f00) );
	jtag_tclk_set(p);
	jtag_tclk_clr(p);
	jtag_dr_shift_16(p, 0x01fe);
	jtag_tclk_set(p);
	jtag_tclk_clr(p);
	jtag_tclk_set(p);
	jtag_tclk_clr(p);
	jtag_tclk_set(p);
	jtag_tclk_clr(p);

	/* Read databus which contains the registers value */
	jtag_ir_shift(p, IR_DATA_CAPTURE);
	value = jtag_dr_shift_16(p, 0x0000);

	/* JTAG controls RW & BYTE */
	jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);
	jtag_dr_shift_16(p, 0x2401);

	jtag_tclk_set(p);

	/* Return value read from register */
	return value;
}

/* Writes a value into a register of the target CPU */
void jtag_write_reg(struct jtdev *p, int reg, address_t value)
{
	/* CPU controls RW & BYTE */
	jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);
	jtag_dr_shift_16(p, 0x3401);

	/* Set CPU into instruction fetch mode */
	jtag_set_instruction_fetch(p);

	jtag_ir_shift(p, IR_DATA_16BIT);

	/* "jmp $-4" instruction */
	/* PC - 4 -> PC          */
	/* needs 4 clock cycles  */
	jtag_dr_shift_16(p, 0x3ffd);
	jtag_tclk_set(p);
	jtag_tclk_clr(p);
	jtag_tclk_set(p);
	jtag_tclk_clr(p);

	/* "mov #value,Rn" instruction
	 * value -> Rn
	 * PC is advanced 4 bytes by this instruction
	 * needs 2 clock cycles
	 */
	jtag_dr_shift_16(p, 0x4030 | (reg & 0x000f) );
	jtag_tclk_set(p);
	jtag_tclk_clr(p);
	jtag_dr_shift_16(p, value);
	jtag_tclk_set(p);
	jtag_tclk_clr(p);

	/* JTAG controls RW & BYTE */
	jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);
	jtag_dr_shift_16(p, 0x2401);

	jtag_tclk_set(p);
}

/*----------------------------------------------------------------------------*/
void jtag_single_step( struct jtdev *p )
{
	unsigned int loop_counter;

	/* CPU controls RW & BYTE */
	jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);
	jtag_dr_shift_16(p, 0x3401);

	/* clock CPU until next instruction fetch cycle  */
	/* failure after 10 clock cycles                 */
	/* this is more than for the longest instruction */
	jtag_ir_shift(p, IR_CNTRL_SIG_CAPTURE);
	for (loop_counter = 10; loop_counter > 0; loop_counter--) {
		jtag_tclk_clr(p);
		jtag_tclk_set(p);
		if ((jtag_dr_shift_16(p, 0x0000) & 0x0080) == 0x0080) {
			break;
		}
	}

	/* JTAG controls RW & BYTE */
	jtag_ir_shift(p, IR_CNTRL_SIG_16BIT);
	jtag_dr_shift_16(p, 0x2401);

	if (loop_counter == 0) {
		/* timeout reached */
		printc_err("pif: single step failed\n");
		p->failed = 1;
	}
}

/*----------------------------------------------------------------------------*/
unsigned int jtag_set_breakpoint( struct jtdev *p,int bp_num, address_t bp_addr )
{
	/* The breakpoint logic is explained in 'SLAU414c EEM.pdf' */
	/* A good overview is given with Figure 1-1                */
	/* MBx           is TBx         in EEM_defs.h              */
	/* CPU Stop      is BREAKREACT  in EEM_defs.h              */
	/* State Storage is STOR_REACT  in EEM_defs.h              */
	/* Cycle Counter is EVENT_REACT in EEM_defs.h              */

	unsigned int breakreact;

	if (bp_num >= 8) {
		/* there are no more than 8 breakpoints in EEM */
		printc_err("jtag_set_breakpoint: failed setting "
			   "breakpoint %d at %04x\n", bp_num, bp_addr);
		p->failed = 1;
		return 0;
	}

	if (bp_num < 0) {
		/* disable all breakpoints by deleting the BREAKREACT
		 * register */
		jtag_ir_shift(p, IR_EMEX_DATA_EXCHANGE);
		jtag_dr_shift_16(p, BREAKREACT + WRITE);
		jtag_dr_shift_16(p, 0x0000);
		return 1;
	}

	/* set breakpoint */
	jtag_ir_shift(p, IR_EMEX_DATA_EXCHANGE);
	jtag_dr_shift_16(p, GENCTRL + WRITE);
	jtag_dr_shift_16(p, EEM_EN + CLEAR_STOP + EMU_CLK_EN + EMU_FEAT_EN);

	jtag_ir_shift(p, IR_EMEX_DATA_EXCHANGE); //repeating may not needed
	jtag_dr_shift_16(p, 8*bp_num + MBTRIGxVAL + WRITE);
	jtag_dr_shift_16(p, bp_addr);

	jtag_ir_shift(p, IR_EMEX_DATA_EXCHANGE); //repeating may not needed
	jtag_dr_shift_16(p, 8*bp_num + MBTRIGxCTL + WRITE);
	jtag_dr_shift_16(p, MAB + TRIG_0 + CMP_EQUAL);

	jtag_ir_shift(p, IR_EMEX_DATA_EXCHANGE); //repeating may not needed
	jtag_dr_shift_16(p, 8*bp_num + MBTRIGxMSK + WRITE);
	jtag_dr_shift_16(p, NO_MASK);

	jtag_ir_shift(p, IR_EMEX_DATA_EXCHANGE); //repeating may not needed
	jtag_dr_shift_16(p, 8*bp_num + MBTRIGxCMB + WRITE);
	jtag_dr_shift_16(p, 1<<bp_num);

	/* read the actual setting of the BREAKREACT register         */
	/* while reading a 1 is automatically shifted into LSB        */
	/* this will be undone and the bit for the new breakpoint set */
	/* then the updated value is stored back                      */
	jtag_ir_shift(p, IR_EMEX_DATA_EXCHANGE); //repeating may not needed
	breakreact  = jtag_dr_shift_16(p, BREAKREACT + READ);
	breakreact += jtag_dr_shift_16(p, 0x000);
	breakreact  = (breakreact >> 1) | (1 << bp_num);
	jtag_dr_shift_16(p, BREAKREACT + WRITE);
	jtag_dr_shift_16(p, breakreact);
	return 1;
}

/*----------------------------------------------------------------------------*/
unsigned int jtag_cpu_state( struct jtdev *p )
{
	jtag_ir_shift(p, IR_EMEX_READ_CONTROL);

	if ((jtag_dr_shift_16(p, 0x0000) & 0x0080) == 0x0080) {
		return 1; /* halted */
	} else {
		return 0; /* running */
	}
}

/*----------------------------------------------------------------------------*/
int jtag_get_config_fuses( struct jtdev *p )
{
    jtag_ir_shift(p, IR_CONFIG_FUSES);

    return jtag_dr_shift_8(p, 0);
}
