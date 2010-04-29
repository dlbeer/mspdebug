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

#include "dis.h"
#include "device.h"
#include "binfile.h"
#include "stab.h"
#include "util.h"
#include "gdb.h"
#include "rtools.h"
#include "sym.h"
#include "devcmd.h"

static void usage(const char *progname)
{
	fprintf(stderr,
"Usage: %s [options] -R [-v voltage] [command ...]\n"
"       %s [options] -u <device> [-j] [-v voltage] [command ...]\n"
"       %s [options] -B <device> [command ...]\n"
"       %s [options] -s [command ...]\n"
"\n"
"    -R\n"
"        Open the first available RF2500 device on the USB bus.\n"
"    -u device\n"
"        Open the given tty device (MSP430 UIF compatible devices).\n"
"    -j\n"
"        Use JTAG, rather than spy-bi-wire (UIF devices only).\n"
"    -v voltage\n"
"        Set the supply voltage, in millivolts.\n"
"    -B device\n"
"        Debug the FET itself through the bootloader.\n"
"    -s\n"
"        Start in simulation mode.\n"
"    -n\n"
"        Do not read ~/.mspdebug on startup.\n"
"    -?\n"
"        Show this help text.\n"
"\n"
"By default, the first RF2500 device on the USB bus is opened.\n"
"\n"
"If commands are given, they will be executed. Otherwise, an interactive\n"
"command reader is started.\n",
		progname, progname, progname, progname);
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

#define MODE_RF2500             0x01
#define MODE_UIF                0x02
#define MODE_UIF_BSL            0x04
#define MODE_SIM                0x08

struct cmdline_args {
	const char      *uif_device;
	const char      *bsl_device;
	int             mode;
	int             want_jtag;
	int             no_rc;
	int             vcc_mv;
};

static int parse_cmdline_args(int argc, char **argv,
			      struct cmdline_args *args)
{
	int opt;

	while ((opt = getopt(argc, argv, "u:jv:B:sR?n")) >= 0)
		switch (opt) {
		case 'R':
			args->mode |= MODE_RF2500;
			break;

		case 'u':
			args->uif_device = optarg;
			args->mode |= MODE_UIF;
			break;

		case 'v':
			args->vcc_mv = atoi(optarg);
			break;

		case 'j':
			args->want_jtag = 1;
			break;

		case 'B':
			args->bsl_device = optarg;
			args->mode |= MODE_UIF_BSL;
			break;

		case 's':
			args->mode |= MODE_SIM;
			break;

		case 'n':
			args->no_rc = 1;
			break;

		case '?':
			usage(argv[0]);
			return 0;

		default:
			fprintf(stderr, "Invalid argument: %c\n"
				"Try -? for help.\n", opt);
			return -1;
		}

	/* Check for incompatible arguments */
	if (args->mode & (args->mode - 1)) {
		fprintf(stderr, "Multiple incompatible options specified.\n"
			"Try -? for help.\n");
		return -1;
	}

	if (!args->mode) {
		fprintf(stderr, "You need to specify an operating mode.\n"
			"Try -? for help.\n");
		return -1;
	}

	return 0;
}

const struct device *setup_device(const struct cmdline_args *args)
{
	const struct device *msp430_dev = NULL;
	const struct fet_transport *trans = NULL;

	/* Open a device */
	if (args->mode == MODE_SIM) {
		msp430_dev = sim_open();
	} else if (args->mode == MODE_UIF_BSL) {
		msp430_dev = bsl_open(args->bsl_device);
	} else if (args->mode == MODE_RF2500 || args->mode == MODE_UIF) {
		int flags = 0;

		/* Open the appropriate transport */
		if (args->mode == MODE_UIF) {
			trans = uif_open(args->uif_device);
		} else {
			trans = rf2500_open();
			flags |= FET_PROTO_RF2500;
		}

		if (!trans)
			return NULL;

		/* Then initialize the device */
		if (!args->want_jtag)
			flags |= FET_PROTO_SPYBIWIRE;

		msp430_dev = fet_open(trans, flags, args->vcc_mv);
	}

	if (!msp430_dev) {
		if (trans)
			trans->close();
		return NULL;
	}

	device_set(msp430_dev);
	return msp430_dev;
}

int main(int argc, char **argv)
{
	const struct device *msp430_dev;
	struct cmdline_args args = {0};
	cproc_t cp;
	int ret = 0;

	puts(
"MSPDebug version 0.6 - debugging tool for MSP430 MCUs\n"
"Copyright (C) 2009, 2010 Daniel Beer <daniel@tortek.co.nz>\n"
"This is free software; see the source for copying conditions.  There is NO\n"
"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n");

	args.vcc_mv = 3000;
	if (parse_cmdline_args(argc, argv, &args) < 0)
		return -1;

	ctrlc_init();

	if (stab_init() < 0)
		return -1;

	msp430_dev = setup_device(&args);
	if (!msp430_dev) {
		stab_exit();
		return -1;
	}

	cp = cproc_new();
	if (!cp ||
	    sym_register(cp) < 0 ||
	    devcmd_register(cp) < 0 ||
	    gdb_register(cp) < 0 ||
	    rtools_register(cp) < 0) {
		perror("couldn't set up command parser");
		if (cp)
			cproc_destroy(cp);
		msp430_dev->close();
		stab_exit();
		return -1;
	}

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
	msp430_dev->close();
	stab_exit();

	return ret;
}
