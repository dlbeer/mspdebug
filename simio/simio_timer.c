/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009, 2010 Daniel Beer
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

#include "simio_device.h"
#include "simio_timer.h"
#include "expr.h"
#include "output.h"

/* TACTL bits (taken from mspgcc headers) */
#define TASSEL2             0x0400  /* unused */
#define TASSEL1             0x0200  /* Timer A clock source select 1 */
#define TASSEL0             0x0100  /* Timer A clock source select 0 */
#define ID1                 0x0080  /* Timer A clock input divider 1 */
#define ID0                 0x0040  /* Timer A clock input divider 0 */
#define MC1                 0x0020  /* Timer A mode control 1 */
#define MC0                 0x0010  /* Timer A mode control 0 */
#define TACLR               0x0004  /* Timer A counter clear */
#define TAIE                0x0002  /* Timer A counter interrupt enable */
#define TAIFG               0x0001  /* Timer A counter interrupt flag */

/* TACCTLx flags (taken from mspgcc) */
#define CM1                 0x8000  /* Capture mode 1 */
#define CM0                 0x4000  /* Capture mode 0 */
#define CCIS1               0x2000  /* Capture input select 1 */
#define CCIS0               0x1000  /* Capture input select 0 */
#define SCS                 0x0800  /* Capture sychronize */
#define SCCI                0x0400  /* Latched capture signal (read) */
#define CAP                 0x0100  /* Capture mode: 1 /Compare mode : 0 */
#define OUTMOD2             0x0080  /* Output mode 2 */
#define OUTMOD1             0x0040  /* Output mode 1 */
#define OUTMOD0             0x0020  /* Output mode 0 */
#define CCIE                0x0010  /* Capture/compare interrupt enable */
#define CCI                 0x0008  /* Capture input signal (read) */
/* #define OUT                 0x0004  PWM Output signal if output mode 0 */
#define COV                 0x0002  /* Capture/compare overflow flag */
#define CCIFG               0x0001  /* Capture/compare interrupt flag */

#define MAX_CCRS		7

struct timer {
	struct simio_device	base;

	int			size;
	int			clock_input;
	int			go_down;

	address_t		base_addr;
	address_t		iv_addr;
	int			irq0;
	int			irq1;

	/* IO registers */
	uint16_t		tactl;
	uint16_t		tar;
	uint16_t		ctls[MAX_CCRS];
	uint16_t		ccrs[MAX_CCRS];
};

static struct simio_device *timer_create(char **arg_text)
{
	char *size_text = get_arg(arg_text);
	struct timer *tr;
	int size = 3;

	if (size_text) {
		address_t value;

		if (expr_eval(size_text, &value) < 0) {
			printc_err("timer: can't parse size: %s\n",
				   size_text);
			return NULL;
		}

		if (size < 2 || size > MAX_CCRS) {
			printc_err("timer: invalid size: %d\n", size);
			return NULL;
		}
	}

	tr = malloc(sizeof(*tr));
	if (!tr) {
		pr_error("timer: can't allocate memory");
		return NULL;
	}

	memset(tr, 0, sizeof(*tr));
	tr->base.type = &simio_timer;
	tr->size = size;
	tr->base_addr = 0x160;
	tr->iv_addr = 0x12e;
	tr->irq0 = 9;
	tr->irq1 = 8;

	return (struct simio_device *)tr;
}

static void timer_destroy(struct simio_device *dev)
{
	struct timer *tr = (struct timer *)dev;

	free(tr);
}

static void timer_reset(struct simio_device *dev)
{
	struct timer *tr = (struct timer *)dev;

	tr->tactl = 0;
	tr->tar = 0;
	tr->go_down = 0;
	memset(tr->ccrs, 0, sizeof(tr->ccrs));
	memset(tr->ctls, 0, sizeof(tr->ctls));
}

static int config_addr(address_t *addr, char **arg_text)
{
	char *text = get_arg(arg_text);

	if (!text) {
		printc_err("timer: config: expected address\n");
		return -1;
	}

	if (expr_eval(text, addr) < 0) {
		printc_err("timer: can't parse address: %s\n", text);
		return -1;
	}

	return 0;
}

static int config_irq(int *irq, char **arg_text)
{
	char *text = get_arg(arg_text);
	address_t value;

	if (!text) {
		printc_err("timer: config: expected interrupt number\n");
		return -1;
	}

	if (expr_eval(text, &value) < 0) {
		printc_err("timer: can't parse interrupt number: %s\n", text);
		return -1;
	}

	*irq = value;
	return 0;
}

static int config_channel(struct timer *tr, char **arg_text)
{
	char *which_text = get_arg(arg_text);
	char *value_text = get_arg(arg_text);
	address_t which;
	address_t value;
	int oldval;
	uint16_t edge_flags = 0;

	if (!(which_text && value_text)) {
		printc_err("timer: config: expected channel and value\n");
		return -1;
	}

	if (expr_eval(which_text, &which) < 0) {
		printc_err("timer: can't parse channel number: %s\n",
			   which_text);
		return -1;
	}

	if (expr_eval(value_text, &value) < 0) {
		printc_err("timer: can't parse channel value: %s\n",
			   value_text);
		return -1;
	}

	if (which > tr->size) {
		printc_err("timer: invalid channel number: %d\n", which);
		return -1;
	}

	oldval = tr->ctls[which] & CCI;
	tr->ctls[which] &= ~CCI;
	if (value)
		tr->ctls[which] |= CCI;

	if (oldval && !value)
		edge_flags |= CM1;
	if (!oldval && value)
		edge_flags |= CM0;

	printc_dbg("Timer channel %d: %s => %s\n",
		   which, oldval ? "H" : "L", value ? "H" : "L");

	if ((tr->ctls[which] & edge_flags) && (tr->ctls[which] & CAP)) {
		if (tr->ctls[which] & CCIFG) {
			printc_dbg("Timer capture overflow\n");
			tr->ctls[which] |= COV;
		} else {
			printc_dbg("Timer capture interrupt triggered\n");
			tr->ccrs[which] = tr->tar;
			tr->ctls[which] |= CCIFG;
		}
	}

	return 0;
}

static int timer_config(struct simio_device *dev,
			const char *param, char **arg_text)
{
	struct timer *tr = (struct timer *)dev;

	if (!strcasecmp(param, "base"))
		return config_addr(&tr->base_addr, arg_text);
	if (!strcasecmp(param, "iv"))
		return config_addr(&tr->iv_addr, arg_text);
	if (!strcasecmp(param, "irq0"))
		return config_irq(&tr->irq0, arg_text);
	if (!strcasecmp(param, "irq1"))
		return config_irq(&tr->irq1, arg_text);
	if (!strcasecmp(param, "set"))
		return config_channel(tr, arg_text);

	printc_err("timer: config: unknown parameter: %s\n", param);
	return -1;
}

static uint16_t calc_iv(struct timer *tr, int update)
{
	int i;

	for (i = 0; i < tr->size; i++)
		if ((tr->ctls[i] & (CCIE | CCIFG)) == (CCIE | CCIFG)) {
			/* Reading or writing TAIV clears the highest flag.
			   TACCR0 is cleared in timer_ack_interrupt(). */
			if (update && (i > 0))
				tr->ctls[i] &= ~CCIFG;
			return i * 2;
		}

	if ((tr->tactl & (TAIFG | TAIE)) == (TAIFG | TAIE)) {
		if (update)
			tr->tactl &= ~TAIFG;
		return 0xa;
	}

	return 0;
}

static int timer_info(struct simio_device *dev)
{
	struct timer *tr = (struct timer *)dev;
	int i;

	printc("Base address: 0x%04x\n", tr->base_addr);
	printc("IV address:   0x%04x\n", tr->iv_addr);
	printc("IRQ0:         %d\n", tr->irq0);
	printc("IRQ1:         %d\n", tr->irq1);
	printc("\n");
	printc("TACTL:        0x%04x\n", tr->tactl);
	printc("TAR:          0x%04x\n", tr->tar);
	printc("TAIV:         0x%02x\n", calc_iv(tr, 0));
	printc("\n");

	for (i = 0; i < tr->size; i++)
		printc("Channel %2d, TACTL = 0x%04x, TACCR = 0x%04x\n",
		       i, tr->ctls[i], tr->ccrs[i]);

	return 0;
}

static int timer_write(struct simio_device *dev,
			address_t addr, uint16_t data)
{
	struct timer *tr = (struct timer *)dev;

	if (addr == tr->base_addr) {
		tr->tactl = data & ~(TACLR | 0x08);
		if (data & TACLR)
			tr->tar = 0;

		return 0;
	}

	if (addr == tr->base_addr + 0x10) {
		tr->tar = data;
		return 0;
	}

	if (addr >= tr->base_addr + 2 &&
	    addr < tr->base_addr + tr->size + 2) {
		int index = ((addr & 0xf) - 2) >> 1;

		tr->ctls[index] = (data & 0xf9f7) |
			(tr->ctls[index] & 0x0608);
		return 0;
	}

	if (addr >= tr->base_addr + 0x12 &&
	    addr < tr->base_addr + tr->size + 0x12) {
		int index = ((addr & 0xf) - 2) >> 1;

		tr->ccrs[index] = data;
		return 0;
	}

	if (addr == tr->iv_addr) {
		/* Writing to TAIV clears the highest priority bit. */
		calc_iv(tr, 1);
		return 0;
	}

	return 1;
}

static int timer_read(struct simio_device *dev,
		       address_t addr, uint16_t *data)
{
	struct timer *tr = (struct timer *)dev;

	if (addr == tr->base_addr) {
		*data = tr->tactl;
		return 0;
	}

	if (addr == tr->base_addr + 0x10) {
		*data = tr->tar;
		return 0;
	}

	if (addr >= tr->base_addr + 2 &&
	    addr < tr->base_addr + tr->size + 2) {
		*data = tr->ctls[((addr & 0xf) - 2) >> 1];
		return 0;
	}

	if (addr >= tr->base_addr + 0x12 &&
	    addr < tr->base_addr + tr->size + 0x12) {
		*data = tr->ccrs[((addr & 0xf) - 2) >> 1];
		return 0;
	}

	if (addr == tr->iv_addr) {
		*data = calc_iv(tr, 1);
		return 0;
	}

	return 1;
}

static int timer_check_interrupt(struct simio_device *dev)
{
	struct timer *tr = (struct timer *)dev;
	int i;

	if ((tr->ctls[0] & (CCIE | CCIFG)) == (CCIE | CCIFG))
		return tr->irq0;

	if ((tr->tactl & (TAIFG | TAIE)) == (TAIFG | TAIE))
		return tr->irq1;

	for (i = 1; i < tr->size; i++)
		if ((tr->ctls[i] & (CCIE | CCIFG)) == (CCIE | CCIFG))
			return tr->irq1;

	return -1;
}

static void timer_ack_interrupt(struct simio_device *dev, int irq)
{
	struct timer *tr = (struct timer *)dev;

	if (irq == tr->irq0)
		tr->ctls[0] &= ~CCIFG;
	/* By design irq1 does not clear CCIFG or TAIFG automatically */
}

static void tar_step(struct timer *tr)
{
	switch ((tr->tactl >> 4) & 3) {
	case 0: break;
	case 1:
		if (tr->tar == tr->ccrs[0]) {
			tr->tar = 0;
			tr->tactl |= TAIFG;
		} else {
			tr->tar++;
		}
		break;

	case 2:
		tr->tar++;
		if (!tr->tar)
			tr->tactl |= TAIFG;
		break;

	case 3:
		if (tr->tar >= tr->ccrs[0])
			tr->go_down = 1;
		if (!tr->tar)
			tr->go_down = 0;

		if (tr->go_down) {
			tr->tar--;
			if (!tr->tar)
				tr->tactl |= TAIFG;
		} else {
			tr->tar++;
		}
		break;
	}
}

static void timer_step(struct simio_device *dev,
		       uint16_t status, const int *clocks)
{
	struct timer *tr = (struct timer *)dev;
	int pulse_count;
	int i;

	(void)status;

	/* Count input clock pulses */
	i = (tr->tactl >> 8) & 3;
	if (i == 2)
		tr->clock_input += clocks[SIMIO_SMCLK];
	else if (i == 1)
		tr->clock_input += clocks[SIMIO_ACLK];

	/* Figure out our clock input divide ratio */
	i = (tr->tactl >> 6) & 3;
	pulse_count = tr->clock_input >> i;
	tr->clock_input &= ((1 << i) - 1);

	/* Run the timer for however many pulses */
	for (i = 0; i < pulse_count; i++) {
		int j;

		for (j = 0; j < tr->size; j++)
			if (!(tr->ctls[j] & CAP) && (tr->tar == tr->ccrs[j])) {
				if (tr->ctls[j] & CCI)
					tr->ctls[j] |= SCCI;
				else
					tr->ctls[j] &= ~SCCI;

				tr->ctls[j] |= CCIFG;
			}

		tar_step(tr);
	}
}

const struct simio_class simio_timer = {
	.name = "timer",
	.help =
"This peripheral implements the Timer_A module.\n"
"\n"
"Constructor arguments: [size]\n"
"    Specify the number of capture/compare registers.\n"
"\n"
"Config arguments are:\n"
"    base <address>\n"
"        Set the peripheral base address.\n"
"    irq0 <interrupt>\n"
"        Set the interrupt vector for CCR0.\n"
"    irq1 <interrupt>\n"
"        Set the interrupt vector for CCR1.\n"
"    iv <address>\n"
"        Set the interrupt vector register address.\n"
"    set <channel> <0|1>\n"
"        Set the capture input value on the given channel.\n",

	.create			= timer_create,
	.destroy		= timer_destroy,
	.reset			= timer_reset,
	.config			= timer_config,
	.info			= timer_info,
	.write			= timer_write,
	.read			= timer_read,
	.check_interrupt	= timer_check_interrupt,
	.ack_interrupt		= timer_ack_interrupt,
	.step			= timer_step
};
