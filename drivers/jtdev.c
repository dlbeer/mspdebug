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

#if defined(__linux__) || \
    ( defined(__FreeBSD__) || defined(__DragonFly__) )
/*===== includes =============================================================*/

#include <fcntl.h>
#include <unistd.h>

#if defined(__linux__)

#include <linux/ppdev.h>

#define par_claim(fd)			ioctl(fd, PPCLAIM, NULL)
#define par_write_data(fd, ptr)		ioctl(fd, PPWDATA, ptr)
#define par_write_control(fd, ptr)	ioctl(fd, PPWCONTROL, ptr)
#define par_release(fd)			ioctl(fd, PPRELEASE, NULL)
#define par_read_status(fd, ptr)	ioctl(fd, PPRSTATUS, ptr)

#elif defined(__FreeBSD__) || defined(__DragonFly__)

#if defined(__FreeBSD__)
#include <dev/ppbus/ppi.h>
#include <dev/ppbus/ppbconf.h>
#else /* __DragonFly__ */
#include <dev/misc/ppi/ppi.h>
#include <bus/ppbus/ppbconf.h>
#endif

#define par_claim(fd)			(0)
#define par_write_data(fd, ptr)		ioctl(fd, PPISDATA, ptr)
#define par_write_control(fd, ptr)	ioctl(fd, PPISCTRL, ptr)
#define par_release(fd)			(0)
#define par_read_status(fd, ptr)	ioctl(fd, PPIGSTATUS, ptr)

#endif /* __linux__ || (__FreeBSD__ || __DragonFly__) */

#include <sys/ioctl.h>

#include "util.h"

/*===== private symbols ======================================================*/

/*--- port data (out) ---*/
#define DATA0 ((unsigned char)0x01)
#define DATA1 ((unsigned char)0x02)
#define DATA2 ((unsigned char)0x04)
#define DATA3 ((unsigned char)0x08)
#define DATA4 ((unsigned char)0x10)
#define DATA5 ((unsigned char)0x20)
#define DATA6 ((unsigned char)0x40)
#define DATA7 ((unsigned char)0x80)

/*--- port status (in) ---*/
#define ERR  ((unsigned char)0x08)
#define SEL  ((unsigned char)0x10)
#define PE   ((unsigned char)0x20)
#define ACK  ((unsigned char)0x40)
#define BUSY ((unsigned char)0x80)

/*--- port control (out) ---*/
#ifndef STROBE
#define STROBE   ((unsigned char)0x01) /* inverted by PC-hardware */
#endif
#ifndef AUTOFEED
#define AUTOFEED ((unsigned char)0x02)
#endif
#ifndef INIT
#define INIT     ((unsigned char)0x04)
#endif
#ifndef SELECTIN
#define SELECTIN ((unsigned char)0x08)
#endif
#define IRQEN    ((unsigned char)0x10)

/*--- JTAG signal mapping ---*/
#define TEST      INIT
#define TDO       PE
#define TDI       DATA0
#define TMS       DATA1
#define TCK       DATA2
#define XOUT      DATA3
#define POWER    (DATA4 | DATA7)
#define RESET     STROBE
#define ENABLE   (SELECTIN | AUTOFEED)
#define LED_GREEN DATA5
#define LED_RED   DATA6

#define TCLK	  TDI

/*===== public functions =====================================================*/

static void do_ppwdata(struct jtdev *p)
{
	if (par_write_data(p->port, &p->data_register) < 0) {
		pr_error("jtdev: par_write_data");
		p->failed = 1;
	}
}

static void do_ppwcontrol(struct jtdev *p)
{
	if (par_write_control(p->port, &p->control_register) < 0) {
		pr_error("jtdev: par_write_control");
		p->failed = 1;
	}
}

static int jtpif_open(struct jtdev *p, const char *device)
{
	p->port = open(device, O_RDWR);
	if (p->port < 0) {
		printc_err("jtdev: can't open %s: %s\n",
			   device, last_error());
		return -1;
	}

	if (par_claim(p->port) < 0) {
		printc_err("jtdev: par_claim: %s: %s\n",
			   device, last_error());
		close(p->port);
		return -1;
	}

	p->data_register = 0;
	p->control_register = 0;
	p->failed = 0;

	do_ppwdata(p);
	do_ppwcontrol(p);

	return 0;
}

static void jtpif_close(struct jtdev *p)
{
	if (par_release(p->port) < 0)
		pr_error("warning: jtdev: failed to release port");

	close(p->port);
}

static void jtpif_power_on(struct jtdev *p)
{
	/* power supply on */
	p->data_register |= POWER;
	do_ppwdata(p);
}

static void jtpif_power_off(struct jtdev *p)
{
	/* power supply off */
	p->data_register &= ~POWER;
	do_ppwdata(p);

	/* a high, inactive reset also powers the target */
	/* reset pin is inverted by PC hardware          */
	p->control_register |= RESET;
	do_ppwcontrol(p);
}

static void jtpif_connect(struct jtdev *p)
{
	p->control_register |= (TEST | ENABLE);
	do_ppwcontrol(p);
}

static void jtpif_release(struct jtdev *p)
{
	p->control_register &= ~(TEST | ENABLE);
	do_ppwcontrol(p);
}

static void jtpif_tck(struct jtdev *p, int out)
{
	if (out)
		p->data_register |= TCK;
	else
		p->data_register &= ~TCK;

	do_ppwdata(p);
}

static void jtpif_tms(struct jtdev *p, int out)
{
	if (out)
		p->data_register |= TMS;
	else
		p->data_register &= ~TMS;

	do_ppwdata(p);
}

static void jtpif_tdi(struct jtdev *p, int out)
{
	if (out)
		p->data_register |= TDI;
	else
		p->data_register &= ~TDI;

	do_ppwdata(p);
}

static void jtpif_rst(struct jtdev *p, int out)
{
	/* reset pin is inverted by PC hardware */
	if (out)
		p->control_register &= ~RESET;
	else
		p->control_register |= RESET;

	do_ppwcontrol(p);
}

static void jtpif_tst(struct jtdev *p, int out)
{
	if (out)
		p->control_register |= TEST;
	else
		p->control_register &= ~TEST;

	do_ppwcontrol(p);
}

static int jtpif_tdo_get(struct jtdev *p)
{
	uint8_t input;

	if (par_read_status(p->port, &input) < 0) {
		pr_error("jtdev: par_read_status:");
		p->failed = 1;
		return 0;
	}

	return (input & TDO) ? 1 : 0;
}

static void jtpif_tclk(struct jtdev *p, int out)
{
	if (out)
		p->data_register |= TCLK;
	else
		p->data_register &= ~TCLK;

	do_ppwdata(p);
}

static int jtpif_tclk_get(struct jtdev *p)
{
	return (p->data_register & TCLK) ? 1 : 0;
}

static void jtpif_tclk_strobe(struct jtdev *p, unsigned int count)
{
	int i;

	for (i = 0; i < count; i++) {
		jtpif_tclk(p, 1);
		jtpif_tclk(p, 0);

		if (p->failed)
			return;
	}
}

static void jtpif_led_green(struct jtdev *p, int out)
{
	if (out)
		p->data_register |= LED_GREEN;
	else
		p->data_register &= ~LED_GREEN;

	do_ppwdata(p);
}

static void jtpif_led_red(struct jtdev *p, int out)
{
	if (out)
		p->data_register |= LED_RED;
	else
		p->data_register &= ~LED_RED;

	do_ppwdata(p);
}
#else /* __linux__ */
static int jtpif_open(struct jtdev *p, const char *device)
{
	printc_err("jtdev: driver is not supported on this platform\n");
	p->failed = 1;
	return -1;
}

static void jtpif_close(struct jtdev *p) { }

static void jtpif_power_on(struct jtdev *p) { }
static void jtpif_power_off(struct jtdev *p) { }
static void jtpif_connect(struct jtdev *p) { }
static void jtpif_release(struct jtdev *p) { }

static void jtpif_tck(struct jtdev *p, int out) { }
static void jtpif_tms(struct jtdev *p, int out) { }
static void jtpif_tdi(struct jtdev *p, int out) { }
static void jtpif_rst(struct jtdev *p, int out) { }
static void jtpif_tst(struct jtdev *p, int out) { }
static int jtpif_tdo_get(struct jtdev *p) { return 0; }

static void jtpif_tclk(struct jtdev *p, int out) { }
static int jtpif_tclk_get(struct jtdev *p) { return 0; }
static void jtpif_tclk_strobe(struct jtdev *p, unsigned int count) { }

static void jtpif_led_green(struct jtdev *p, int out) { }
static void jtpif_led_red(struct jtdev *p, int out) { }
#endif

const struct jtdev_func jtdev_func_pif = {
  .jtdev_open        = jtpif_open,
  .jtdev_close       = jtpif_close,
  .jtdev_power_on    = jtpif_power_on,
  .jtdev_power_off   = jtpif_power_off,
  .jtdev_connect     = jtpif_connect,
  .jtdev_release     = jtpif_release,
  .jtdev_tck	     = jtpif_tck,
  .jtdev_tms	     = jtpif_tms,
  .jtdev_tdi	     = jtpif_tdi,
  .jtdev_rst	     = jtpif_rst,
  .jtdev_tst	     = jtpif_tst,
  .jtdev_tdo_get     = jtpif_tdo_get,
  .jtdev_tclk	     = jtpif_tclk,
  .jtdev_tclk_get    = jtpif_tclk_get,
  .jtdev_tclk_strobe = jtpif_tclk_strobe,
  .jtdev_led_green   = jtpif_led_green,
  .jtdev_led_red     = jtpif_led_red
};

