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

#include <string.h>

#include "output.h"
#include "output_util.h"
#include "dis.h"
#include "simio.h"
#include "simio_cpu.h"
#include "simio_device.h"

#include "simio_tracer.h"
#include "simio_timer.h"
#include "simio_wdt.h"
#include "simio_hwmult.h"
#include "simio_gpio.h"
#include "simio_console.h"

static const struct simio_class *const class_db[] = {
	&simio_tracer,
	&simio_timer,
	&simio_wdt,
	&simio_hwmult,
	&simio_gpio,
	&simio_console
};

/* Simulator data. We keep a list of devices on the bus, and the special
 * function registers.
 *
 * Currently, MCLK and SMCLK are tied together, and ACLK runs at a fixed
 * ratio of 1:256 with MCLK. aclk_counter counts fractional cycles.
 */
static struct list_node device_list;
static uint8_t sfr_data[16];
static int aclk_counter;

static void destroy_device(struct simio_device *dev)
{
	list_remove(&dev->node);
	dev->type->destroy(dev);
}

void simio_init(void)
{
	list_init(&device_list);
	simio_reset();
}

void simio_exit(void)
{
	while (!LIST_EMPTY(&device_list))
		destroy_device((struct simio_device *)device_list.next);
}

static const struct simio_class *find_class(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_LEN(class_db); i++) {
		const struct simio_class *t = class_db[i];

		if (!strcasecmp(t->name, name))
			return t;
	}

	return NULL;
}

static struct simio_device *find_device(const char *name)
{
	struct list_node *n;

	for (n = device_list.next; n != &device_list; n = n->next) {
		struct simio_device *dev = (struct simio_device *)n;

		if (!strcasecmp(dev->name, name))
			return dev;
	}

	return NULL;
}

static int cmd_add(char **arg_text)
{
	const char *type_text = get_arg(arg_text);
	const char *name_text = get_arg(arg_text);
	const struct simio_class *type;
	struct simio_device *dev;

	if (!(name_text && type_text)) {
		printc_err("simio add: device class and name must be "
			   "specified.\n");
		return -1;
	}

	if (find_device(name_text)) {
		printc_err("simio add: device name is not unique: %s\n",
			   name_text);
		return -1;
	}

	type = find_class(type_text);
	if (!type) {
		printc_err("simio add: unknown type.\n");
		return -1;
	}

	dev = type->create(arg_text);
	if (!dev) {
		printc_err("simio add: failed to create device.\n");
		return -1;
	}

	list_insert(&dev->node, &device_list);
	strncpy(dev->name, name_text, sizeof(dev->name));
	dev->name[sizeof(dev->name) - 1] = 0;

	printc_dbg("Added new device \"%s\" of type \"%s\".\n",
		   dev->name, dev->type->name);
	return 0;
}

static int cmd_del(char **arg_text)
{
	const char *name_text = get_arg(arg_text);
	struct simio_device *dev;

	if (!name_text) {
		printc_err("simio del: device name must be specified.\n");
		return -1;
	}

	dev = find_device(name_text);
	if (!dev) {
		printc_err("simio del: no such device: %s\n", name_text);
		return -1;
	}

	destroy_device(dev);
	printc_dbg("Destroyed device \"%s\".\n", name_text);
	return 0;
}

static int cmd_devices(char **arg_text)
{
	struct list_node *n;

	(void)arg_text;

	for (n = device_list.next; n != &device_list; n = n->next) {
		struct simio_device *dev = (struct simio_device *)n;
		int irq = -1;

		if (dev->type->check_interrupt)
			irq = dev->type->check_interrupt(dev);

		printc("    %-10s (type %s", dev->name, dev->type->name);
		if (irq < 0)
			printc(")\n");
		else
			printc(", IRQ pending: %d)\n", irq);
	}

	return 0;
}

static int cmd_classes(char **arg_text)
{
	struct vector v;
	int i;

	(void)arg_text;

	vector_init(&v, sizeof(const char *));
	for (i = 0; i < ARRAY_LEN(class_db); i++) {
		if (vector_push(&v, &class_db[i]->name, 1) < 0) {
			printc_err("simio classes: can't allocate memory\n");
			vector_destroy(&v);
			return -1;
		}
	}

	printc("Available device classes:\n");
	namelist_print(&v);
	vector_destroy(&v);

	return 0;
}

static int cmd_help(char **arg_text)
{
	const char *name = get_arg(arg_text);
	const struct simio_class *type;

	if (!name) {
		printc_err("simio help: you must specify a device class\n");
		return -1;
	}

	type = find_class(name);
	if (!type) {
		printc_err("simio help: unknown device class: %s\n", name);
		return -1;
	}

	printc("\x1b[1mDEVICE CLASS: %s\x1b[0m\n\n%s\n", type->name, type->help);
	return 0;
}

static int cmd_config(char **arg_text)
{
	const char *name = get_arg(arg_text);
	const char *param = get_arg(arg_text);
	struct simio_device *dev;

	if (!(name && param)) {
		printc_err("simio config: you must specify a device name and "
			   "a parameter\n");
		return -1;
	}

	dev = find_device(name);
	if (!dev) {
		printc_err("simio config: no such device: %s\n", name);
		return -1;
	}

	if (!dev->type->config) {
		printc_err("simio config: no configuration parameters are "
			   "defined for this device\n");
		return -1;
	}

	return dev->type->config(dev, param, arg_text);
}

static int cmd_info(char **arg_text)
{
	const char *name = get_arg(arg_text);
	struct simio_device *dev;

	if (!name) {
		printc_err("simio info: you must specify a device name\n");
		return -1;
	}

	dev = find_device(name);
	if (!dev) {
		printc_err("simio info: no such device: %s\n", name);
		return -1;
	}

	if (!dev->type->info) {
		printc_err("simio config: no information available\n");
		return -1;
	}

	return dev->type->info(dev);
}

int cmd_simio(char **arg_text)
{
	const char *subcmd = get_arg(arg_text);
	static const struct {
		const char *name;
		int (*func)(char **arg_text);
	} cmd_table[] = {
		{"add",		cmd_add},
		{"del",		cmd_del},
		{"devices",	cmd_devices},
		{"classes",	cmd_classes},
		{"help",	cmd_help},
		{"config",	cmd_config},
		{"info",	cmd_info}
	};
	int i;

	if (!subcmd) {
		printc_err("simio: a subcommand is required\n");
		return -1;
	}

	for (i = 0; i < ARRAY_LEN(cmd_table); i++)
		if (!strcasecmp(cmd_table[i].name, subcmd))
			return cmd_table[i].func(arg_text);

	printc_err("simio: unknown subcommand: %s\n", subcmd);
	return -1;
}

void simio_reset(void)
{
	struct list_node *n;

	memset(sfr_data, 0, sizeof(sfr_data));
	aclk_counter = 0;

	for (n = device_list.next; n != &device_list; n = n->next) {
		struct simio_device *dev = (struct simio_device *)n;
		const struct simio_class *type = dev->type;

		if (type->reset)
			type->reset(dev);
	}
}

#define IO_REQUEST_FUNC(name, method, datatype) \
int name(address_t addr, datatype data) { \
	struct list_node *n; \
	int ret = 1; \
\
	for (n = device_list.next; n != &device_list; n = n->next) { \
		struct simio_device *dev = (struct simio_device *)n; \
		const struct simio_class *type = dev->type; \
\
		if (type->method) { \
			int r = type->method(dev, addr, data); \
\
			if (r < ret) \
				ret = r; \
		} \
	} \
\
	return ret; \
}
#define IO_REQUEST_FUNC_S(name, method, datatype) \
	static IO_REQUEST_FUNC(name, method, datatype)

IO_REQUEST_FUNC(simio_write, write, uint16_t)
IO_REQUEST_FUNC(simio_read, read, uint16_t *)
IO_REQUEST_FUNC_S(simio_write_b_device, write_b, uint8_t)
IO_REQUEST_FUNC_S(simio_read_b_device, read_b, uint8_t *)

int simio_write_b(address_t addr, uint8_t data)
{
	if (addr < 16) {
		sfr_data[addr] = data;
		return 0;
	}

	return simio_write_b_device(addr, data);
}

int simio_read_b(address_t addr, uint8_t *data)
{
	if (addr < 16) {
		*data = sfr_data[addr];
		return 0;
	}

	return simio_read_b_device(addr, data);
}

int simio_check_interrupt(void)
{
	int irq = -1;
	struct list_node *n;

	for (n = device_list.next; n != &device_list; n = n->next) {
		struct simio_device *dev = (struct simio_device *)n;
		const struct simio_class *type = dev->type;

		if (type->check_interrupt) {
			int i = type->check_interrupt(dev);

			if (i > irq)
				irq = i;
		}
	}

	return irq;
}

void simio_ack_interrupt(int irq)
{
	struct list_node *n;

	for (n = device_list.next; n != &device_list; n = n->next) {
		struct simio_device *dev = (struct simio_device *)n;
		const struct simio_class *type = dev->type;

		if (type->ack_interrupt)
			type->ack_interrupt(dev, irq);
	}
}

void simio_step(uint16_t status_register, int cycles)
{
	int clocks[SIMIO_NUM_CLOCKS] = {0};
	struct list_node *n;

	aclk_counter += cycles;

	clocks[SIMIO_MCLK] = cycles;
	clocks[SIMIO_SMCLK] = cycles;
	clocks[SIMIO_ACLK] = aclk_counter >> 8;

	aclk_counter &= 0xff;

	if (status_register & MSP430_SR_CPUOFF)
		clocks[SIMIO_MCLK] = 0;

	if (status_register & MSP430_SR_SCG1)
		clocks[SIMIO_SMCLK] = 0;

	if (status_register & MSP430_SR_OSCOFF)
		clocks[SIMIO_ACLK] = 0;

	for (n = device_list.next; n != &device_list; n = n->next) {
		struct simio_device *dev = (struct simio_device *)n;
		const struct simio_class *type = dev->type;

		if (type->step)
			type->step(dev, status_register, clocks);
	}
}

uint8_t simio_sfr_get(address_t which)
{
	if (which > sizeof(sfr_data))
		return 0;

	return sfr_data[which];
}

void simio_sfr_modify(address_t which, uint8_t mask, uint8_t bits)
{
	if (which > sizeof(sfr_data))
		return;

	sfr_data[which] = (sfr_data[which] & ~mask) | bits;
}
