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

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>

#include "dis.h"
#include "device.h"
#include "binfile.h"
#include "stab.h"
#include "util.h"
#include "usbutil.h"
#include "gdb.h"
#include "rtools.h"
#include "sym.h"
#include "devcmd.h"
#include "expr.h"

#include "sim.h"
#include "bsl.h"
#include "fet.h"
#include "vector.h"
#include "fet_db.h"

#include "uif.h"
#include "olimex.h"
#include "rf2500.h"

static void io_prefix(stab_t stab,
		      const char *prefix, uint16_t pc,
		      uint16_t addr, int is_byte)
{
	char name[64];

	if (!stab_nearest(stab, pc, name, sizeof(name), &pc)) {
		printf("%s", name);
		if (pc)
			printf("+0x%x", pc);
	} else {
		printf("0x%04x", pc);
	}

	printf(": IO %s.%c: 0x%04x", prefix, is_byte ? 'B' : 'W', addr);
	if (!stab_nearest(stab, addr, name, sizeof(name), &addr)) {
		printf(" (%s", name);
		if (addr)
			printf("+0x%x", addr);
		printf(")");
	}
}

static int fetch_io(void *user_data, uint16_t pc,
		    uint16_t addr, int is_byte, uint16_t *data_ret)
{
	stab_t stab = (stab_t)user_data;

	io_prefix(stab, "READ", pc, addr, is_byte);

	for (;;) {
		char text[128];
		int len;
		int data;

		printf("? ");
		fflush(stdout);
		if (!fgets(text, sizeof(text), stdin)) {
			printf("\nAborted IO request\n");
			return -1;
		}

		len = strlen(text);
		while (len && isspace(text[len - 1]))
			len--;
		text[len] = 0;

		if (!len)
			return 0;

		if (!expr_eval(stab, text, &data)) {
			if (data_ret)
				*data_ret = data;
			return 0;
		}
	}

	return 0;
}

static void store_io(void *user_data, uint16_t pc,
		     uint16_t addr, int is_byte, uint16_t data)
{
	stab_t stab = (stab_t)user_data;

	io_prefix(stab, "WRITE", pc, addr, is_byte);

	if (is_byte)
		printf(" => 0x%02x\n", data & 0xff);
	else
		printf(" => 0x%04x\n", data);
}

struct cmdline_args {
	const char      *driver_name;
	const char      *serial_device;
	const char      *usb_device;
	const char      *fet_force_id;
	int             want_jtag;
	int             no_rc;
	int             vcc_mv;
	stab_t          stab;
};

struct driver {
	const char      *name;
	const char      *help;
	device_t        (*func)(const struct cmdline_args *args);
};

static device_t driver_open_fet(const struct cmdline_args *args,
				int flags, transport_t trans)
{
	device_t dev;

	if (!args->want_jtag)
		flags |= FET_PROTO_SPYBIWIRE;

	dev = fet_open(trans, flags, args->vcc_mv, args->fet_force_id);
	if (!dev) {
		trans->destroy(trans);
		return NULL;
	}

	return dev;
}

static device_t driver_open_rf2500(const struct cmdline_args *args)
{
	transport_t trans;

	if (args->serial_device) {
		fprintf(stderr, "This driver does not support tty devices.\n");
		return NULL;
	}

	trans = rf2500_open(args->usb_device);
	if (!trans)
		return NULL;

	return driver_open_fet(args, FET_PROTO_RF2500, trans);
}

static device_t driver_open_olimex(const struct cmdline_args *args)
{
	transport_t trans;

	if (args->serial_device)
		trans = uif_open(args->serial_device, 1);
	else
		trans = olimex_open(args->usb_device);

	if (!trans)
		return NULL;

	return driver_open_fet(args, FET_PROTO_OLIMEX, trans);
}

static device_t driver_open_sim(const struct cmdline_args *args)
{
	return sim_open(fetch_io, store_io, args->stab);
}

static device_t driver_open_uif(const struct cmdline_args *args)
{
	transport_t trans;

	if (!args->serial_device) {
		fprintf(stderr,	"This driver does not support USB access. "
			"Specify a tty device using -d.\n");
		return NULL;
	}

	trans = uif_open(args->serial_device, 0);
	if (!trans)
		return NULL;

	return driver_open_fet(args, 0, trans);
}

static device_t driver_open_uif_bsl(const struct cmdline_args *args)
{
	if (!args->serial_device) {
		fprintf(stderr,	"This driver does not support USB access. "
			"Specify a tty device using -d.\n");
		return NULL;
	}

	return bsl_open(args->serial_device);
}

static const struct driver driver_table[] = {
	{
		.name = "rf2500",
		.help = "eZ430-RF2500 devices. Only USB connection is "
		"supported.",
		driver_open_rf2500
	},
	{       .name = "olimex",
		.help = "Olimex MSP-JTAG-TINY.",
		.func = driver_open_olimex
	},
	{
		.name = "sim",
		.help = "Simulation mode.",
		.func = driver_open_sim
	},
	{
		.name = "uif",
		.help = "TI FET430UIF and compatible devices (e.g. eZ430).",
		.func = driver_open_uif
	},
	{
		.name = "uif-bsl",
		.help = "TI FET430UIF bootloader.",
		.func = driver_open_uif_bsl
	}
};

static void usage(const char *progname)
{
	int i;

	fprintf(stderr,
"Usage: %s [options] <driver> [command ...]\n"
"\n"
"    -d device\n"
"        Connect via the given tty device, rather than USB.\n"
"    -U bus:dev\n"
"        Specify a particular USB device to connect to.\n"
"    -j\n"
"        Use JTAG, rather than Spy-Bi-Wire (UIF devices only).\n"
"    -v voltage\n"
"        Set the supply voltage, in millivolts.\n"
"    -n\n"
"        Do not read ~/.mspdebug on startup.\n"
"    --help\n"
"        Show this help text.\n"
"    --fet-list\n"
"        Show a list of devices supported by the FET driver.\n"
"    --fet-force-id string\n"
"        Override the device ID returned by the FET.\n"
"    --usb-list\n"
"        Show a list of available USB devices.\n"
"\n"
"Most drivers connect by default via USB, unless told otherwise via the\n"
"-d option. By default, the first USB device found is opened.\n"
"\n"
"If commands are given, they will be executed. Otherwise, an interactive\n"
"command reader is started.\n\n",
		progname);

	printf("Available drivers are:\n");
	for (i = 0; i < ARRAY_LEN(driver_table); i++) {
		const struct driver *drv = &driver_table[i];

		printf("    %s\n        %s\n", drv->name, drv->help);
	}
}

static void process_rc_file(cproc_t cp)
{
	const char *home = getenv("HOME");
	char text[256];

	if (!home)
		return;

	snprintf(text, sizeof(text), "%s/.mspdebug", home);
	if (!access(text, F_OK))
		cproc_process_file(cp, text);
}

static int add_fet_device(void *user_data, const struct fet_db_record *r)
{
	struct vector *v = (struct vector *)user_data;

	return vector_push(v, &r->name, 1);
}

static int cmp_char_ptr(const void *a, const void *b)
{
	return strcmp(*(const char **)a, *(const char **)b);
}

static int list_devices(void)
{
	struct vector v;
	int i;

	vector_init(&v, sizeof(const char *));
	if (fet_db_enum(add_fet_device, &v) < 0) {
		perror("couldn't allocate memory");
		vector_destroy(&v);
		return -1;
	}

	qsort(v.ptr, v.size, v.elemsize, cmp_char_ptr);

	printf("Devices supported by FET driver:\n");
	for (i = 0; i < v.size; i++)
		printf("    %s\n", VECTOR_AT(v, i, const char *));

	vector_destroy(&v);
	return 0;
}

static int parse_cmdline_args(int argc, char **argv,
			      struct cmdline_args *args)
{
	int opt;
	const static struct option longopts[] = {
		{"help",                0, 0, 'H'},
		{"fet-list",            0, 0, 'L'},
		{"fet-force-id",        1, 0, 'F'},
		{"usb-list",            0, 0, 'I'},
		{NULL, 0, 0, 0}
	};

	while ((opt = getopt_long(argc, argv, "d:jv:nU:",
				  longopts, NULL)) >= 0)
		switch (opt) {
		case 'I':
			usb_init();
			usb_find_busses();
			usb_find_devices();
			usbutil_list();
			exit(0);

		case 'd':
			args->serial_device = optarg;
			break;

		case 'U':
			args->usb_device = optarg;
			break;

		case 'L':
			exit(list_devices());

		case 'F':
			args->fet_force_id = optarg;
			break;

		case 'H':
			usage(argv[0]);
			exit(0);

		case 'v':
			args->vcc_mv = atoi(optarg);
			break;

		case 'j':
			args->want_jtag = 1;
			break;

		case 'n':
			args->no_rc = 1;
			break;

		case '?':
			fprintf(stderr, "Try --help for usage information.\n");
			return -1;
		}

	if (args->usb_device && args->serial_device) {
		fprintf(stderr, "You can't simultaneously specify a serial and "
			"a USB device.\n");
		return -1;
	}

	if (optind >= argc) {
		fprintf(stderr, "You need to specify a driver. Try --help for "
			"a list.\n");
		return -1;
	}

	args->driver_name = argv[optind];
	optind++;

	return 0;
}

cproc_t setup_cproc(struct cmdline_args *args)
{
	int i;
	device_t msp430_dev;
	stab_t stab;
	cproc_t cp;

	i = 0;
	while (i < ARRAY_LEN(driver_table) &&
	       strcasecmp(driver_table[i].name, args->driver_name))
		i++;
	if (i >= ARRAY_LEN(driver_table)) {
		fprintf(stderr, "Unknown driver: %s. Try --help for a list.\n",
			args->driver_name);
		return NULL;
	}

	stab = stab_new();
	if (!stab)
		return NULL;
	args->stab = stab;

	msp430_dev = driver_table[i].func(args);
	if (!msp430_dev) {
		stab_destroy(stab);
		return NULL;
	}

	cp = cproc_new(msp430_dev, stab);
	if (!cp) {
		msp430_dev->destroy(msp430_dev);
		stab_destroy(stab);
		return NULL;
	}

	if (sym_register(cp) < 0 ||
	    devcmd_register(cp) < 0 ||
	    gdb_register(cp) < 0 ||
	    rtools_register(cp) < 0) {
		cproc_destroy(cp);
		return NULL;
	}

	return cp;
}

int main(int argc, char **argv)
{
	struct cmdline_args args = {0};
	cproc_t cp;
	int ret = 0;

	puts(
"MSPDebug version 0.10 - debugging tool for MSP430 MCUs\n"
"Copyright (C) 2009, 2010 Daniel Beer <daniel@tortek.co.nz>\n"
"This is free software; see the source for copying conditions.  There is NO\n"
"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR "
"PURPOSE.\n");

	args.vcc_mv = 3000;
	if (parse_cmdline_args(argc, argv, &args) < 0)
		return -1;

	ctrlc_init();

	cp = setup_cproc(&args);
	if (!cp)
		return -1;

	if (!args.no_rc)
		process_rc_file(cp);

	/* Process commands */
	if (optind < argc) {
		while (optind < argc) {
			if (cproc_process_command(cp, argv[optind++]) < 0) {
				ret = -1;
				break;
			}
		}
	} else {
		cproc_reader_loop(cp);
	}

	cproc_destroy(cp);

	return ret;
}
