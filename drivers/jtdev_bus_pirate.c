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
#include "jtdev.h"
#include "output.h"

#if defined(__linux__)
/*===== includes =============================================================*/

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>
#include <time.h>

#include "util.h"

/*===== private symbols ======================================================*/

/*--- bus pirate pins ---*/
#define BP_CS       ((unsigned char)0x01)
#define BP_MISO     ((unsigned char)0x02)
#define BP_CLK      ((unsigned char)0x04)
#define BP_MOSI     ((unsigned char)0x08)
#define BP_AUX      ((unsigned char)0x10)
#define BP_PULLUP   ((unsigned char)0x20)
#define BP_POWER    ((unsigned char)0x40)

/*--- bus pirate binary commands ---*/
#define CMD_ENTER_BB            ((unsigned char)0x00)
#define CMD_LEAVE_BB            ((unsigned char)0x0f)
#define CMD_CONFIG_PIN_DIR(x)   ((unsigned char)0x40 | (x & 0x1f))
#define CMD_WRITE_PINS(x)       ((unsigned char)0x80 | (x & 0x7f))

/*--- JTAG signal mapping ---*/
#define TDO         BP_MISO
#define TDI         BP_MOSI
#define TMS         BP_CS
#define POWER       BP_POWER
#define RESET       BP_AUX
#define TCK	        BP_CLK

/*===== public functions =====================================================*/

#include <sys/ioctl.h>

static void do_bus_pirate_data(struct jtdev *p)
{
    char res;
    char out_buff, in_buff;
    int buffered;

    out_buff = CMD_WRITE_PINS(p->data_register);

    ioctl(p->port, TIOCINQ, &buffered);
    if(buffered != 0) {
        pr_error("jtdev: extraneous bytes available on serial port, flushing it");
        tcflush(p->port, TCIFLUSH);
    }

    if(write(p->port, &out_buff, 1) < 1) {
		pr_error("jtdev: failed writing to serial port");
		p->failed = 1;
    }

    res = read(p->port, &in_buff, 1);
    p->data_register &= ~TDO;
    p->data_register |= in_buff & TDO;

    // TODO: Handle failure, try again if we don't receive
	if (res < 1) {
		pr_error("jtdev: no response with in data");
		p->failed = 1;
	}
}

static int jtbp_open(struct jtdev *p, const char *device)
{
    int i;
    char in_buff, out_buff;
    struct termios tio;
    const char* response = "BBIO1";

	p->port = open(device, O_RDWR | O_NOCTTY);
	if (p->port < 0) {
		printc_err("jtdev: can't open %s: %s\n",
			   device, last_error());
		return -1;
	}

	memset(&tio, 0, sizeof(tio));

	tio.c_cflag = B115200 | CS8 | CLOCAL | CREAD;
	tio.c_iflag = IGNPAR;
	tio.c_oflag = 0;
	tio.c_lflag = 0;

	tio.c_cc[VTIME] = 1;
	tio.c_cc[VMIN] = 0;

	tcflush(p->port, TCIFLUSH);
	tcsetattr(p->port, TCSANOW, &tio);

    // If it's in the middle of spewing something, let it finish
	while(read(p->port, &in_buff, 1) != 0);

	out_buff = 0;

	for(i=0; i<20; ++i) {
        if(write(p->port, &out_buff, 1) < 1) {
            pr_error("jtdev: failed writing to serial port");
            p->failed = 1;
        }

	    if(read(p->port, &in_buff, 1) > 0) {
	        break;
        }
    }

	if (i == 20) {
		printc_err("jtdev: bus pirate failed to enter bit bang mode\n");
		return -1;
	}

	if(in_buff != *response) {
		printc_err("jtdev: bus pirate: got bad response %c\n", in_buff);
		return -1;
    }

    ++response;

    for(i=0; i<4; ++i, ++response) {
	    if(read(p->port, &in_buff, 1) <= 0) {
            printc_err("jtdev: bus pirate: got no response\n");
        }

        if(in_buff != *response) {
            printc_err("jtdev: bus pirate: got bad response %c\n", in_buff);
            return -1;
        }
    }

    out_buff = CMD_CONFIG_PIN_DIR(TDO);
    if(write(p->port, &out_buff, 1) < 1) {
		pr_error("jtdev: failed writing to serial port");
		p->failed = 1;
    }

    if(read(p->port, &in_buff, 1) <= 0) {
        printc_err("jtdev: bus pirate: got no response\n");
    }

	p->data_register = 0;
	p->control_register = 0;
	p->failed = 0;

	do_bus_pirate_data(p);

	return 0;
}

static void jtbp_close(struct jtdev *p)
{
    char out_buff;

    out_buff = 0x0f;
    // Don't care if this fails, user can just power cycle the bus pirate
    if(write(p->port, &out_buff, 1));

	close(p->port);
}

static void jtbp_power_on(struct jtdev *p)
{
	/* power supply on */
	p->data_register |= POWER;
	do_bus_pirate_data(p);
	sleep(1);
}

static void jtbp_power_off(struct jtdev *p)
{
	/* power supply off */
	p->data_register &= ~(POWER | RESET);
	do_bus_pirate_data(p);
}

static void jtbp_connect(struct jtdev *p)
{
    // unsure what this function does, I presume my setup w/ bus pirate is
    // "always enabled"
}

static void jtbp_release(struct jtdev *p)
{
    // unsure what this function does, I presume my setup w/ bus pirate is
    // "always enabled"
}

static void jtbp_tck(struct jtdev *p, int out)
{
	if (out)
		p->data_register |= TCK;
	else
		p->data_register &= ~TCK;

	do_bus_pirate_data(p);
}

static void jtbp_tms(struct jtdev *p, int out)
{
	if (out)
		p->data_register |= TMS;
	else
		p->data_register &= ~TMS;

	do_bus_pirate_data(p);
}

static void jtbp_tdi(struct jtdev *p, int out)
{
	if (out)
		p->data_register |= TDI;
	else
		p->data_register &= ~TDI;

	do_bus_pirate_data(p);
}

static void jtbp_rst(struct jtdev *p, int out)
{
	if (out)
		p->data_register |= RESET;
	else
		p->data_register &= ~RESET;

	do_bus_pirate_data(p);
}

static void jtbp_tst(struct jtdev *p, int out)
{
    // Test not supported on bus pirate
}

static int jtbp_tdo_get(struct jtdev *p)
{
	do_bus_pirate_data(p);

	return (p->data_register & TDO) ? 1 : 0;
}

static void jtbp_tclk(struct jtdev *p, int out)
{
    jtbp_tdi(p, out);
}

static int jtbp_tclk_get(struct jtdev *p)
{
	do_bus_pirate_data(p);

	return (p->data_register & TDI) ? 1 : 0;
}

static void jtbp_tclk_strobe(struct jtdev *p, unsigned int count)
{
	int i;

	for (i = 0; i < count; i++) {
		jtbp_tclk(p, 1);
		jtbp_tclk(p, 0);

		if (p->failed)
			return;
	}
}

static void jtbp_led_green(struct jtdev *p, int out)
{
    // TCLK not supported by bus pirate
}

static void jtbp_led_red(struct jtdev *p, int out)
{
    // TCLK not supported by bus pirate
}
#else /* __linux__ */
static int jtbp_open(struct jtdev *p, const char *device)
{
	printc_err("jtdev: driver is not supported on this platform\n");
	p->failed = 1;
	return -1;
}

static void jtbp_close(struct jtdev *p) { }

static void jtbp_power_on(struct jtdev *p) { }
static void jtbp_power_off(struct jtdev *p) { }
static void jtbp_connect(struct jtdev *p) { }
static void jtbp_release(struct jtdev *p) { }

static void jtbp_tck(struct jtdev *p, int out) { }
static void jtbp_tms(struct jtdev *p, int out) { }
static void jtbp_tdi(struct jtdev *p, int out) { }
static void jtbp_rst(struct jtdev *p, int out) { }
static void jtbp_tst(struct jtdev *p, int out) { }
static int jtbp_tdo_get(struct jtdev *p) { return 0; }

static void jtbp_tclk(struct jtdev *p, int out) { }
static int jtbp_tclk_get(struct jtdev *p) { return 0; }
static void jtbp_tclk_strobe(struct jtdev *p, unsigned int count) { }

static void jtbp_led_green(struct jtdev *p, int out) { }
static void jtbp_led_red(struct jtdev *p, int out) { }
#endif

const struct jtdev_func jtdev_func_bp = {
  .jtdev_open        = jtbp_open,
  .jtdev_close       = jtbp_close,
  .jtdev_power_on    = jtbp_power_on,
  .jtdev_power_off   = jtbp_power_off,
  .jtdev_connect     = jtbp_connect,
  .jtdev_release     = jtbp_release,
  .jtdev_tck	     = jtbp_tck,
  .jtdev_tms	     = jtbp_tms,
  .jtdev_tdi	     = jtbp_tdi,
  .jtdev_rst	     = jtbp_rst,
  .jtdev_tst	     = jtbp_tst,
  .jtdev_tdo_get     = jtbp_tdo_get,
  .jtdev_tclk	     = jtbp_tclk,
  .jtdev_tclk_get    = jtbp_tclk_get,
  .jtdev_tclk_strobe = jtbp_tclk_strobe,
  .jtdev_led_green   = jtbp_led_green,
  .jtdev_led_red     = jtbp_led_red
};

