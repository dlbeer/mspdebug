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
#include "simio_wdt.h"
#include "output.h"
#include "expr.h"

/* WDTCTL flags, taken from mspgcc.
 *
 * Watchdog timer password. Always read as 069h. Must be written as 05Ah,
 * or a PUC will be generated.
 */
#define WDTIS0              0x0001
#define WDTIS1              0x0002
#define WDTSSEL             0x0004
#define WDTCNTCL            0x0008
#define WDTTMSEL            0x0010
#define WDTNMI              0x0020
#define WDTNMIES            0x0040
#define WDTHOLD             0x0080
#define WDTPW               0x5A00

/* Flags in IE1 */
#define WDTIE		0x01
#define NMIIE		0x10

/* Flags in IFG1 */
#define WDTIFG		0x01
#define NMIIFG		0x10

struct wdt {
	struct simio_device		base;

	int				pin_state;
	int				wdt_irq;

	int				count_reg;
	int				reset_triggered;
	uint8_t				wdtctl;
};

struct simio_device *wdt_create(char **arg_text)
{
	struct wdt *w = malloc(sizeof(*w));

	(void)arg_text;

	if (!w) {
		pr_error("wdt: can't allocate memory");
		return NULL;
	}

	memset(w, 0, sizeof(*w));
	w->base.type = &simio_wdt;
	w->pin_state = 1;
	w->wdt_irq = 10;

	w->reset_triggered = 0;
	w->wdtctl = 0;
	w->count_reg = 0;

	return (struct simio_device *)w;
}

static void wdt_destroy(struct simio_device *dev)
{
	free(dev);
}

static void wdt_reset(struct simio_device *dev) {
	struct wdt *w = (struct wdt *)dev;

	w->reset_triggered = 0;
	w->wdtctl = 0;
	w->count_reg = 0;
}

static int parse_int(int *val, char **arg_text)
{
	const char *text = get_arg(arg_text);
	address_t value;

	if (!text) {
		printc_err("wdt: expected integer argument\n");
		return -1;
	}

	if (expr_eval(text, &value) < 0) {
		printc_err("wdt: couldn't parse argument: %s\n", text);
		return -1;
	}

	*val = value;
	return 0;
}

static int wdt_config(struct simio_device *dev, const char *param,
		      char **arg_text)
{
	struct wdt *w = (struct wdt *)dev;

	if (!strcasecmp(param, "nmi")) {
		int old = w->pin_state;

		if (parse_int(&w->pin_state, arg_text) < 0)
			return -1;

		if (w->wdtctl & WDTNMI) {
			if (((w->wdtctl & WDTNMIES) &&
				old && !w->pin_state) ||
			    (!(w->wdtctl & WDTNMIES) &&
				 !old && w->pin_state))
				simio_sfr_modify(SIMIO_IFG1, NMIIFG, NMIIFG);
		}

		return 0;
	}

	if (!strcasecmp(param, "irq"))
		return parse_int(&w->wdt_irq, arg_text);

	printc_err("wdt: unknown configuration parameter: %s\n", param);
	return -1;
}

static int wdt_info(struct simio_device *dev)
{
	struct wdt *w = (struct wdt *)dev;

	printc("Configured WDT IRQ:  %d\n", w->wdt_irq);
	printc("WDTCTL:              0x__%02x\n", w->wdtctl);
	printc("NMI/RST# pin:        %s\n", w->pin_state ? "HIGH" : "low");
	printc("Counter:             0x%04x\n", w->count_reg);
	printc("Reset:               %s\n",
	       w->reset_triggered ? "TRIGGERED" : "not triggered");

	return 0;
}

static int wdt_write(struct simio_device *dev, address_t addr, uint16_t data)
{
	struct wdt *w = (struct wdt *)dev;

	if (addr != 0x120)
		return 1;

	if (data >> 8 != 0x5a)
		w->reset_triggered = 1;

	w->wdtctl = data & 0xf7;
	if (w->wdtctl & WDTCNTCL)
		w->count_reg = 0;

	return 0;
}

static int wdt_read(struct simio_device *dev, address_t addr, uint16_t *data)
{
	struct wdt *w = (struct wdt *)dev;

	if (addr != 0x120)
		return 1;

	*data = 0x6900 | w->wdtctl;
	return 0;
}

static int wdt_check_interrupt(struct simio_device *dev)
{
	struct wdt *w = (struct wdt *)dev;
	uint8_t flags;

	if (!(w->wdtctl & WDTNMI) && !w->pin_state)
		return 15;

	if (w->reset_triggered)
		return 15;

	flags = simio_sfr_get(SIMIO_IFG1) & simio_sfr_get(SIMIO_IE1);

	if (flags & NMIIFG)
		return 14;

	if (flags & WDTIFG)
		return w->wdt_irq;

	return -1;
}

static void wdt_ack_interrupt(struct simio_device *dev, int irq)
{
	struct wdt *w = (struct wdt *)dev;

	if (irq == 14)
		simio_sfr_modify(SIMIO_IFG1, NMIIFG, 0);
	else if (irq == w->wdt_irq)
		simio_sfr_modify(SIMIO_IFG1, WDTIFG, 0);
}

static void wdt_step(struct simio_device *dev, uint16_t status_register,
		     const int *clocks)
{
	struct wdt *w = (struct wdt *)dev;
	int max = 1;

	(void)status_register;

	/* If on hold, nothing happens */
	if (w->wdtctl & WDTHOLD)
		return;

	/* Count input clock cycles */
	if (w->wdtctl & WDTSSEL)
		w->count_reg += clocks[SIMIO_ACLK];
	else
		w->count_reg += clocks[SIMIO_SMCLK];

	/* Figure out the divisor */
	switch (w->wdtctl & 3) {
	case 0: max = 32768; break;
	case 1: max = 8192; break;
	case 2: max = 512; break;
	case 3: max = 64; break;
	}

	/* Check for overflow */
	if (w->count_reg >= max) {
		if (w->wdtctl & WDTTMSEL)
			simio_sfr_modify(SIMIO_IFG1, WDTIFG, WDTIFG);
		else
			w->reset_triggered = 1;
	}

	w->count_reg &= (max - 1);
}

const struct simio_class simio_wdt = {
	.name = "wdt",
	.help =
"This module simulates the Watchdog Timer+ peripheral. There are no\n"
"constructor arguments. Configuration parameters are:\n"
"    irq <irq>\n"
"        Set the interrupt vector for the WDT interrupt.\n"
"    nmi <0|1>\n"
"        Set the state of the NMI/RST# pin.\n",

	.create			= wdt_create,
	.destroy		= wdt_destroy,
	.reset			= wdt_reset,
	.config			= wdt_config,
	.info			= wdt_info,
	.write			= wdt_write,
	.read			= wdt_read,
	.check_interrupt	= wdt_check_interrupt,
	.ack_interrupt		= wdt_ack_interrupt,
	.step			= wdt_step
};
