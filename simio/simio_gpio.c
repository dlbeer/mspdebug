/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009, 2010 Daniel Beer
 *
 * This program is free software; you can redisgibute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is disgibuted in the hope that it will be useful,
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
#include "simio_gpio.h"
#include "expr.h"
#include "output.h"

#define REG_IN			0
#define REG_OUT			1
#define REG_DIR			2
#define REG_IFG			3
#define REG_IES			4
#define REG_IE			5
#define REG_SEL			6
#define REG_REN			7

struct gpio {
	struct simio_device	base;

	/* Print when output changes? */
	int			verbose;

	/* Base address */
	address_t		base_addr;

	/* IRQ, or -1 if disabled */
	int			irq;

	/* Congol registers */
	uint8_t			regs[8];
};

static struct simio_device *gpio_create(char **arg_text)
{
	struct gpio *g;

	(void)arg_text;

	g = malloc(sizeof(*g));
	if (!g) {
		pr_error("gpio: can't allocate memory");
		return NULL;
	}

	memset(g, 0, sizeof(*g));
	g->base.type = &simio_gpio;
	g->base_addr = 0x20;
	g->irq = -1;

	return (struct simio_device *)g;
}

static void gpio_destroy(struct simio_device *dev)
{
	struct gpio *g = (struct gpio *)dev;

	free(g);
}

static void gpio_reset(struct simio_device *dev)
{
	struct gpio *g = (struct gpio *)dev;

	g->regs[REG_DIR] = 0;
	g->regs[REG_IFG] = 0;
	g->regs[REG_IE] = 0;
	g->regs[REG_SEL] = 0;
	g->regs[REG_REN] = 0;
}

static int config_addr(address_t *addr, char **arg_text)
{
	char *text = get_arg(arg_text);

	if (!text) {
		printc_err("gpio: config: expected address\n");
		return -1;
	}

	if (expr_eval(text, addr) < 0) {
		printc_err("gpio: can't parse address: %s\n", text);
		return -1;
	}

	return 0;
}

static int config_irq(int *irq, char **arg_text)
{
	char *text = get_arg(arg_text);
	address_t value;

	if (!text) {
		printc_err("gpio: config: expected interrupt number\n");
		return -1;
	}

	if (expr_eval(text, &value) < 0) {
		printc_err("gpio: can't parse interrupt number: %s\n", text);
		return -1;
	}

	*irq = value;
	return 0;
}

static int config_channel(struct gpio *g, char **arg_text)
{
	char *which_text = get_arg(arg_text);
	char *value_text = get_arg(arg_text);
	address_t which;
	address_t value;
	uint8_t mask;

	if (!(which_text && value_text)) {
		printc_err("gpio: config: expected pin and value\n");
		return -1;
	}

	if (expr_eval(which_text, &which) < 0) {
		printc_err("gpio: can't parse pin number: %s\n",
			   which_text);
		return -1;
	}

	if (expr_eval(value_text, &value) < 0) {
		printc_err("gpio: can't parse pin value: %s\n",
			   value_text);
		return -1;
	}

	if (which > 7) {
		printc_err("gpio: invalid pin number: %d\n", which);
		return -1;
	}

	mask = 1 << which;
	if (g->regs[REG_IE] & mask) {
		if (((g->regs[REG_IES] & mask) &&
		     !value && (g->regs[REG_IN] & mask)) ||
		    (!(g->regs[REG_IES] & mask) &&
		     value && !(g->regs[REG_IN] & mask)))
			g->regs[REG_IFG] |= mask;
	}

	if (value)
		g->regs[REG_IN] |= mask;
	else
		g->regs[REG_IN] &= ~mask;

	return 0;
}

static int gpio_config(struct simio_device *dev,
			const char *param, char **arg_text)
{
	struct gpio *g = (struct gpio *)dev;

	if (!strcasecmp(param, "base"))
		return config_addr(&g->base_addr, arg_text);

	if (!strcasecmp(param, "irq"))
		return config_irq(&g->irq, arg_text);

	if (!strcasecmp(param, "set"))
		return config_channel(g, arg_text);

	if (!strcasecmp(param, "noirq")) {
		g->irq = -1;
		return 0;
	}

	if (!strcasecmp(param, "verbose")) {
		g->verbose = 1;
		return 0;
	}

	if (!strcasecmp(param, "quiet")) {
		g->verbose = 0;
		return 0;
	}

	printc_err("gpio: config: unknown parameter: %s\n", param);
	return -1;
}

static void print_tristate(uint8_t mask, uint8_t value)
{
	int i;

	for (i = 0; i < 8; i++) {
		if (!(mask & 0x80))
			printc("-");
		else if (value & 0x80)
			printc("H");
		else
			printc("l");

		if (i == 3)
			printc(" ");

		value <<= 1;
		mask <<= 1;
	}
}

static int port_map(struct gpio *g, address_t addr)
{
	int ren_addr;

	if (g->irq >= 0) {
		if (addr < g->base_addr)
			return -1;
		if (addr >= g->base_addr + 8)
			return -1;

		return addr - g->base_addr;
	}

	if (addr >= g->base_addr && addr <= g->base_addr + 2)
		return addr - g->base_addr;

	if (addr == g->base_addr + 3)
		return REG_SEL;

	ren_addr = ((g->base_addr >> 2) & 1) |
		((g->base_addr >> 4) & 2) | 0x10;
	if (addr == ren_addr)
		return REG_REN;

	return -1;
}

static int gpio_info(struct simio_device *dev)
{
	struct gpio *g = (struct gpio *)dev;

	printc("Base address:          0x%04x\n", g->base_addr);

	printc("Input state:           ");
	print_tristate(~g->regs[REG_DIR] & ~g->regs[REG_SEL], g->regs[REG_IN]);
	printc("\n");
	printc("Output state:          ");
	print_tristate(g->regs[REG_DIR] & ~g->regs[REG_SEL], g->regs[REG_OUT]);
	printc("\n");
	printc("Direction:             ");
	print_tristate(~g->regs[REG_SEL], g->regs[REG_DIR]);
	printc("\n");

	if (g->irq >= 0) {
		printc("IRQ:                   %d\n", g->irq);
		printc("Interrupt:             ");
		print_tristate(g->regs[REG_IE], g->regs[REG_IFG]);
		printc("\n");
		printc("Interrupt edge select: ");
		print_tristate(g->regs[REG_IE], g->regs[REG_IES]);
		printc("\n");
		printc("Interrupt enable:      ");
		print_tristate(0xff, g->regs[REG_IE]);
		printc("\n");
	}

	printc("Port select:           ");
	print_tristate(0xff, g->regs[REG_SEL]);
	printc("\n");
	printc("Resistor enable:       ");
	print_tristate(0xff, g->regs[REG_REN]);
	printc("\n");

	return 0;
}

static int gpio_write_b(struct simio_device *dev,
			address_t addr, uint8_t data)
{
	struct gpio *g = (struct gpio *)dev;
	int index = port_map(g, addr);

	if (index < 0)
		return 1;

	if (g->verbose && index == REG_OUT) {
		uint8_t delta = (g->regs[REG_OUT] ^ data) &
			g->regs[REG_DIR] & ~g->regs[REG_SEL];

		if (delta) {
			printc("gpio: state change on %s: ", g->base.name);
			print_tristate(delta, data);
			printc("\n");
		}
	}

	g->regs[index] = data;
	return 1;
}

static int gpio_read_b(struct simio_device *dev,
		       address_t addr, uint8_t *data)
{
	struct gpio *g = (struct gpio *)dev;
	int index = port_map(g, addr);

	if (index < 0)
		return 1;

	if (addr < g->base_addr || index >= 8)
		return 1;

	*data = g->regs[index];
	return 0;
}

static int gpio_check_interrupt(struct simio_device *dev)
{
	struct gpio *g = (struct gpio *)dev;

	if (g->regs[REG_IFG] & g->regs[REG_IE])
		return g->irq;

	return -1;
}

const struct simio_class simio_gpio = {
	.name = "gpio",
	.help =
"This peripheral implements a digital IO port, with optional interrupt\n"
"functionality.\n"
"\n"
"Config arguments are:\n"
"    base <address>\n"
"        Set the peripheral base address.\n"
"    irq <interrupt>\n"
"        Set the interrupt vector for input pin state changes.\n"
"    noirq\n"
"        Disable interrupt functionality.\n"
"    verbose\n"
"        Print a message when output states change.\n"
"    quiet\n"
"        Don't print messages as output state changes.\n"
"    set <pin> <0|1>\n"
"        Set input pin state.\n",

	.create			= gpio_create,
	.destroy		= gpio_destroy,
	.reset			= gpio_reset,
	.config			= gpio_config,
	.info			= gpio_info,
	.write_b		= gpio_write_b,
	.read_b			= gpio_read_b,
	.check_interrupt	= gpio_check_interrupt
};
