/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2012 Daniel Beer
 * Copyright (C) 2012 Peter Bägel
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

#ifdef __linux__
/*===== includes =============================================================*/

#include <fcntl.h>
#include <unistd.h>
#include <linux/ppdev.h>
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
#define STROBE   ((unsigned char)0x01) /* inverted by PC-hardware */
#define AUTOFEED ((unsigned char)0x02)
#define INIT     ((unsigned char)0x04)
#define SELECTIN ((unsigned char)0x08)
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
	if (ioctl(p->port, PPWDATA, &p->data_register) < 0) {
		pr_error("ioctl: PPWDATA");
		p->failed = 1;
	}
}

static void do_ppwcontrol(struct jtdev *p)
{
	if (ioctl(p->port, PPWCONTROL, &p->control_register) < 0) {
		pr_error("ioctl: PPWCONTROL");
		p->failed = 1;
	}
}

int jtdev_open(struct jtdev *p, const char *device)
{
	p->port = open(device, O_RDWR);
	if (p->port < 0) {
		printc_err("jtdev: can't open %s: %s\n",
			   device, last_error());
		return -1;
	}

	if (ioctl(p->port, PPCLAIM, NULL) < 0) {
		printc_err("jtdev: PPCLAIM: %s: %s\n",
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

void jtdev_close(struct jtdev *p)
{
	if (ioctl(p->port, PPRELEASE, NULL) < 0)
		pr_error("warning: jtdev: failed to release port");

	close(p->port);
}

void jtdev_power_on(struct jtdev *p)
{
	/* power supply on */
	p->data_register |= POWER;
	do_ppwdata(p);
}

void jtdev_power_off(struct jtdev *p)
{
	/* power supply off */
	p->data_register &= ~POWER;
	do_ppwdata(p);

	/* a high, inactive reset also powers the target */
	/* reset pin is inverted by PC hardware          */
	p->control_register |= RESET;
	do_ppwcontrol(p);
}

void jtdev_connect(struct jtdev *p)
{
	p->control_register |= (TEST | ENABLE);
	do_ppwcontrol(p);
}

void jtdev_release(struct jtdev *p)
{
	p->control_register &= ~(TEST | ENABLE);
	do_ppwcontrol(p);
}

void jtdev_tck(struct jtdev *p, int out)
{
	if (out)
		p->data_register |= TCK;
	else
		p->data_register &= ~TCK;

	do_ppwdata(p);
}

void jtdev_tms(struct jtdev *p, int out)
{
	if (out)
		p->data_register |= TMS;
	else
		p->data_register &= ~TMS;

	do_ppwdata(p);
}

void jtdev_tdi(struct jtdev *p, int out)
{
	if (out)
		p->data_register |= TDI;
	else
		p->data_register &= ~TDI;

	do_ppwdata(p);
}

void jtdev_rst(struct jtdev *p, int out)
{
	/* reset pin is inverted by PC hardware */
	if (out)
		p->control_register &= ~RESET;
	else
		p->control_register |= RESET;

	do_ppwcontrol(p);
}

void jtdev_tst(struct jtdev *p, int out)
{
	if (out)
		p->control_register |= TEST;
	else
		p->control_register &= ~TEST;

	do_ppwcontrol(p);
}

int jtdev_tdo_get(struct jtdev *p)
{
	uint8_t input;

	if (ioctl(p->port, PPRSTATUS, &input) < 0) {
		pr_error("ioctl: PPRSTATUS");
		p->failed = 1;
		return 0;
	}

	return (input & TDO) ? 1 : 0;
}

void jtdev_tclk(struct jtdev *p, int out)
{
	if (out)
		p->data_register |= TCLK;
	else
		p->data_register &= ~TCLK;

	do_ppwdata(p);
}

int jtdev_tclk_get(struct jtdev *p)
{
	return (p->data_register & TCLK) ? 1 : 0;
}

void jtdev_tclk_strobe(struct jtdev *p, unsigned int count)
{
	int i;

	for (i = 0; i < count; i++) {
		jtdev_tclk(p, 1);
		jtdev_tclk(p, 0);

		if (p->failed)
			return;
	}
}

void jtdev_led_green(struct jtdev *p, int out)
{
	if (out)
		p->data_register |= LED_GREEN;
	else
		p->data_register &= ~LED_GREEN;

	do_ppwdata(p);
}

void jtdev_led_red(struct jtdev *p, int out)
{
	if (out)
		p->data_register |= LED_RED;
	else
		p->data_register &= ~LED_RED;

	do_ppwdata(p);
}
#else /* __linux__ */
int jtdev_open(struct jtdev *p, const char *device)
{
	printc_err("jtdev: driver is only supported for Linux\n");
	p->failed = 1;
	return -1;
}

void jtdev_close(struct jtdev *p) { }

void jtdev_power_on(struct jtdev *p) { }
void jtdev_power_off(struct jtdev *p) { }
void jtdev_connect(struct jtdev *p) { }
void jtdev_release(struct jtdev *p) { }

void jtdev_tck(struct jtdev *p, int out) { }
void jtdev_tms(struct jtdev *p, int out) { }
void jtdev_tdi(struct jtdev *p, int out) { }
void jtdev_rst(struct jtdev *p, int out) { }
void jtdev_tst(struct jtdev *p, int out) { }
int jtdev_tdo_get(struct jtdev *p) { return 0; }

void jtdev_tclk(struct jtdev *p, int out) { }
int jtdev_tclk_get(struct jtdev *p) { return 0; }
void jtdev_tclk_strobe(struct jtdev *p, unsigned int count) { }

void jtdev_led_green(struct jtdev *p, int out) { }
void jtdev_led_red(struct jtdev *p, int out) { }
#endif
