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
#include "parse.h"
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

static void process_rc_file(void)
{
	const char *home = getenv("HOME");
	char text[256];

	if (!home)
		return;

	snprintf(text, sizeof(text), "%s/.mspdebug", home);
	process_file(text);
}

#define MODE_RF2500             0x01
#define MODE_UIF                0x02
#define MODE_UIF_BSL            0x04
#define MODE_SIM                0x08

int main(int argc, char **argv)
{
	const struct fet_transport *trans;
	const char *uif_device = NULL;
	const char *bsl_device = NULL;
	const struct device *msp430_dev = NULL;
	int opt;
	int no_rc = 0;
	int ret = 0;
	int flags = 0;
	int want_jtag = 0;
	int vcc_mv = 3000;
	int mode = 0;

	puts(
"MSPDebug version 0.6 - debugging tool for MSP430 MCUs\n"
"Copyright (C) 2009, 2010 Daniel Beer <daniel@tortek.co.nz>\n"
"This is free software; see the source for copying conditions.  There is NO\n"
"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n");

	/* Parse arguments */
	while ((opt = getopt(argc, argv, "u:jv:B:sR?n")) >= 0)
		switch (opt) {
		case 'R':
			mode |= MODE_RF2500;
			break;

		case 'u':
			uif_device = optarg;
			mode |= MODE_UIF;
			break;

		case 'v':
			vcc_mv = atoi(optarg);
			break;

		case 'j':
			want_jtag = 1;
			break;

		case 'B':
			bsl_device = optarg;
			mode |= MODE_UIF_BSL;
			break;

		case 's':
			mode |= MODE_SIM;
			break;

		case 'n':
			no_rc = 1;
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
	if (mode & (mode - 1)) {
		fprintf(stderr, "Multiple incompatible options specified.\n"
			"Try -? for help.\n");
		return -1;
	}

	if (!mode) {
		fprintf(stderr, "You need to specify an operating mode.\n"
			"Try -? for help.\n");
		return -1;
	}

	if (stab_init() < 0)
		return -1;

	/* Open a device */
	if (mode == MODE_SIM) {
		msp430_dev = sim_open();
	} else if (mode == MODE_UIF_BSL) {
		msp430_dev = bsl_open(bsl_device);
	} else if (mode == MODE_RF2500 || mode == MODE_UIF) {
		/* Open the appropriate transport */
		if (mode == MODE_UIF) {
			trans = uif_open(uif_device);
		} else {
			trans = rf2500_open();
			flags |= FET_PROTO_RF2500;
		}

		if (!trans)
			return -1;

		/* Then initialize the device */
		if (!want_jtag)
			flags |= FET_PROTO_SPYBIWIRE;

		msp430_dev = fet_open(trans, flags, vcc_mv);
	}

	if (!msp430_dev) {
		stab_exit();
		return -1;
	}

	/* Initialise parsing */
	device_set(msp430_dev);
	ctrlc_init();
	parse_init();
	sym_init();
	devcmd_init();
	gdb_init();
	rtools_init();

	if (!no_rc)
		process_rc_file();

	/* Process commands */
	if (optind < argc) {
		while (optind < argc) {
			if (process_command(argv[optind++], 0) < 0) {
				ret = -1;
				break;
			}
		}
	} else {
		reader_loop();
	}

	msp430_dev->close();
	stab_exit();

	return ret;
}
