/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009, 2010 Daniel Beer
 * Copyright (C) 2018 Tadashi G. Takaoka
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

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ctrlc.h"
#include "simio.h"
#include "stab.h"

/* Module under test */
#include "simio_timer.c"


/*
 * Helper functions for testing timer simio.
 */

static char **setup_args(const char *text)
{
	static char args_buf[80];
	static char *args;

	strncpy(args_buf, text, sizeof(args_buf));
	args = args_buf;
	return &args;
}

static int* setup_clocks(int mclk, int smclk, int aclk)
{
	static int clocks[SIMIO_NUM_CLOCKS];

	clocks[SIMIO_MCLK] = mclk;
	clocks[SIMIO_SMCLK] = smclk;
	clocks[SIMIO_ACLK] = aclk;
	return clocks;
}

static struct simio_device *create_timer(const char *arg)
{
	return simio_timer.create(setup_args(arg));
}

static int config_timer(struct simio_device *dev, const char *param,
			const char *arg)
{
	return simio_timer.config(dev, param, setup_args(arg));
}

/* Register offset from base_addr */
#define TxCTL		0x00
#define TxR		0x10
#define TxCCTL(index)	(0x02 + (index) * 2)
#define TxCCR(index)	(0x12 + (index) * 2)
#define TxIV_TxIFG(index) ((index) * 2)

static uint16_t read_timer(struct simio_device *dev, int offset)
{
	struct timer *tmr = (struct timer *)dev;
	uint16_t data;
	assert(simio_timer.read(dev, tmr->base_addr + offset, &data) == 0);
	return data;
}

static void write_timer(struct simio_device *dev, int offset, uint16_t data)
{
	struct timer *tmr = (struct timer *)dev;
	assert(simio_timer.write(dev, tmr->base_addr + offset, data) == 0);
}

static uint16_t read_iv(struct simio_device *dev)
{
	struct timer *tmr = (struct timer *)dev;
	uint16_t iv;
	assert(simio_timer.read(dev, tmr->iv_addr, &iv) == 0);
	return iv;
}

static void step_smclk(struct simio_device *dev, int smclk)
{
	uint16_t status_register = 0;
	simio_timer.step(dev, status_register, setup_clocks(0, smclk, 0));
}

static void step_aclk(struct simio_device *dev, int aclk)
{
	uint16_t status_register = 0;
	simio_timer.step(dev, status_register, setup_clocks(0, 0, aclk));
}

static bool check_noirq(struct simio_device *dev)
{
	return simio_timer.check_interrupt(dev) < 0;
}

static bool check_irq0(struct simio_device *dev)
{
	struct timer *tmr = (struct timer *)dev;
	return simio_timer.check_interrupt(dev) == tmr->irq0;
}

static int check_irq1(struct simio_device *dev)
{
	struct timer *tmr = (struct timer *)dev;
	return simio_timer.check_interrupt(dev) == tmr->irq1;
}

static void ack_irq0(struct simio_device *dev)
{
	struct timer *tmr = (struct timer *)dev;
	simio_timer.ack_interrupt(dev, tmr->irq0);
}


/*
 * Working variables for tests.
 */

static struct simio_device *dev;


/*
 * Set up and tear down for each test.
 */

static void set_up()
{
	setup_args("");
	setup_clocks(0, 0, 0);
	dev = NULL;
}

static void tear_down()
{
	if (dev != NULL) {
		simio_timer.destroy(dev);
		dev = NULL;
	}
}

#define assert_not(e) assert(!(e))

/*
 * Set up for globals.
 */

static void set_up_globals()
{
	ctrlc_init();
	stab_init();
}


/*
 * Tests for timer simio.
 */

static void test_create_no_option()
{
	dev = create_timer("");

	assert(dev != NULL);
	assert(dev->type != NULL);
	assert(strcmp(dev->type->name, "timer") == 0);

	// Check default values.
	struct timer *tmr = (struct timer *)dev;
	assert(tmr->size == 3);
	assert(tmr->timer_type == TIMER_TYPE_A);
	assert(tmr->base_addr == 0x0160);
	assert(tmr->iv_addr == 0x012e);
	assert(tmr->irq0 == 9);
	assert(tmr->irq1 == 8);
}

static void test_create_with_size_7()
{
	dev = create_timer("7");

	// Timer can have 7 comparators/captures at most.
	struct timer *tmr = (struct timer *)dev;
	assert(tmr->size == 7);
}

static void test_create_with_size_2()
{
	dev = create_timer("2");

	// Timer should have 2 comparators/captures at least.
	struct timer *tmr = (struct timer *)dev;
	assert(tmr->size == 2);
}

static void test_create_with_size_8()
{
	dev = create_timer("8");

	// Timer can't have 8 or more comparators/captures.
	assert(dev == NULL);
}


static void test_create_with_size_1()
{
	dev = create_timer("1");

	// Timer can't have 1 or no comparator/capture.
	assert(dev == NULL);
}

static void test_config_type_default()
{
	dev = create_timer("");

	// Default timer type is A.
	struct timer *tmr = (struct timer *)dev;
	assert(tmr->timer_type == TIMER_TYPE_A);
}

static void test_config_type_A()
{
	dev = create_timer("");

	// Timer can configured as type A.
	assert(config_timer(dev, "type", "A") == 0);
	struct timer *tmr = (struct timer *)dev;
	assert(tmr->timer_type == TIMER_TYPE_A);
}

static void test_config_type_B()
{
	dev = create_timer("");

	// Timer can configured as type B.
	struct timer *tmr = (struct timer *)dev;
	assert(config_timer(dev, "type", "B") == 0);
	assert(tmr->timer_type == TIMER_TYPE_B);
}

static void test_config_type_bad()
{
	dev = create_timer("");

	struct timer *tmr = (struct timer *)dev;
	// Timer can't configured other than A/B.
	assert(config_timer(dev, "type", "bad") != 0);
}

static void test_config_type_empty()
{
	dev = create_timer("");

	struct timer *tmr = (struct timer *)dev;
	// Timer type can't be empty.
	assert(config_timer(dev, "type", "") != 0);
}

static void test_address_space()
{
	dev = create_timer("7");
	struct timer *tmr = (struct timer *)dev;

	for (uint32_t a = 0; a < 0x10000; a += 2) {
		const address_t addr = (address_t)a;
		uint16_t data;
		// Timer has 16 word registers from its base address.
		if (addr >= tmr->base_addr && addr < tmr->base_addr + 0x20) {
			assert(simio_timer.read(dev, addr, &data) == 0);
			assert(simio_timer.write(dev, addr, data) == 0);
		}
		// Timer has 1 word register as vector register.
		else if (addr == tmr->iv_addr) {
			assert(simio_timer.read(dev, addr, &data) == 0);
			assert(simio_timer.write(dev, addr, data) == 0);
		}
		// Timer has no register other than above.
		else {
			assert(simio_timer.read(dev, addr, &data) != 0);
			assert(simio_timer.write(dev, addr, data) != 0);
		}
	}
}

static void test_timer_continuous()
{
	dev = create_timer("");

	/* Continuous mode, SMCLK, clear */
	write_timer(dev, TxCTL, MC1 | TASSEL1 | TACLR);
	// When TxCCR[0]=0 in continuous mode, then counter stops.
	write_timer(dev, TxCCTL(0), 0);
	write_timer(dev, TxCCR(0), 0);

	assert(read_timer(dev, TxR) == 0);

	step_smclk(dev, 10);
	assert(read_timer(dev, TxR) == 10);

	step_aclk(dev, 10);
	assert(read_timer(dev, TxR) == 10);
}

static void test_timer_stop()
{
	dev = create_timer("");

	/* Stop mode, SMCLK, clear */
	write_timer(dev, TxCTL, TASSEL1 | TACLR);
	// When TxCCR[0]>0 in stop mode, then counter stops.
	write_timer(dev, TxCCTL(0), 0);
	write_timer(dev, TxCCR(0), 100);

	assert(read_timer(dev, TxR) == 0);

	step_smclk(dev, 10);
	assert(read_timer(dev, TxR) == 0);
}

static void test_timer_up_stop()
{
	dev = create_timer("");

	/* Up mode, SMCLK, clear */
	write_timer(dev, TxCTL, MC0 | TASSEL1 | TACLR);
	// When TxCCR[0]=0 in up mode, then counter stops.
	write_timer(dev, TxCCTL(0), 0);
	write_timer(dev, TxCCR(0), 0);

	assert(read_timer(dev, TxR) == 0);

	step_smclk(dev, 10);
	assert(read_timer(dev, TxR) == 0);
}

static void test_timer_updown_stop()
{
	dev = create_timer("");

	/* Up/Down mode, SMCLK, clear */
	write_timer(dev, TxCTL, MC1 | MC0 | TASSEL1 | TACLR);
	// When TxCCR[0]=0 in up/down mode, then counter stops.
	write_timer(dev, TxCCTL(0), 0);
	write_timer(dev, TxCCR(0), 0);

	assert(read_timer(dev, TxR) == 0);

	step_smclk(dev, 10);
	assert(read_timer(dev, TxR) == 0);
}

static void test_timer_a_up()
{
	dev = create_timer("");

	/* Up mode, SMCLK, enable interrupt, clear */
	write_timer(dev, TxCTL, MC0 | TASSEL1 | TACLR | TAIE);
	/* Enable compare interrupt */
	write_timer(dev, TxCCTL(0), CCIE);
	write_timer(dev, TxCCR(0), 10);
	step_smclk(dev, 10);
	assert(read_timer(dev, TxR) == 10);

	// Compare interrupt and TAIFG interrupt both will happen.
	step_smclk(dev, 1);
	assert(read_timer(dev, TxR) == 0);
	assert(read_timer(dev, TxCCTL(0)) & CCIFG);
	assert(read_timer(dev, TxCTL) & TAIFG);
	// Compare interrupt has priority.
	assert(check_irq0(dev));
	ack_irq0(dev);
	assert_not(read_timer(dev, TxCCTL(0)) & CCIFG);
	assert(read_timer(dev, TxCTL) & TAIFG);
	// Then TAIFG interrupt should happen.
	assert(check_irq1(dev));
	assert(read_iv(dev) == TAIV_TAIFG);
	assert_not(read_timer(dev, TxCTL) & TAIFG);
	assert(check_noirq(dev));
}

static void test_timer_a_up_change_period()
{
	dev = create_timer("");

	/* Up mode, SMCLK, enable interrupt, clear */
	write_timer(dev, TxCTL, MC0 | TASSEL1 | TACLR | TAIE);
	write_timer(dev, TxCCTL(0), CCIE);
	write_timer(dev, TxCCR(0), 10);
	step_smclk(dev, 8);

	// Changing period to less than current count will roll down
	// counter to 0.
	assert(read_timer(dev, TxR) == 8);
	write_timer(dev, TxCCR(0), 5);
	assert(check_noirq(dev));

	// TAIFG interrupt should happen.
	step_smclk(dev, 1);
	assert(read_timer(dev, TxR) == 0);
	assert(check_irq1(dev));
	assert(read_timer(dev, TxCTL) & TAIFG);
	assert(read_iv(dev) == TAIV_TAIFG);
	assert(check_noirq(dev));
	assert_not(read_timer(dev, TxCTL) & TAIFG);

	step_smclk(dev, 4);
	assert(read_timer(dev, TxR) == 4);
	// Changing period to greater that current count will continue
	// counting to the new period.
	write_timer(dev, TxCCR(0), 8);
	step_smclk(dev, 4);
	assert(check_noirq(dev));
	assert(read_timer(dev, TxR) == 8);

	// Compare interrupt should happen at new period TAR=8
	step_smclk(dev, 1);
	assert(read_timer(dev, TxR) == 0);
	assert(check_irq0(dev));
	assert(read_timer(dev, TxCCTL(0)) & CCIFG);
	ack_irq0(dev);
	assert_not(read_timer(dev, TxCCTL(0)) & CCIFG);
	assert(read_timer(dev, TxCTL) & TAIFG);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TAIV_TAIFG);
	assert(check_noirq(dev));
	assert_not(read_timer(dev, TxCTL) & TAIFG);
}

static void test_timer_a_updown_change_period()
{
	dev = create_timer("");

	/* Up/Down mode, SMCLK, enable interrupt, clear */
	write_timer(dev, TxCTL, MC1 | MC0 | TASSEL1 | TACLR | TAIE);
	write_timer(dev, TxCCTL(0), CCIE);
	write_timer(dev, TxCCR(0), 10);
	step_smclk(dev, 8);

	// While counting up, changing period to less than current
	// count will change the counting direction to down.
	assert(read_timer(dev, TxR) == 8);
	write_timer(dev, TxCCR(0), 5);
	assert(read_timer(dev, TxR) == 8);
	step_smclk(dev, 2);
	assert(read_timer(dev, TxR) == 6);
	assert(check_noirq(dev));

	// While counting down, Changing period to greater that
	// current count will continue counting to 0.
	write_timer(dev, TxCCR(0), 8);
	step_smclk(dev, 6);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TAIV_TAIFG);
	assert(check_noirq(dev));
	assert(read_timer(dev, TxR) == 0);

	// Then count up to TACCR[0] and compare interrupt should happen.
	step_smclk(dev, 8);
	assert(check_noirq(dev));
	assert(read_timer(dev, TxR) == 8);
	step_smclk(dev, 1);
	assert(check_irq0(dev));
	assert(read_timer(dev, TxCCTL(0)) & CCIFG);
	ack_irq0(dev);
	assert(check_noirq(dev));
	assert(read_timer(dev, TxR) == 7);
}

static void test_timer_divider()
{
	dev = create_timer("");

	/* Continuous mode, SMCLK/1, clear */
	write_timer(dev, TxCTL, MC1 | TASSEL1 | TACLR);
	assert(read_timer(dev, TxR) == 0);
	step_smclk(dev, 80);
	assert(read_timer(dev, TxR) == 80);
	step_aclk(dev, 100);
	assert(read_timer(dev, TxR) == 80);

	/* Continuous mode, SMCLK/2, clear */
	write_timer(dev, TxCTL, MC1 | TASSEL1 | ID0 | TACLR);
	assert(read_timer(dev, TxR) == 0);
	step_smclk(dev, 80);
	assert(read_timer(dev, TxR) == 40);
	step_aclk(dev, 100);
	assert(read_timer(dev, TxR) == 40);

	/* Continuous mode, SMCLK/4, clear */
	write_timer(dev, TxCTL, MC1 | TASSEL1 | ID1 | TACLR);
	assert(read_timer(dev, TxR) == 0);
	step_smclk(dev, 80);
	assert(read_timer(dev, TxR) == 20);
	step_aclk(dev, 100);
	assert(read_timer(dev, TxR) == 20);

	/* Continuous mode, SMCLK/2, clear */
	write_timer(dev, TxCTL, MC1 | TASSEL1 | ID1 | ID0 | TACLR);
	assert(read_timer(dev, TxR) == 0);
	step_smclk(dev, 80);
	assert(read_timer(dev, TxR) == 10);
	step_aclk(dev, 100);
	assert(read_timer(dev, TxR) == 10);

	/* Continuous mode, ACLK/1, clear */
	write_timer(dev, TxCTL, MC1 | TASSEL0 | TACLR);
	assert(read_timer(dev, TxR) == 0);
	step_aclk(dev, 80);
	assert(read_timer(dev, TxR) == 80);
	step_smclk(dev, 100);
	assert(read_timer(dev, TxR) == 80);

	/* Continuous mode, ACLK/2, clear */
	write_timer(dev, TxCTL, MC1 | TASSEL0 | ID0 | TACLR);
	assert(read_timer(dev, TxR) == 0);
	step_aclk(dev, 80);
	assert(read_timer(dev, TxR) == 40);
	step_smclk(dev, 100);
	assert(read_timer(dev, TxR) == 40);

	/* Continuous mode, ACLK/4, clear */
	write_timer(dev, TxCTL, MC1 | TASSEL0 | ID1 | TACLR);
	assert(read_timer(dev, TxR) == 0);
	step_aclk(dev, 80);
	assert(read_timer(dev, TxR) == 20);
	step_smclk(dev, 100);
	assert(read_timer(dev, TxR) == 20);

	/* Continuous mode, ACLK/2, clear */
	write_timer(dev, TxCTL, MC1 | TASSEL0 | ID1 | ID0 | TACLR);
	assert(read_timer(dev, TxR) == 0);
	step_aclk(dev, 80);
	assert(read_timer(dev, TxR) == 10);
	step_smclk(dev, 100);
	assert(read_timer(dev, TxR) == 10);
}

static void test_timer_capture_by_software()
{
	dev = create_timer("");

	/* Continuous mode, ACLK, clear */
	write_timer(dev, TxCTL, MC1 | TASSEL0 | TACLR);

	/* Capture mode, input GND, both edge */
	write_timer(dev, TxCCTL(0), CAP | CCIS1 | CM1 | CM0);
	step_aclk(dev, 10);
	// Rising edge.
	write_timer(dev, TxCCTL(0), read_timer(dev, TxCCTL(0)) | CCIS0);
	assert(read_timer(dev, TxCCTL(0)) & CCI);
	assert_not(read_timer(dev, TxCCTL(0)) & COV);
	assert(read_timer(dev, TxCCTL(0)) & CCIFG);
	assert(read_timer(dev, TxCCR(0)) == 10);

	write_timer(dev, TxCCTL(0), read_timer(dev, TxCCTL(0)) & ~CCIFG);
	step_aclk(dev, 10);
	// Falling edge.
	write_timer(dev, TxCCTL(0), read_timer(dev, TxCCTL(0)) & ~CCIS0);
	assert_not(read_timer(dev, TxCCTL(0)) & CCI);
	assert_not(read_timer(dev, TxCCTL(0)) & COV);
	assert(read_timer(dev, TxCCTL(0)) & CCIFG);
	assert(read_timer(dev, TxCCR(0)) == 20);

	// Keep CCIFG on and capture causes COV */
	step_aclk(dev, 10);
	// Rising edge.
	write_timer(dev, TxCCTL(0), read_timer(dev, TxCCTL(0)) | CCIS0);
	assert(read_timer(dev, TxCCTL(0)) & CCI);
	assert(read_timer(dev, TxCCTL(0)) & COV);
	assert(read_timer(dev, TxCCTL(0)) & CCIFG);
	assert(read_timer(dev, TxCCR(0)) == 20);

	/* Capture mode, input GND, rising edge */
	write_timer(dev, TxCCTL(1), CAP | CCIS1 | CM0);
	// Rising edge.
	write_timer(dev, TxCCTL(1), read_timer(dev, TxCCTL(1)) | CCIS0);
	assert(read_timer(dev, TxCCTL(1)) & CCI);
	assert_not(read_timer(dev, TxCCTL(1)) & COV);
	assert(read_timer(dev, TxCCTL(1)) & CCIFG);
	assert(read_timer(dev, TxCCR(1)) == 30);

	write_timer(dev, TxCCTL(1), read_timer(dev, TxCCTL(1)) & ~CCIFG);
	step_aclk(dev, 10);
	// Falling edge.
	write_timer(dev, TxCCTL(1), read_timer(dev, TxCCTL(1)) & ~CCIS0);
	assert_not(read_timer(dev, TxCCTL(1)) & CCI);
	assert_not(read_timer(dev, TxCCTL(1)) & COV);
	assert_not(read_timer(dev, TxCCTL(1)) & CCIFG);
	assert(read_timer(dev, TxCCR(1)) == 30);

	/* Capture mode, input GND, falling edge */
	write_timer(dev, TxCCTL(2), CAP | CCIS1 | CM1);
	// Rising edge.
	write_timer(dev, TxCCTL(2), read_timer(dev, TxCCTL(2)) | CCIS0);
	assert(read_timer(dev, TxCCTL(2)) & CCI);
	assert_not(read_timer(dev, TxCCTL(2)) & COV);
	assert_not(read_timer(dev, TxCCTL(2)) & CCIFG);
	assert(read_timer(dev, TxCCR(2)) == 0);

	step_aclk(dev, 10);
	// Falling edge.
	write_timer(dev, TxCCTL(2), read_timer(dev, TxCCTL(2)) & ~CCIS0);
	assert_not(read_timer(dev, TxCCTL(2)) & CCI);
	assert_not(read_timer(dev, TxCCTL(2)) & COV);
	assert(read_timer(dev, TxCCTL(2)) & CCIFG);
	assert(read_timer(dev, TxCCR(2)) == 50);
}


static void test_timer_capture_by_signal()
{
	dev = create_timer("");

	/* Continuous mode, ACLK, clear */
	write_timer(dev, TxCTL, MC1 | TASSEL0 | TACLR);

	/* Capture mode, input CCIxA, both edge */
	write_timer(dev, TxCCTL(0), CAP | CM1 | CM0);
	step_aclk(dev, 10);
	// Rising edge.
	config_timer(dev, "set", "0 1");
	assert(read_timer(dev, TxCCTL(0)) & CCI);
	assert_not(read_timer(dev, TxCCTL(0)) & COV);
	assert(read_timer(dev, TxCCTL(0)) & CCIFG);
	assert(read_timer(dev, TxCCR(0)) == 10);

	write_timer(dev, TxCCTL(0), read_timer(dev, TxCCTL(0)) & ~CCIFG);
	step_aclk(dev, 10);
	// Falling edge.
	config_timer(dev, "set", "0 0");
	assert_not(read_timer(dev, TxCCTL(0)) & CCI);
	assert_not(read_timer(dev, TxCCTL(0)) & COV);
	assert(read_timer(dev, TxCCTL(0)) & CCIFG);
	assert(read_timer(dev, TxCCR(0)) == 20);

	// Keep CCIFG on and capture causes COV */
	step_aclk(dev, 10);
	// Rising edge.
	config_timer(dev, "set", "0 1");
	assert(read_timer(dev, TxCCTL(0)) & CCI);
	assert(read_timer(dev, TxCCTL(0)) & COV);
	assert(read_timer(dev, TxCCTL(0)) & CCIFG);
	assert(read_timer(dev, TxCCR(0)) == 20);

	/* Capture mode, input CCIxB, rising edge, enable interrupt */
	write_timer(dev, TxCCTL(1), CAP | CCIS0 | CM0 | CCIE);
	// Rising edge.
	config_timer(dev, "set", "1 1");
	assert(check_irq1(dev));
	assert(read_timer(dev, TxCCTL(1)) & CCI);
	assert(read_timer(dev, TxCCTL(1)) & CCIFG);
	assert_not(read_timer(dev, TxCCTL(1)) & COV);
	assert(read_iv(dev) == TxIV_TxIFG(1));
	assert(check_noirq(dev));
	assert_not(read_timer(dev, TxCCTL(1)) & CCIFG);
	assert(read_timer(dev, TxCCR(1)) == 30);

	write_timer(dev, TxCCTL(1), read_timer(dev, TxCCTL(1)) & ~CCIFG);
	step_aclk(dev, 10);
	// Falling edge.
	config_timer(dev, "set", "1 0");
	assert(check_noirq(dev));
	assert_not(read_timer(dev, TxCCTL(1)) & CCI);
	assert_not(read_timer(dev, TxCCTL(1)) & COV);
	assert_not(read_timer(dev, TxCCTL(1)) & CCIFG);
	assert(read_timer(dev, TxCCR(1)) == 30);

	/* Capture mode, input CCIxB falling edge, enable interrupt */
	write_timer(dev, TxCCTL(2), CAP | CCIS0 | CM1 | CCIE);
	// Rising edge.
	config_timer(dev, "set", "2 1");
	assert(check_noirq(dev));
	assert(read_timer(dev, TxCCTL(2)) & CCI);
	assert_not(read_timer(dev, TxCCTL(2)) & COV);
	assert_not(read_timer(dev, TxCCTL(2)) & CCIFG);
	assert(read_timer(dev, TxCCR(2)) == 0);

	step_aclk(dev, 10);
	// Falling edge.
	config_timer(dev, "set", "2 0");
	assert(check_irq1(dev));
	assert_not(read_timer(dev, TxCCTL(2)) & CCI);
	assert_not(read_timer(dev, TxCCTL(2)) & COV);
	assert(read_timer(dev, TxCCTL(2)) & CCIFG);
	assert(read_iv(dev) == TxIV_TxIFG(2));
	assert(check_noirq(dev));
	assert(read_timer(dev, TxCCR(2)) == 50);
}

static void test_timer_a_compare()
{
	dev = create_timer("");

	/* Continuous mode, ACLK, interrupt enable, clear */
	write_timer(dev, TxCTL, MC1 | TASSEL0 | TACLR | TAIE);
	write_timer(dev, TxR, 0xffff);
	/* Compare mode, enable interrupts */
	write_timer(dev, TxCCTL(0), CCIE);
	write_timer(dev, TxCCR(0), 200);
	write_timer(dev, TxCCTL(1), CCIE);
	write_timer(dev, TxCCR(1), 100);
	write_timer(dev, TxCCTL(2), CCIE);
	write_timer(dev, TxCCR(2), 200);

	// Timer_A overflow interrupt should happen.
	step_aclk(dev, 1);
	assert(check_irq1(dev));
	// Timer_A overflow vector is 0x0a.
	assert(TAIV_TAIFG == 0x0a);
	assert(read_iv(dev) == TAIV_TAIFG);
	assert(check_noirq(dev));
	assert(read_timer(dev, TxR) == 0);

	step_aclk(dev, 100);
	assert(check_noirq(dev));
	assert_not(read_timer(dev, TxCCTL(1)) & SCCI);
	// Set CCI of CCTL[1] to 1.
	config_timer(dev, "set", "1 1");
	// Timer_A comparator interrupt should happen.
	step_aclk(dev, 1);
	assert(check_irq1(dev));
	assert(read_timer(dev, TxCCTL(1)) & CCIFG);
	assert(read_iv(dev) == TxIV_TxIFG(1));
	assert(check_noirq(dev));
	assert_not(read_timer(dev, TxCCTL(1)) & CCIFG);
	assert(read_timer(dev, TxCCTL(1)) & SCCI);

	// Timer_A comparator interrupts should happen.
	step_aclk(dev, 100);
	assert(check_irq0(dev));
	assert(read_timer(dev, TxCCTL(0)) & CCIFG);
	ack_irq0(dev);
	assert_not(check_noirq(dev));
	assert_not(read_timer(dev, TxCCTL(0)) & CCIFG);
	// Lower priority interrupt.
	assert(check_irq1(dev));
	assert(read_timer(dev, TxCCTL(2)) & CCIFG);
	assert(read_iv(dev) == TxIV_TxIFG(2));
	assert(check_noirq(dev));
	assert_not(read_timer(dev, TxCCTL(2)) & CCIFG);

	// Set CCI of CCTL[1] to 0.
	config_timer(dev, "set", "1 0");
	write_timer(dev, TxCCR(1), 300);
	step_aclk(dev, 100);
	// Timer_A comparator interrupt should happen.
	assert(check_irq1(dev));
	assert(read_timer(dev, TxCCTL(1)) & CCIFG);
	assert(read_iv(dev) == TxIV_TxIFG(1));
	assert(check_noirq(dev));
	assert_not(read_timer(dev, TxCCTL(1)) & CCIFG);
	assert_not(read_timer(dev, TxCCTL(1)) & SCCI);
}

static void test_timer_b_compare()
{
	dev = create_timer("");
	config_timer(dev, "type", "B");

	/* Continuous mode, ACLK, interrupt enable, clear */
	write_timer(dev, TxCTL, MC1 | TASSEL0 | TACLR | TAIE);
	write_timer(dev, TxR, 0xffff);
	/* Compare mode, enable interrupts */
	write_timer(dev, TxCCTL(0), CCIE);
	write_timer(dev, TxCCR(0), 200);
	write_timer(dev, TxCCTL(1), CCIE);
	write_timer(dev, TxCCR(1), 100);
	write_timer(dev, TxCCTL(2), CCIE);
	write_timer(dev, TxCCR(2), 200);

	// Timer_B overflow interrupt should happen.
	step_aclk(dev, 1);
	assert(check_irq1(dev));
	// Timer_B overflow vector is 0x0e.
	assert(TBIV_TBIFG == 0x0e);
	assert(read_iv(dev) == TBIV_TBIFG);
	assert(check_noirq(dev));
	assert(read_timer(dev, TxR) == 0);

	step_aclk(dev, 100);
	assert(check_noirq(dev));
	// Set CCI of CCTL[1] to 1.
	config_timer(dev, "set", "1 1");
	// Timer_B comparator interrupt should happen.
	step_aclk(dev, 1);
	assert(check_irq1(dev));
	assert(read_timer(dev, TxCCTL(1)) & CCIFG);
	assert(read_iv(dev) == TxIV_TxIFG(1));
	assert(check_noirq(dev));
	assert_not(read_timer(dev, TxCCTL(1)) & CCIFG);

	// Timer_B comparator interrupts should happen.
	step_aclk(dev, 100);
	assert(check_irq0(dev));
	assert(read_timer(dev, TxCCTL(0)) & CCIFG);
	ack_irq0(dev);
	assert_not(check_noirq(dev));
	assert_not(read_timer(dev, TxCCTL(0)) & CCIFG);
	// Lower priority interrupt.
	assert(check_irq1(dev));
	assert(read_timer(dev, TxCCTL(2)) & CCIFG);
	assert(read_iv(dev) == TxIV_TxIFG(2));
	assert(check_noirq(dev));
	assert_not(read_timer(dev, TxCCTL(2)) & CCIFG);

	// Set CCI of CCTL[1] to 0.
	config_timer(dev, "set", "1 0");
	write_timer(dev, TxCCR(1), 300);
	step_aclk(dev, 100);
	// Timer_B comparator interrupt should happen.
	assert(check_irq1(dev));
	assert(read_timer(dev, TxCCTL(1)) & CCIFG);
	assert(read_iv(dev) == TxIV_TxIFG(1));
	assert(check_noirq(dev));
	assert_not(read_timer(dev, TxCCTL(1)) & CCIFG);
}

static void test_timer_capture()
{
	dev = create_timer("");

	/* Continuous mode, ACLK, clear */
	write_timer(dev, TxCTL, MC1 | TASSEL0 | TACLR);
	/* Capture mode, enable interrupts */
}

static void test_timer_b_length_8()
{
	dev = create_timer("");
	config_timer(dev, "type", "B");

	/* Continuous 8 bit, SMCLK, interrupt enable, clear */
	write_timer(dev, TxCTL, MC1 | CNTL1 | CNTL0 | TACLR | TAIE | TASSEL1);
	write_timer(dev, TxR, 0x00ff);
	step_smclk(dev, 2);

	// Timer B can configured as 8 bit length.
	assert(check_irq1(dev));
	assert(read_iv(dev) == TBIV_TBIFG);
	assert(read_timer(dev, TxR) == 1);
}

static void test_timer_b_length_10()
{
	dev = create_timer("");
	config_timer(dev, "type", "B");

	/* Continuous 10 bit, ACLK, interrupt enable, clear */
	write_timer(dev, TxCTL, MC1 | CNTL1 | TACLR | TAIE | TASSEL0);
	write_timer(dev, TxR, 0x03ff);
	step_aclk(dev, 3);

	// Timer B can configured as 10 bit length.
	assert(check_irq1(dev));
	assert(read_iv(dev) == TBIV_TBIFG);
	assert(read_timer(dev, TxR) == 2);
}

static void test_timer_b_length_12()
{
	dev = create_timer("");
	config_timer(dev, "type", "B");

	/* Continuous 12 bit, SMCLK, interrupt enable, clear */
	write_timer(dev, TxCTL,	 MC1 | CNTL0 | TACLR | TAIE | TASSEL1);
	write_timer(dev, TxR, 0x0fff);
	step_smclk(dev, 4);

	// Timer B can configured as 12 bit length.
	assert(check_irq1(dev));
	assert(read_iv(dev) == TBIV_TBIFG);
	assert(read_timer(dev, TxR) == 3);
}

static void test_timer_b_length_16()
{
	dev = create_timer("");
	config_timer(dev, "type", "B");

	/* Continuous 16 bit, SMCLK, interrupt enable, clear */
	write_timer(dev, TxCTL, MC1 | TACLR | TAIE | TASSEL1);
	write_timer(dev, TxR, 0xffff);
	step_smclk(dev, 5);

	// Timer B can configured as 16 bit length.
	assert(check_irq1(dev));
	assert(read_iv(dev) == TBIV_TBIFG);
	assert(read_timer(dev, TxR) == 4);
}

static void test_timer_b_compare_latch_0_up()
{
	dev = create_timer("");
	config_timer(dev, "type", "B");

	/* Up 16 bit, SMCLK, clear */
	write_timer(dev, TxCTL, MC0 | TACLR | TASSEL1);

	write_timer(dev, TxCCR(0), 2);
	write_timer(dev, TxCCR(2), 5);
	// When CLLD=0, new data is transferred from TBCCRx to TBCLx immediately
	// when TBCCRx is written to.
	write_timer(dev, TxCCTL(0), CCIE);
	write_timer(dev, TxCCTL(2), CCIE);
	step_smclk(dev, 2);
	assert(check_noirq(dev));

	// TBCCR[0] is immediately transferred to TBCL[0].
	write_timer(dev, TxCCR(0), 10);

	// Because now TBCL[0] is 10, no overflow interrupt should happen at TBR=2
	step_smclk(dev, 1);
	assert(read_timer(dev, TxR) == 3);
	assert(check_noirq(dev));

	// Because TBCL[2] is 5, comparator interrupt should happen at TBR=5.
	step_smclk(dev, 3);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(2));
	assert(check_noirq(dev));

	// Because TBCL[2] is 8, comparator interrupt should happen again at TBR=8.
	write_timer(dev, TxCCR(2), 8);
	step_smclk(dev, 3);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(2));
	assert(check_noirq(dev));

	// Because TBCL[0] is 10, overflow interrupt should happen again at TBR=10.
	step_smclk(dev, 2);
	assert(check_irq0(dev));
	ack_irq0(dev);
	assert(check_noirq(dev));
	assert(read_timer(dev, TxR) == 0);
}

static void test_timer_b_compare_latch_1_up()
{
	dev = create_timer("3");
	config_timer(dev, "type", "B");

	/* Up 16 bit, SMCLK, clear */
	write_timer(dev, TxCTL, MC0 | TACLR | TASSEL1);

	write_timer(dev, TxCCR(0), 5);
	write_timer(dev, TxCCR(1), 2);
	// When CLLD=1, new data is transferred from TBCCRx to TBCLx when TBR
	// counts to 0.
	write_timer(dev, TxCCTL(0), CLLD0 | CCIE);
	write_timer(dev, TxCCTL(1), CLLD0 | CCIE);
	step_smclk(dev, 2);
	assert(check_noirq(dev));

	// TBCCR[0] will be transferred to TBCL[0] when next TBR=0.
	write_timer(dev, TxCCR(0), 10);
	// TBCCR[1] will be transferred to TBCL[1] when next TBR=0.
	write_timer(dev, TxCCR(1), 2);

	// Because TBCL[1] keeps 2, compare interrupt should happen at TBR=2.
	step_smclk(dev, 1);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(1));
	assert(check_noirq(dev));

	// Because TBCL[0] keeps 5, compare interrupt should happen at TBR=5, then TBR=0.
	step_smclk(dev, 3);
	assert(check_irq0(dev));
	ack_irq0(dev);
	assert(check_noirq(dev));
	assert(read_iv(dev) == TxIV_TxIFG(0));

	// Now TBCL[1] becomes 2, compare interrupt should happen again at TBR=2.
	step_smclk(dev, 3);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(1));
	assert(check_noirq(dev));

	// Now TBCL[0] becomes 10, no compare interrupt should happen at TBR=5.
	step_smclk(dev, 7);
	assert(check_noirq(dev));

	// Then compare interrupt should happen at TBR=10, then TBR=0.
	step_smclk(dev, 1);
	assert(check_irq0(dev));
	ack_irq0(dev);
	assert(check_noirq(dev));
	assert(read_iv(dev) == TxIV_TxIFG(0));
	assert(read_timer(dev, TxR) == 0);
}

static void test_timer_b_compare_latch_2_continuous()
{
	dev = create_timer("7");
	config_timer(dev, "type", "B");

	/* Continuous 8 bit, SMCLK, clear */
	write_timer(dev, TxCTL, MC1 | CNTL1 | CNTL0 | TACLR | TASSEL1);
	write_timer(dev, TxR, 0x00fe);

	write_timer(dev, TxCCR(6), 2);
	// When CLLD=2, new data is transferred from TBCCRx to TBCLx when TBR
	// counts to 0 for continuous mode.
	write_timer(dev, TxCCTL(6), CLLD1 | CCIE);

	// TBCCR[6] will be transferred to TBCL[6] when next TBR=0.
	write_timer(dev, TxCCR(6), 5);
	step_smclk(dev, 2);
	assert(read_timer(dev, TxR) == 0);

	// Now TBCL[6] becomes 5, compare interrupt should not happen at TBR=2.
	step_smclk(dev, 3);
	assert(check_noirq(dev));

	// Because TBCL[6] is 5, compare interrupt should happen at TBR=5.
	step_smclk(dev, 3);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(6));
	assert(check_noirq(dev));

	// Because TBCCR[6] will be transferred to TBCL[6] when next TBR=0,
	// no compare interrupt should happen at TBR=5.
	write_timer(dev, TxCCR(6), 10);
	step_smclk(dev, 0x100);
	assert(check_noirq(dev));
	assert(read_timer(dev, TxR) > 5);

	// Now TBCL[6] becomes 10, compare interrupt should happen.
	step_smclk(dev, 5);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(6));
	assert(check_noirq(dev));
}

static void test_timer_b_compare_latch_2_up()
{
	dev = create_timer("5");
	config_timer(dev, "type", "B");

	/* Up 16 bit, SMCLK, clear */
	write_timer(dev, TxCTL, MC0 | TACLR | TASSEL1);

	write_timer(dev, TxCCR(0), 5);
	write_timer(dev, TxCCR(4), 2);
	// When CLLD=2, new data is transferred from TBCCRx to TBCLx when TBR
	// counts to 0.
	write_timer(dev, TxCCTL(0), CLLD0 | CCIE);
	write_timer(dev, TxCCTL(4), CLLD0 | CCIE);
	step_smclk(dev, 2);
	assert(check_noirq(dev));

	// TBCCR[0] will be transferred to TBCL[0] when next TBR=0.
	write_timer(dev, TxCCR(0), 10);
	// TBCCR[4] will be transferred to TBCL[4] when next TBR=0.
	write_timer(dev, TxCCR(4), 2);

	// Because TBCL[4] keeps 2, compare interrupt should happen at TBR=2.
	step_smclk(dev, 1);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(4));
	assert(check_noirq(dev));

	// Because TBCL[0] keeps 5, compare interrupt should happen at TBR=5, then TBR=0.
	step_smclk(dev, 3);
	assert(check_irq0(dev));
	ack_irq0(dev);
	assert(check_noirq(dev));
	assert(read_iv(dev) == TxIV_TxIFG(0));

	// Now TBCL[4] becomes 2, compare interrupt should happen again at TBR=2.
	step_smclk(dev, 3);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(4));
	assert(check_noirq(dev));

	// Now TBCL[0] becomes 10, no compare interrupt should happen at TBR=5.
	step_smclk(dev, 7);
	assert(check_noirq(dev));

	// Then compare interrupt should happen at TBR=10, then TBR=0.
	step_smclk(dev, 1);
	assert(check_irq0(dev));
	ack_irq0(dev);
	assert(check_noirq(dev));
	assert(read_iv(dev) == TxIV_TxIFG(0));
	assert(read_timer(dev, TxR) == 0);
}

static void test_timer_b_compare_latch_2_updown()
{
	dev = create_timer("7");
	config_timer(dev, "type", "B");

	/* Up/Down 16 bit, SMCLK, interrupt enable, clear */
	write_timer(dev, TxCTL, MC1 | MC0 | TACLR | TAIE | TASSEL1);

	write_timer(dev, TxCCR(0), 10);
	write_timer(dev, TxCCTL(0), CCIE);
	write_timer(dev, TxCCR(5), 5);
	// When CLLD=2, new data is transferred from TBCCRx to TBCLx when TBR
	// counts to the old TBCL0 value or to 0 for up/down mode.
	write_timer(dev, TxCCTL(5), CLLD1 | CCIE);
	step_smclk(dev, 4);
	assert(check_noirq(dev));
	assert(read_timer(dev, TxR) == 4);

	// TBCCR[5] will be transferred to TBCL[5] when next TBR=5.
	write_timer(dev, TxCCR(5), 8);

	// Because TBCL[0] is 5, compare interrupt should happen at TBR=TBCL[5]=5.
	step_smclk(dev, 2);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(5));
	assert(check_noirq(dev));

	// Now TBCL[5] becomes 8, compare interrupt should happen at TBR=8.
	step_smclk(dev, 3);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(5));
	assert(check_noirq(dev));

	// Because TBCL[0]=10, compare interrupt should happen at TBR=10.
	step_smclk(dev, 2);
	assert(check_irq0(dev));
	ack_irq0(dev);
	assert(check_noirq(dev));
	assert(read_iv(dev) == TxIV_TxIFG(0));
	assert(read_timer(dev, TxR) == 9);

	// Because TBCL[5] is still 8, compare interrupt should happen again at TBR=8.
	step_smclk(dev, 2);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(5));
	assert(check_noirq(dev));

	// Because TBCL[5] is still 8, no compare interrupt should happen.
	step_smclk(dev, 6);
	assert(read_timer(dev, TxR) == 1);

	// TBCCR[5] will be transferred to TBCL[5] when next TBR=0.
	write_timer(dev, TxCCR(5), 2);

	// Compare interrupt should happen at TBR=0.
	step_smclk(dev, 1);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TBIV_TBIFG);
	assert(check_noirq(dev));
	assert(read_iv(dev) == TxIV_TxIFG(0));
	assert(read_timer(dev, TxR) == 0);

	// Now TBCL[5] becomes 2, compare interrupt should happen at TBR=2.
	step_smclk(dev, 3);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(5));
	assert(check_noirq(dev));
	assert(read_timer(dev, TxR) == 3);
}

static void test_timer_b_compare_latch_3_up()
{
	dev = create_timer("5");
	config_timer(dev, "type", "B");

	/* Up 16 bit, SMCLK, clear */
	write_timer(dev, TxCTL, MC0 | TACLR | TASSEL1);

	write_timer(dev, TxCCR(0), 10);
	write_timer(dev, TxCCTL(0), CCIE);
	write_timer(dev, TxCCR(2), 5);
	// When CLLD=3, new data is transferred from TBCCRx to TBCLx when TBR
	// counts to the old TBCL0 value.
	write_timer(dev, TxCCTL(2), CLLD1 | CLLD0 | CCIE);
	step_smclk(dev, 4);
	assert(check_noirq(dev));
	assert(read_timer(dev, TxR) == 4);

	// TBCCR[2] will be transferred to TBCL[2] when next TBR=5.
	write_timer(dev, TxCCR(2), 8);

	// Because TBCL[0] is 5, compare interrupt should happen at TBR=TBCL[2]=5.
	step_smclk(dev, 2);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(2));
	assert(check_noirq(dev));

	// TBCCR[2] will be transferred to TBCL[2] when next TBR=8.
	write_timer(dev, TxCCR(2), 2);

	// Now TBCL[2] becomes 8, compare interrupt should happen at TBR=8.
	step_smclk(dev, 3);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(2));
	assert(check_noirq(dev));

	// Because TBCL[0] is 10, compare interrupt should happen at TBR=10.
	step_smclk(dev, 2);
	assert(check_irq0(dev));
	ack_irq0(dev);
	assert(check_noirq(dev));
	assert(read_iv(dev) == TxIV_TxIFG(0));
	assert(read_timer(dev, TxR) == 0);

	// Because TBCL[2] is 2, compare interrupt should happen again at TBR=2.
	step_smclk(dev, 3);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(2));
	assert(check_noirq(dev));
}

static void test_timer_b_grouping_1()
{
	dev = create_timer("7");
	config_timer(dev, "type", "B");

	/* Continuous mode, SMCLK, pair grouping */
	write_timer(dev, TxCTL, MC1 | TASSEL1 | TBCLGRP0);
	write_timer(dev, TxCCR(0), 2);
	write_timer(dev, TxCCR(1), 4);
	write_timer(dev, TxCCR(2), 5);
	write_timer(dev, TxCCR(3), 6);
	write_timer(dev, TxCCR(4), 7);
	write_timer(dev, TxCCR(5), 8);
	write_timer(dev, TxCCR(6), 9);
	/* Load TBCL when TBR reaches old TBCL value, compare interrupt */
	write_timer(dev, TxCCTL(0), CLLD1 | CLLD0 | CCIE);
	write_timer(dev, TxCCR(0), 10);
	// Both CCR[1] and CCR[2] are set.
	write_timer(dev, TxCCTL(1), CLLD1 | CLLD0 | CCIE);
	write_timer(dev, TxCCTL(2), CCIE);
	write_timer(dev, TxCCR(1), 12);
	write_timer(dev, TxCCR(2), 13);
	// CCR[3] is set but CCR[4] keep unset.
	write_timer(dev, TxCCTL(3), CLLD1 | CLLD0 | CCIE);
	write_timer(dev, TxCCTL(4), CCIE);
	write_timer(dev, TxCCR(3), 14);
	// Both CCR[5] and CCR[6] are set, but CCTL[5] has CCLD=0.
	write_timer(dev, TxCCTL(5), CCIE);
	write_timer(dev, TxCCTL(6), CLLD1 | CLLD0 | CCIE);
	write_timer(dev, TxCCR(5), 16);
	write_timer(dev, TxCCR(6), 17);

	step_smclk(dev, 2);

	// TBCL[0] becomes 10.
	assert(read_timer(dev, TxR) == 2);
	step_smclk(dev, 1);
	assert(check_irq0(dev));
	ack_irq0(dev);
	assert(check_noirq(dev));

	// TBCL[1] and TBCL[2] becomes 12 and 13.
	assert(read_timer(dev, TxR) == 3);
	step_smclk(dev, 2);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(1));
	assert(check_noirq(dev));

	// TBCL[3] and TBCL[4] keep previous value.
	// because TBCCR[4] has no valid value set.
	assert(read_timer(dev, TxR) == 5);
	step_smclk(dev, 2);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(3));
	assert(check_noirq(dev));
	step_smclk(dev, 1);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(4));
	assert(check_noirq(dev));

	// TBCL[5] and TBCL[6] is already 16 and 17 because TBCTL[5]
	// has CLLD=0 set, thus no interrupts at TBR=8,9.
	assert(read_timer(dev, TxR) == 8);
	step_smclk(dev, 1);
	assert(check_noirq(dev));
	step_smclk(dev, 1);
	assert(check_noirq(dev));

	// TBCL[0] will match.
	step_smclk(dev, 1);
	assert(check_irq0(dev));
	assert(read_timer(dev, TxCCTL(0)) & CCIFG);
	ack_irq0(dev);
	assert(check_noirq(dev));
	assert_not(read_timer(dev, TxCCTL(0)) & CCIFG);

	// TBCL[1]will match.
	step_smclk(dev, 2);
	assert(check_irq1(dev));
	assert(read_timer(dev, TxCCTL(1)) & CCIFG);
	assert(read_iv(dev) == TxIV_TxIFG(1));
	assert(check_noirq(dev));
	assert_not(read_timer(dev, TxCCTL(1)) & CCIFG);
	// Because TBCL[2] is also loaded as group with TBCL[1].
	step_smclk(dev, 1);
	assert(check_irq1(dev));
	assert(read_timer(dev, TxCCTL(2)) & CCIFG);
	assert(read_iv(dev) == TxIV_TxIFG(2));
	assert(check_noirq(dev));
	assert_not(read_timer(dev, TxCCTL(2)) & CCIFG);

	// TBCL[3] and TBCL[4] didn't updated since TBCCR[4] has
	// no value set, thus no compare interrupts at TBR=14,15.
	assert(read_timer(dev, TxR) == 14);
	step_smclk(dev, 1);
	assert(check_noirq(dev));
	step_smclk(dev, 1);
	assert(check_noirq(dev));

	// TBCL[5] and TBCL[6] have set to 16 and 17, thus two compare
	// interrupts at TBR=16,17
	step_smclk(dev, 1);
	assert(check_irq1(dev));
	assert(read_timer(dev, TxCCTL(5)) & CCIFG);
	assert(read_iv(dev) == TxIV_TxIFG(5));
	assert(check_noirq(dev));
	assert_not(read_timer(dev, TxCCTL(5)) & CCIFG);
	step_smclk(dev, 1);
	assert(check_irq1(dev));
	assert(read_timer(dev, TxCCTL(6)) & CCIFG);
	assert(read_iv(dev) == TxIV_TxIFG(6));
	assert(check_noirq(dev));
	assert_not(read_timer(dev, TxCCTL(6)) & CCIFG);
}

static void test_timer_b_grouping_2()
{
	dev = create_timer("7");
	config_timer(dev, "type", "B");

	/* Continuous mode, SMCLK, triplet grouping */
	write_timer(dev, TxCTL, MC1 | TASSEL1 | TBCLGRP1);
	write_timer(dev, TxCCR(0), 2);
	write_timer(dev, TxCCR(1), 4);
	write_timer(dev, TxCCR(2), 5);
	write_timer(dev, TxCCR(3), 6);
	write_timer(dev, TxCCR(4), 7);
	write_timer(dev, TxCCR(5), 8);
	write_timer(dev, TxCCR(6), 9);
	/* Load TBCL when TBR reaches old TBCL value, compare interrupt */
	write_timer(dev, TxCCTL(0), CLLD1 | CLLD0 | CCIE);
	write_timer(dev, TxCCR(0), 10);
	// All CCR[1], CCR[2], and CCR[3] are set.
	write_timer(dev, TxCCTL(1), CLLD1 | CLLD0 | CCIE);
	write_timer(dev, TxCCTL(2), CCIE);
	write_timer(dev, TxCCTL(3), CCIE);
	write_timer(dev, TxCCR(1), 12);
	write_timer(dev, TxCCR(2), 6);
	write_timer(dev, TxCCR(3), 5);
	// CCR[4] and CCR[6] are set but CCR[5] keep unset.
	write_timer(dev, TxCCTL(4), CLLD1 | CLLD0 | CCIE);
	write_timer(dev, TxCCTL(5), CCIE);
	write_timer(dev, TxCCTL(6), CCIE);
	write_timer(dev, TxCCR(4), 14);
	write_timer(dev, TxCCR(6), 8);

	step_smclk(dev, 2);

	// TBCL[0] becomes 10.
	assert(read_timer(dev, TxR) == 2);
	step_smclk(dev, 1);
	assert(check_irq0(dev));
	ack_irq0(dev);
	assert(check_noirq(dev));

	// TBCL[1], TBCL[2], and TBCL[3] becomes 12, 6, and 5.
	assert(read_timer(dev, TxR) == 3);
	step_smclk(dev, 2);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(1));
	assert(check_noirq(dev));
	assert(read_timer(dev, TxR) == 5);
	step_smclk(dev, 1);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(3));
	assert(check_noirq(dev));
	step_smclk(dev, 1);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(2));
	assert(check_noirq(dev));

	
	// TBCL[4], TBCL[5] and TBC[6] keep previous value.
	// because TBCCR[5] has no valid value set.
	assert(read_timer(dev, TxR) == 7);
	step_smclk(dev, 1);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(4));
	assert(check_noirq(dev));
	step_smclk(dev, 1);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(5));
	assert(check_noirq(dev));
	step_smclk(dev, 1);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(6));
	assert(check_noirq(dev));

	// TBCL[0] is updated to 10 and TBCL[1] has 12.
	assert(read_timer(dev, TxR) == 10);
	step_smclk(dev, 1);
	assert(check_irq0(dev));
	ack_irq0(dev);
	assert(check_noirq(dev));
	step_smclk(dev, 2);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(1));
	assert(check_noirq(dev));
}

static void test_timer_b_grouping_3()
{
	dev = create_timer("7");
	config_timer(dev, "type", "B");

	/* Continuous mode, SMCLK, triplet grouping */
	write_timer(dev, TxCTL, MC1 | TASSEL1 | TBCLGRP1 | TBCLGRP0);
	write_timer(dev, TxCCR(0), 3);
	write_timer(dev, TxCCR(1), 2);
	write_timer(dev, TxCCR(2), 4);
	write_timer(dev, TxCCR(3), 5);
	write_timer(dev, TxCCR(4), 6);
	write_timer(dev, TxCCR(5), 7);
	write_timer(dev, TxCCR(6), 8);
	/* Load TBCL when TBR reaches old TBCL value, compare interrupt */
	write_timer(dev, TxCCTL(0), CCIE);
	write_timer(dev, TxCCTL(1), CLLD1 | CLLD0 | CCIE);
	write_timer(dev, TxCCTL(6), CCIE);
	write_timer(dev, TxCCR(0), 9);
	write_timer(dev, TxCCR(1), 10);
	write_timer(dev, TxCCR(2), 11);
	write_timer(dev, TxCCR(3), 12);
	write_timer(dev, TxCCR(4), 13);
	write_timer(dev, TxCCR(5), 14);
	write_timer(dev, TxCCR(6), 15);

	step_smclk(dev, 2);

	// TBCL[1] becomes 10.
	assert(read_timer(dev, TxR) == 2);
	step_smclk(dev, 1);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(1));
	assert(check_noirq(dev));

	// The least valued TBCL is TBCL[9].
	step_smclk(dev, 6);
	assert(read_timer(dev, TxR) == 9);
	assert(check_noirq(dev));
	step_smclk(dev, 1);
	assert(check_irq0(dev));
	ack_irq0(dev);
	step_smclk(dev, 1);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(1));
	step_smclk(dev, 4);
	assert(check_noirq(dev));
	assert(read_timer(dev, TxCCTL(2)) & CCIFG);
	assert(read_timer(dev, TxCCTL(3)) & CCIFG);
	assert(read_timer(dev, TxCCTL(4)) & CCIFG);
	assert(read_timer(dev, TxCCTL(5)) & CCIFG);
	step_smclk(dev, 1);
	assert(check_irq1(dev));
	assert(read_iv(dev) == TxIV_TxIFG(6));
	assert(check_noirq(dev));
}


/*
 * Test runner.
 */

static void run_test(void (*test)(), const char *test_name)
{
	set_up();

	test();
	printf("  PASS %s\n", test_name);

	tear_down();
}

#define RUN_TEST(test) run_test(test, #test)

int main(int argc, char **argv)
{
	set_up_globals();

	RUN_TEST(test_create_no_option);
	RUN_TEST(test_create_with_size_7);
	RUN_TEST(test_create_with_size_2);
	RUN_TEST(test_create_with_size_8);
	RUN_TEST(test_create_with_size_1);
	RUN_TEST(test_config_type_default);
	RUN_TEST(test_config_type_A);
	RUN_TEST(test_config_type_B);
	RUN_TEST(test_config_type_bad);
	RUN_TEST(test_config_type_empty);
	RUN_TEST(test_address_space);
	RUN_TEST(test_timer_continuous);
	RUN_TEST(test_timer_stop);
	RUN_TEST(test_timer_up_stop);
	RUN_TEST(test_timer_updown_stop);
	RUN_TEST(test_timer_a_up);
	RUN_TEST(test_timer_a_up_change_period);
	RUN_TEST(test_timer_a_updown_change_period);
	RUN_TEST(test_timer_divider);
	RUN_TEST(test_timer_capture_by_software);
	RUN_TEST(test_timer_capture_by_signal);
	RUN_TEST(test_timer_a_compare);
	RUN_TEST(test_timer_b_compare);
	RUN_TEST(test_timer_b_length_8);
	RUN_TEST(test_timer_b_length_10);
	RUN_TEST(test_timer_b_length_12);
	RUN_TEST(test_timer_b_length_16);
	RUN_TEST(test_timer_b_compare_latch_0_up);
	RUN_TEST(test_timer_b_compare_latch_1_up);
	RUN_TEST(test_timer_b_compare_latch_2_continuous);
	RUN_TEST(test_timer_b_compare_latch_2_up);
	RUN_TEST(test_timer_b_compare_latch_2_updown);
	RUN_TEST(test_timer_b_compare_latch_3_up);
	RUN_TEST(test_timer_b_grouping_1);
	RUN_TEST(test_timer_b_grouping_2);
	RUN_TEST(test_timer_b_grouping_3);
}
