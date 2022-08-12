/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2012 Daniel Beer
 * Copyright (C) 2012 Peter Bägel
 *
 * ppdev/ppi abstraction inspired by uisp src/DARPA.C
 *   originally written by Sergey Larin;
 *   corrected by Denis Chertykov, Uros Platise and Marek Michalkiewicz.
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

#include <stdint.h>
#include <ftdi.h>
#include "jtaglib.h"
#include "output.h"
#include "util.h"

/*===== private symbols ======================================================*/

/*--- FTDI port pins ---*/
#define FTDI_TXD ((unsigned char)0x01) // TCK/SK
#define FTDI_RXD ((unsigned char)0x02) // TDI/DO
#define FTDI_RTS ((unsigned char)0x04) // TDO/DI
#define FTDI_CTS ((unsigned char)0x08) // TMS/CS
#define FTDI_DTR ((unsigned char)0x10) // GPIOL0
#define FTDI_DSR ((unsigned char)0x20) // GPIOL1
#define FTDI_DCD ((unsigned char)0x40) // GPIOL2
#define FTDI_RI  ((unsigned char)0x80) // GPIOL3

/*--- JTAG signal mapping ---*/
// while technically re-assignable, for now they're just hardcoded
#define TDO   FTDI_RTS
#define TDI   FTDI_RXD
#define TMS   FTDI_CTS
#define TCK   FTDI_TXD
#define RESET FTDI_DCD // OpenOCD uses this as the default SYSRST pin

#define OUT_BITS (TDI | TMS | TCK | RESET)

#define DEFAULT_VID 0x0403
static const uint16_t default_pids[] = {
	0x6001, // FT232RL
	0x6010, // FT2232HL
	0x6011, // FT4232HL
	0x6014, // FT232HL
};

// ft232r has 128 byte Tx buffer, but openocd suggests that anything larger than
// 64 bytes causes hangups
static uint8_t fast_buf[64];
static size_t fast_buf_i;

/*===== public functions =====================================================*/

static void ftdi_print_err(const char *msg, int code, struct jtdev *p)
{
	printc_err("jtdev: %s: %d (%s)\n", msg, code, ftdi_get_error_string(p->handle));
}

static void do_bitbang_write(struct jtdev *p)
{
	int f;

	if((f = ftdi_write_data(p->handle, &p->data_register, 1)) < 0) {
		ftdi_print_err("failed writing to FTDI port", f, p);
		p->failed = 1;
		return;
	}
}

static void do_bitbang_bitset(struct jtdev *p, uint8_t mask, int out)
{
	if (out)
		p->data_register |= mask;
	else
		p->data_register &= ~mask;

	do_bitbang_write(p);
}

static int do_bitbang_read(struct jtdev *p, uint8_t bit)
{
	int f;
	uint8_t buf;

	if((f = ftdi_read_pins(p->handle, &buf)) < 0) {
		ftdi_print_err("failed reading from FTDI port", f, p);
		p->failed = 1;
		return 0;
	}

	return (buf & bit) ? 1 : 0;
}

static int jtbitbang_open_ex(struct jtdev *p, const char *device, const uint16_t *vid, const uint16_t *pid)
{
	int f = -1;
	size_t i;
	uint16_t _vid, _pid;

	if ((p->handle = ftdi_new()) == NULL) {
		printc_err("jtdev: ftdi_new failed\n");
		return -1;
	}

	if(vid == NULL && pid == NULL) {
		// iterate through all the default VID/PID pairs for auto-detect
		for(i = 0; i < ARRAY_LEN(default_pids) && f < 0; i++) {
			f = ftdi_usb_open(p->handle, DEFAULT_VID, default_pids[i]);
		}
	} else {
		// pick sane defaults if either provided
		_vid = vid ? *vid : DEFAULT_VID;
		_pid = pid ? *pid : 0x6010;
		f = ftdi_usb_open(p->handle, _vid, _pid);
	}

	if(f < 0) {
		ftdi_print_err("unable to open ftdi device", f, p);
		p->f->jtdev_close(p);
		return -1;
	}

	if((f = ftdi_set_bitmode(p->handle, OUT_BITS, BITMODE_BITBANG)) < 0) {
		ftdi_print_err("unable to enable ftdi bitbang mode", f, p);
		p->f->jtdev_close(p);
		return -1;
	}

	p->f->jtdev_set_fast_baud(p, false);

	p->data_register = 0;
	p->control_register = 0;
	p->failed = 0;

	do_bitbang_write(p);

	return 0;
}

static void jtbitbang_close(struct jtdev *p)
{
	if(p->handle != NULL) {
		ftdi_set_bitmode(p->handle, OUT_BITS, BITMODE_RESET);
		ftdi_usb_close(p->handle);
		ftdi_free(p->handle);

		p->handle = NULL;
	}
}

static int jtbitbang_set_fast_baud(struct jtdev *p, bool fast)
{
	int f;
	// baud is multiplied by 4 for some reason in bitbang mode?
	//int baud = fast ? 350000/4 : 9600/4;
	int baud = fast ? 350000 : 9600;

	if((f = ftdi_set_baudrate(p->handle, baud)) < 0) {
		ftdi_print_err("unable to set ftdi baud", f, p);
		return -1;
	}
	printc_dbg("jtdev: set ftdi baud to %d\n", baud);

	return 0;
}

static void jtbitbang_power_on(struct jtdev *p)
{
	// Some FTDI ports have PWREN pins, but not implemented here
}

static void jtbitbang_power_off(struct jtdev *p)
{
	// Some FTDI ports have PWREN pins, but not implemented here
}

static void jtbitbang_connect(struct jtdev *p)
{
    // unsure what this function does, I presume my setup w/ FTDI is
    // "always enabled"
}

static void jtbitbang_release(struct jtdev *p)
{
    // unsure what this function does, I presume my setup w/ FTDI is
    // "always enabled"
}

static void jtbitbang_tck(struct jtdev *p, int out)
{
	do_bitbang_bitset(p, TCK, out);
}

static void jtbitbang_tms(struct jtdev *p, int out)
{
	do_bitbang_bitset(p, TMS, out);
}

static void jtbitbang_tdi(struct jtdev *p, int out)
{
	do_bitbang_bitset(p, TDI, out);
}

static void jtbitbang_rst(struct jtdev *p, int out)
{
	do_bitbang_bitset(p, RESET, out);
}

static void jtbitbang_tst(struct jtdev *p, int out)
{
    // Test not supported in bitbang
}

static int jtbitbang_tdo_get(struct jtdev *p)
{
	return do_bitbang_read(p, TDO);
}

static void jtbitbang_tclk(struct jtdev *p, int out)
{
    jtbitbang_tdi(p, out);
}

static int jtbitbang_tclk_get(struct jtdev *p)
{
	return do_bitbang_read(p, TDI);
}

static void jtbitbang_led_green(struct jtdev *p, int out)
{
    // LED responds to data, not directly controllable?
}

static void jtbitbang_led_red(struct jtdev *p, int out)
{
    // LED responds to data, not directly controllable?
}

static void fast_flush(struct jtdev *p)
{
	if(fast_buf_i == 0) {
		return;
	}

	int f = ftdi_write_data(p->handle, fast_buf, fast_buf_i);
	fast_buf_i = 0;
	if(f < 0) {
		ftdi_print_err("failed writing to FTDI port", f, p);
		p->failed = 1;
		return;
	}
}

static void fast_push(struct jtdev *p, uint8_t data_reg)
{
	if(fast_buf_i >= sizeof(fast_buf)) {
		fast_flush(p);
	}

	fast_buf[fast_buf_i++] = data_reg;
	p->data_register = data_reg;
}

static void fast_clock(struct jtdev *p)
{
	fast_push(p, p->data_register & ~TCK);
	fast_push(p, p->data_register | TCK);
}

static void fast_clock_and_dat(struct jtdev *p, uint8_t data_reg)
{
	// data always latched on rising edge, loaded on falling edge
	fast_push(p, data_reg & ~TCK);
	/// p->data_register was updated just before
	fast_push(p, p->data_register | TCK);
}

static void jtbitbang_tclk_strobe(struct jtdev *p, unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		fast_push(p, p->data_register | TDI);
		fast_push(p, p->data_register & ~TDI);
	}

	fast_flush(p);
}

static void fast_tclk_prep (struct jtdev *p)
{
	/* JTAG state = Exit-DR */
	fast_clock(p);

	/* JTAG state = Update-DR */
	fast_clock_and_dat(p, p->data_register & ~TMS);

	/* JTAG state = Run-Test/Idle */
}

static unsigned int fast_shift( struct jtdev *p,
				unsigned char num_bits,
				unsigned int  data_out )
{
	unsigned int data_in;
	unsigned int mask;
	unsigned int tclk_save;

	tclk_save = p->data_register & TDI;

	data_in = 0;
	for (mask = 0x0001U << (num_bits - 1); mask != 0; mask >>= 1) {
		uint8_t out;
		if ((data_out & mask) != 0)
			out = p->data_register | TDI;
		else
			out = p->data_register & ~TDI;

		if (mask == 1)
			out |= TMS;

		fast_clock_and_dat(p, out);

		// need to flush so jtdev_tdo_get has the right value. Tried to optimise
		// using ftdi_read but speed actually decreased and logic was complex
		fast_flush(p);

		if (p->f->jtdev_tdo_get(p) == 1)
			data_in |= mask;
	}

	p->data_register &= ~TDI;
	fast_push(p, p->data_register | tclk_save);

	/* Set JTAG state back to Run-Test/Idle */
	fast_tclk_prep(p);

	// Potentially returning to non-fast function, flush bytes
	fast_flush(p);

	return data_in;
}

static uint8_t fast_ir_shift(struct jtdev *p, uint8_t ir)
{
	/* JTAG state = Run-Test/Idle */
	fast_clock_and_dat(p, p->data_register | TMS);

	/* JTAG state = Select DR-Scan */
	fast_clock(p);

	/* JTAG state = Select IR-Scan */
	fast_clock_and_dat(p, p->data_register & ~TMS);

	/* JTAG state = Capture-IR */
	fast_clock(p);

	/* JTAG state = Shift-IR, Shift in TDI (8-bit) */
	return fast_shift(p, 8, ir);

	/* JTAG state = Run-Test/Idle */
}

static uint16_t fast_dr_shift_16(struct jtdev *p, uint16_t data)
{
	/* JTAG state = Run-Test/Idle */
	fast_clock_and_dat(p, p->data_register | TMS);

	/* JTAG state = Select DR-Scan */
	fast_clock_and_dat(p, p->data_register & ~TMS);

	/* JTAG state = Capture-DR */
	fast_clock(p);

	/* JTAG state = Shift-DR, Shift in TDI (16-bit) */
	return fast_shift(p, 16, data);

	/* JTAG state = Run-Test/Idle */
}


const struct jtdev_func jtdev_func_ftdi_bitbang = {
  // note: using open_ex as we need PID/VID populated
  .jtdev_open_ex       = jtbitbang_open_ex,
  .jtdev_close         = jtbitbang_close,
  .jtdev_power_on      = jtbitbang_power_on,
  .jtdev_power_off     = jtbitbang_power_off,
  .jtdev_connect       = jtbitbang_connect,
  .jtdev_release       = jtbitbang_release,
  .jtdev_tck	       = jtbitbang_tck,
  .jtdev_tms	       = jtbitbang_tms,
  .jtdev_tdi	       = jtbitbang_tdi,
  .jtdev_rst	       = jtbitbang_rst,
  .jtdev_tst	       = jtbitbang_tst,
  .jtdev_tdo_get       = jtbitbang_tdo_get,
  .jtdev_tclk	       = jtbitbang_tclk,
  .jtdev_tclk_get      = jtbitbang_tclk_get,
  .jtdev_tclk_strobe   = jtbitbang_tclk_strobe,
  .jtdev_led_green     = jtbitbang_led_green,
  .jtdev_led_red       = jtbitbang_led_red,

  .jtdev_set_fast_baud = jtbitbang_set_fast_baud,
  // optimised sending for hot functions used for flash programming + reading
  .jtdev_ir_shift      = fast_ir_shift,
  .jtdev_dr_shift_16   = fast_dr_shift_16,
  // these aren't called too often and can be default
  .jtdev_dr_shift_8    = jtag_default_dr_shift_8,
  .jtdev_tms_sequence  = jtag_default_tms_sequence,
  .jtdev_init_dap      = jtag_default_init_dap,
};

