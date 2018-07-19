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

#include <stdbool.h>
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
/* TBCTL bits */
#define TBCLGRP1            0x4000  /* Timer B Compare latch load group 1 */
#define TBCLGRP0            0x2000  /* Timer B Compare latch load group 0 */
#define CNTL1               0x1000  /* Timer B Counter length 1 */
#define CNTL0               0x0800  /* Timer B Counter length 0 */

/* TACCTLx flags (taken from mspgcc) */
#define CM1                 0x8000  /* Capture mode 1 */
#define CM0                 0x4000  /* Capture mode 0 */
#define CCIS1               0x2000  /* Capture input select 1 */
#define CCIS0               0x1000  /* Capture input select 0 */
#define SCS                 0x0800  /* Capture synchronize */
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
/* TBCCTLx flags */
#define CLLD1               0x0400  /* Compare latch load source 1 */
#define CLLD0               0x0200  /* Compare latch load source 0 */
/* Timer IV words */
#define TAIV_TAIFG          0x000A  /* Interrupt vector word for TAIFG */
#define TBIV_TBIFG          0x000E  /* Interrupt vector word for TBIFG */

#define MAX_CCRS		7

typedef enum {
	TIMER_TYPE_A,
	TIMER_TYPE_B,
} timer_type_t;

struct timer {
	struct simio_device	base;

	int			size;
	int			clock_input;
	bool			go_down;

	address_t		base_addr;
	address_t		iv_addr;
	int			irq0;
	int			irq1;
	timer_type_t		timer_type;

	/* IO registers */
	uint16_t		tactl;
	uint16_t		tar;
	uint16_t		ctls[MAX_CCRS];
	uint16_t		ccrs[MAX_CCRS];
	/* Compare latch for Timer_B */
	uint16_t		bcls[MAX_CCRS];
	/* True if ccrs[index] has a value set. Used for compare latch grouping */
	bool			valid_ccrs[MAX_CCRS];
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
		size = value;

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
	tr->timer_type = TIMER_TYPE_A;

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
	tr->go_down = false;
	memset(tr->ccrs, 0, sizeof(tr->ccrs));
	memset(tr->ctls, 0, sizeof(tr->ctls));
	memset(tr->bcls, 0, sizeof(tr->bcls));
	memset(tr->valid_ccrs, false, sizeof(tr->valid_ccrs));
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

static int config_type(timer_type_t *timer_type, char **arg_text)
{
	char *text = get_arg(arg_text);

	if (!text) {
		printc_err("timer: config: expected type\n");
		return -1;
	}

	if (!strcasecmp(text, "A")) {
		*timer_type = TIMER_TYPE_A;
		return 0;
	}
	if (!strcasecmp(text, "B")) {
		*timer_type = TIMER_TYPE_B;
		return 0;
	}

	printc_err("timer: can't parse type: %s\n", text);
	return -1;
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

static void trigger_capture(struct timer *tr, int which, int oldval, int value)
{
	uint16_t edge_flags = 0;

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
}

static int config_channel(struct timer *tr, char **arg_text)
{
	char *which_text = get_arg(arg_text);
	char *value_text = get_arg(arg_text);
	address_t which;
	address_t value;

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

	trigger_capture(tr, which, tr->ctls[which] & CCI, value);

	return 0;
}

static int timer_config(struct simio_device *dev,
			const char *param, char **arg_text)
{
	struct timer *tr = (struct timer *)dev;

	if (!strcasecmp(param, "base"))
		return config_addr(&tr->base_addr, arg_text);
	if (!strcasecmp(param, "type"))
		return config_type(&tr->timer_type, arg_text);
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
		return (tr->timer_type == TIMER_TYPE_A) ?
			TAIV_TAIFG : TBIV_TBIFG;
	}

	return 0;
}

static int timer_info(struct simio_device *dev)
{
	struct timer *tr = (struct timer *)dev;
	int i;
	char timer_type = (tr->timer_type == TIMER_TYPE_A) ? 'A' : 'B';

	printc("Base address: 0x%04x\n", tr->base_addr);
	printc("IV address:   0x%04x\n", tr->iv_addr);
	printc("IRQ0:	      %d\n", tr->irq0);
	printc("IRQ1:	      %d\n", tr->irq1);
	printc("\n");
	printc("T%cCTL:	       0x%04x\n", timer_type, tr->tactl);
	printc("T%cR:	       0x%04x\n", timer_type, tr->tar);
	printc("T%cIV:	       0x%02x\n", timer_type, calc_iv(tr, 0));
	printc("\n");

	for (i = 0; i < tr->size; i++) {
		printc("T%cCCTL%d = 0x%04x, T%cCCR%d = 0x%04x",
		       timer_type, i, tr->ctls[i], timer_type, i, tr->ccrs[i]);
		if (tr->timer_type == TIMER_TYPE_B)
			printc(", TBCL%d = 0x%04x", i, tr->bcls[i]);
		printc("\n");
	}

	return 0;
}

static uint16_t tar_mask(struct timer *tr)
{
	if (tr->timer_type == TIMER_TYPE_B) {
		switch (tr->tactl & (CNTL1 | CNTL0)) {
		case 0: /* 16 bits */
			break;
		case CNTL0: /* 12 bits */
			return 0x0fff;
		case CNTL1: /* 10 bits */
			return 0x03ff;
		case CNTL1 | CNTL0: /* 8 bits */
			return 0x00ff;
		}
	}
	return 0xffff;
}

static void set_bcl(struct timer *tr, int index)
{
	tr->bcls[index] = tr->ccrs[index];
	tr->valid_ccrs[index] = false;
}

static bool no_double_buffer(struct timer *tr, int index)
{
	uint16_t clgrp = tr->tactl & (TBCLGRP1 | TBCLGRP0);

	if (clgrp == TBCLGRP0 && (index == 2 || index == 4 || index == 6))
		return (tr->ctls[index - 1] & (CLLD1 | CLLD0)) == 0;
	if (clgrp == TBCLGRP1 && (index == 2 || index == 5))
		return (tr->ctls[index - 1] & (CLLD1 | CLLD0)) == 0;
	if (clgrp == TBCLGRP1 && (index == 3 || index == 6))
		return (tr->ctls[index - 2] & (CLLD1 | CLLD0)) == 0;
	if (clgrp == (TBCLGRP1 | TBCLGRP0))
		return (tr->ctls[1] & (CLLD1 | CLLD0)) == 0;
	return (tr->ctls[index] & (CLLD1 | CLLD0)) == 0;
}

static void set_ccr(struct timer *tr, int index, uint16_t data)
{
	tr->ccrs[index] = data;
	tr->valid_ccrs[index] = true;

	if (tr->timer_type == TIMER_TYPE_A) {
		/* When CCR[0] set is less than TAR in up mode, TAR rolls to
		 * 0. */
		if (index == 0 && data < tr->tar &&
		    (tr->tactl & (MC1 | MC0)) == MC0) {
			tr->go_down = true;
		}
	}
	if (tr->timer_type == TIMER_TYPE_B) {
		/* Writing TBCCRx triggers update TBCLx immediately.  No
		 * grouping. */
		if (no_double_buffer(tr, index)) {
			set_bcl(tr, index);
		}
	}
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
		tr->tar = data & tar_mask(tr);
		return 0;
	}

	if (addr >= tr->base_addr + 2 &&
	    addr < tr->base_addr + (tr->size << 1) + 2) {
		int index = ((addr & 0xf) - 2) >> 1;
		uint16_t oldval = tr->ctls[index];
		uint16_t mask;

		if (tr->timer_type == TIMER_TYPE_A)
			mask = 0x0608;
		if (tr->timer_type == TIMER_TYPE_B)
			mask = 0x0008;
		tr->ctls[index] = (data & ~mask) | (oldval & mask);
		/* Check capture initiated by Software */
		if ((data & (CAP | CCIS1)) == (CAP | CCIS1))
			trigger_capture(tr, index, oldval & CCI, data & CCIS0);
		return 0;
	}

	if (addr >= tr->base_addr + 0x12 &&
	    addr < tr->base_addr + (tr->size << 1) + 0x12) {
		int index = ((addr & 0xf) - 2) >> 1;

		set_ccr(tr, index, data);
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
	    addr < tr->base_addr + (tr->size << 1) + 2) {
		*data = tr->ctls[((addr & 0xf) - 2) >> 1];
		return 0;
	}

	if (addr >= tr->base_addr + 0x12 &&
	    addr < tr->base_addr + (tr->size << 1) + 0x12) {
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

static uint16_t get_ccr(struct timer *tr, int index) {
	if (tr->timer_type == TIMER_TYPE_B)
		return tr->bcls[index];
	return tr->ccrs[index];
}

static uint16_t tar_increment(struct timer *tr)
{
	tr->tar++;
	tr->tar &= tar_mask(tr);
	return tr->tar;
}

static uint16_t tar_decrement(struct timer *tr)
{
	tr->tar--;
	tr->tar &= tar_mask(tr);
	return tr->tar;
}

static void tar_step(struct timer *tr)
{
	switch ((tr->tactl >> 4) & 3) {
	case 0:
		break;
	case 1:
		if (tr->tar == get_ccr(tr, 0) || tr->go_down) {
			tr->tar = 0;
			tr->tactl |= TAIFG;
			tr->go_down = false;
		} else {
			tar_increment(tr);
		}
		break;

	case 2:
		if (tar_increment(tr) == 0)
			tr->tactl |= TAIFG;
		break;

	case 3:
		if (tr->tar >= get_ccr(tr, 0))
			tr->go_down = true;
		if (tr->tar == 0)
			tr->go_down = false;

		if (tr->go_down) {
			if (tar_decrement(tr) == 0)
				tr->tactl |= TAIFG;
		} else {
			tar_increment(tr);
		}
		break;
	}
}

static void update_bcls(struct timer *tr, int start, int n)
{
	int index;
	const int end = start + n;

	for (index = start; index < end; index++) {
		if (!tr->valid_ccrs[index])
			return;
	}

	for (index = start; index < end; index++)
		set_bcl(tr, index);
}

static void update_bcl_group(struct timer *tr, int index)
{
	switch (tr->tactl & (TBCLGRP1 | TBCLGRP0)) {
	case 0: /* Individual */
		set_bcl(tr, index);
		return;
	case TBCLGRP0: /* 0, 1&2, 3&4, 5&6 */
		if (index == 0) {
			update_bcls(tr, index, 1);
		} else if (index == 1 || index == 3 || index == 5) {
			update_bcls(tr, index, 2);
		}
		return;
	case TBCLGRP1: /* 0, 1&2&3, 4&5&6 */
		if (index == 0) {
			update_bcls(tr, index, 1);
		} else if (index == 1 || index == 4) {
			update_bcls(tr, index, 3);
		}
		return;
	case TBCLGRP1 | TBCLGRP0: /* All at once */
		if (index == 1) {
			update_bcls(tr, 0, tr->size);
		}
		return;
	}
}

static void comparator_step(struct timer *tr, int index)
{
	if (tr->timer_type == TIMER_TYPE_A) {
		if (tr->tar == get_ccr(tr, index)) {
			tr->ctls[index] |= CCIFG;
			if (tr->ctls[index] & CCI)
				tr->ctls[index] |= SCCI;
			else
				tr->ctls[index] &= ~SCCI;
		}
	}

	if (tr->timer_type == TIMER_TYPE_B) {
		const uint16_t mc = tr->tactl & (MC1 | MC0);
		const uint16_t clld = tr->ctls[index] & (CLLD1 | CLLD0);
		if (tr->tar == 0) {
			if (clld == CLLD0 || (clld == CLLD1 && mc != 0)) {
				update_bcl_group(tr, index);
			}
		}
		if (tr->tar == get_ccr(tr, index)) {
			tr->ctls[index] |= CCIFG;
			if ((clld == CLLD1 && mc == (MC1 | MC0)) ||
			    clld == (CLLD1 | CLLD0)) {
				update_bcl_group(tr, index);
			}
		}
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

		for (j = 0; j < tr->size; j++) {
			if (!(tr->ctls[j] & CAP))
				comparator_step(tr, j);
		}
		tar_step(tr);
	}
}

const struct simio_class simio_timer = {
	.name = "timer",
	.help =
	"This peripheral implements the Timer_A and Timer_B module.\n"
	"\n"
	"Constructor arguments: [size]\n"
	"    Specify the number of capture/compare registers.\n"
	"\n"
	"Config arguments are:\n"
	"    base <address>\n"
	"        Set the peripheral base address.\n"
	"    type <A|B>\n"
	"        Set timer type.\n"
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
