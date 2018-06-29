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
#define TAIV_TAIFG      0x0a

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

static void test_timer_up()
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

static void test_timer_capture()
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

static void test_timer_compare()
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
	RUN_TEST(test_address_space);
	RUN_TEST(test_timer_continuous);
	RUN_TEST(test_timer_stop);
	RUN_TEST(test_timer_up_stop);
	RUN_TEST(test_timer_updown_stop);
	RUN_TEST(test_timer_up);
	RUN_TEST(test_timer_divider);
	RUN_TEST(test_timer_capture);
	RUN_TEST(test_timer_compare);
}
