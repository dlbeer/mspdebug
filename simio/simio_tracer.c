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
#include "simio_tracer.h"
#include "expr.h"
#include "output.h"
#include "output_util.h"
#include "dis.h"

#define DEFAULT_HISTORY		16

typedef enum {
	EVENT_WRITE_16,
	EVENT_READ_16,
	EVENT_WRITE_8,
	EVENT_READ_8,
	EVENT_IRQ_HANDLE,
	EVENT_RESET
} event_type_t;

typedef unsigned long long counter_t;

struct event {
	counter_t		when;
	event_type_t		what;
	address_t		addr;
	uint16_t		data;
};

struct tracer {
	struct simio_device	base;

	/* IO event history ring buffer. */
	struct event		*history;
	int			size;
	int			head;
	int			tail;

	/* Clock and instruction counters. */
	counter_t		cycles[SIMIO_NUM_CLOCKS];
	counter_t		inscount;

	/* Outstanding interrupt request. */
	int			irq_request;

	/* Verbose mode. */
	int			verbose;
};

static void event_print(const struct event *e)
{
	char name[128];

	print_address(e->addr, name, sizeof(name), 0);
	printc("  %10" LLFMT ": ", e->when);

	switch (e->what) {
	case EVENT_WRITE_16:
		printc("write.w => %s 0x%04x\n", name, e->data);
		break;

	case EVENT_READ_16:
		printc("read.w => %s\n", name);
		break;

	case EVENT_WRITE_8:
		printc("write.b => %s 0x%02x\n", name, e->data);
		break;

	case EVENT_READ_8:
		printc("read.b => %s\n", name);
		break;

	case EVENT_IRQ_HANDLE:
		printc("irq handle %d\n", e->addr);
		break;

	case EVENT_RESET:
		printc("system reset\n");
		break;

	default:
		printc("unknown 0x%04x 0x%04x\n", e->addr, e->data);
		break;
	}
}

static void event_rec(struct tracer *tr, event_type_t what,
		      address_t addr, uint16_t data)
{
	struct event *e = &tr->history[tr->head];

	e->when = tr->cycles[SIMIO_MCLK];
	e->what = what;
	e->addr = addr;
	e->data = data;

	if (tr->verbose)
		event_print(e);

	tr->head = (tr->head + 1) % tr->size;
	if (tr->head == tr->tail)
		tr->tail = (tr->tail + 1) % tr->size;
}

static struct simio_device *tracer_create(char **arg_text)
{
	const char *size_text = get_arg(arg_text);
	int size = DEFAULT_HISTORY;
	struct event *history;
	struct tracer *tr;

	if (size_text) {
		address_t value;

		if (expr_eval(size_text, &value) < 0) {
			printc_err("tracer: can't parse history size: %s\n",
				   size_text);
			return NULL;
		}

		size = value;
		if (size < 2) {
			printc_err("tracer: invalid size: %d\n", size);
			return NULL;
		}
	}

	history = malloc(sizeof(history[0]) * size);
	if (!history) {
		pr_error("tracer: couldn't allocate memory for history");
		return NULL;
	}

	tr = malloc(sizeof(*tr));
	if (!tr) {
		pr_error("tracer: couldn't allocate memory");
		free(history);
		return NULL;
	}

	memset(tr, 0, sizeof(*tr));
	tr->base.type = &simio_tracer;
	tr->history = history;
	tr->size = size;
	tr->irq_request = -1;

	return (struct simio_device *)tr;
}

static void tracer_destroy(struct simio_device *dev)
{
	struct tracer *tr = (struct tracer *)dev;

	free(tr->history);
	free(tr);
}

static void tracer_reset(struct simio_device *dev)
{
	struct tracer *tr = (struct tracer *)dev;

	event_rec(tr, EVENT_RESET, 0, 0);
}

static int tracer_config(struct simio_device *dev,
			 const char *param, char **arg_text)
{
	struct tracer *tr = (struct tracer *)dev;

	if (!strcasecmp(param, "verbose"))
		tr->verbose = 1;
	else if (!strcasecmp(param, "quiet"))
		tr->verbose = 0;
	else if (!strcasecmp(param, "untrigger"))
		tr->irq_request = -1;
	else if (!strcasecmp(param, "clear")) {
		tr->head = 0;
		tr->tail = 0;
		memset(tr->cycles, 0, sizeof(tr->cycles));
		tr->inscount = 0;
	} else if (!strcasecmp(param, "trigger")) {
		const char *irq_text = get_arg(arg_text);
		address_t value;

		if (!irq_text) {
			printc_err("tracer: trigger: must specify an IRQ "
				   "number\n");
			return -1;
		}

		if (expr_eval(irq_text, &value) < 0) {
			printc_err("tracer: trigger: can't parse IRQ "
				   "number: %s\n", irq_text);
			return -1;
		}

		if (value >= 16) {
			printc_err("tracer: trigger: invalid IRQ: %d\n",
				   value);
			return -1;
		}

		tr->irq_request = value;
	} else {
		printc_err("tracer: unknown config parameter: %s\n", param);
		return -1;
	}

	return 0;
}

static int tracer_info(struct simio_device *dev)
{
	struct tracer *tr = (struct tracer *)dev;
	int i;

	printc("Instruction count: %" LLFMT "\n", tr->inscount);
	printc("MCLK:              %" LLFMT "\n", tr->cycles[SIMIO_MCLK]);
	printc("SMCLK              %" LLFMT "\n", tr->cycles[SIMIO_SMCLK]);
	printc("ACLK:              %" LLFMT "\n", tr->cycles[SIMIO_ACLK]);

	if (tr->irq_request >= 0)
		printc("IRQ pending:       %d\n", tr->irq_request);
	else
		printc("No IRQ is pending\n");

	printc("\nIO event history (oldest first):\n");
	for (i = tr->tail; i != tr->head; i = (i + 1) % tr->size)
		event_print(&tr->history[i]);

	return 0;
}

static int tracer_write(struct simio_device *dev,
			address_t addr, uint16_t data)
{
	struct tracer *tr = (struct tracer *)dev;

	event_rec(tr, EVENT_WRITE_16, addr, data);
	return 1;
}

static int tracer_read(struct simio_device *dev,
		       address_t addr, uint16_t *data)
{
	struct tracer *tr = (struct tracer *)dev;

	(void)data;

	event_rec(tr, EVENT_READ_16, addr, 0);
	return 1;
}

static int tracer_write_b(struct simio_device *dev,
			  address_t addr, uint8_t data)
{
	struct tracer *tr = (struct tracer *)dev;

	event_rec(tr, EVENT_WRITE_8, addr, data);
	return 1;
}

static int tracer_read_b(struct simio_device *dev,
			 address_t addr, uint8_t *data)
{
	struct tracer *tr = (struct tracer *)dev;

	(void)data;

	event_rec(tr, EVENT_READ_8, addr, 0);
	return 1;
}

static int tracer_check_interrupt(struct simio_device *dev)
{
	struct tracer *tr = (struct tracer *)dev;

	return tr->irq_request;
}

static void tracer_ack_interrupt(struct simio_device *dev, int irq)
{
	struct tracer *tr = (struct tracer *)dev;

	if (tr->irq_request == irq)
		tr->irq_request = -1;

	event_rec(tr, EVENT_IRQ_HANDLE, irq, 0);
}

static void tracer_step(struct simio_device *dev,
			uint16_t status, const int *clocks)
{
	struct tracer *tr = (struct tracer *)dev;
	int i;

	for (i = 0; i < SIMIO_NUM_CLOCKS; i++)
		tr->cycles[i] += clocks[i];

	if (!(status & MSP430_SR_CPUOFF))
		tr->inscount++;
}

const struct simio_class simio_tracer = {
	.name = "tracer",
	.help =
"A debug peripheral to implement IO tracing. This will keep a record of\n"
"IO activity which can be checked at any time. It can also be used to\n"
"manually trigger interrupts.\n"
"\n"
"Constructor arguments: [history-size]\n"
"    If specified, enlarge the IO event history from its default size.\n"
"\n"
"Config arguments are:\n"
"    verbose\n"
"        Show IO events as they occur.\n"
"    quiet\n"
"        Only show IO events when requested (default).\n"
"    trigger <irq>\n"
"        Trigger an specific IRQ vector.\n"
"    untrigger\n"
"        Cancel an interrupt request.\n"
"    clear\n"
"        Clear the IO history and counter so far.\n",

	.create			= tracer_create,
	.destroy		= tracer_destroy,
	.reset			= tracer_reset,
	.config			= tracer_config,
	.info			= tracer_info,
	.write			= tracer_write,
	.read			= tracer_read,
	.write_b		= tracer_write_b,
	.read_b			= tracer_read_b,
	.check_interrupt	= tracer_check_interrupt,
	.ack_interrupt		= tracer_ack_interrupt,
	.step			= tracer_step
};
