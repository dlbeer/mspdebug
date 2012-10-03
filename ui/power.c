/* MSPDebug - debugging tool MSP430 MCUs
 * Copyright (C) 2009-2012 Daniel Beer
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

#include "util.h"
#include "output.h"
#include "output_util.h"
#include "device.h"
#include "power.h"
#include "powerbuf.h"

static void print_header(powerbuf_t pb, unsigned int s)
{
	unsigned int length;
	const struct powerbuf_session *rec =
		powerbuf_session_info(pb, s, &length);

	printc("Session #%d: %s", s, ctime(&rec->wall_clock));
	printc("%d samples (spanning %.03f ms)\n",
		length, (double)(length * pb->interval_us) / 1000.0);
	printc("%.01f uA average (%.01f uAs total charge)\n",
		(double)rec->total_ua / (double)length,
		(double)(rec->total_ua * pb->interval_us) / 1000000.0);
}

static void dump_session_data(powerbuf_t pb, unsigned int s,
			      unsigned int gran)
{
	unsigned int length;
	const struct powerbuf_session *rec =
		powerbuf_session_info(pb, s, &length);
	unsigned int i;
	int idx;

	print_header(pb, s);
	printc("\n");

	printc("%15s %15s %-15s\n", "Time (us)", "Current (uA)", "MAB");
	printc("------------------------------------------------\n");

	idx = rec->start_index;

	for (i = 0; i + gran <= length; i += gran) {
		address_t mab = pb->mab[idx];
		unsigned long ua_tot = 0;
		char addr[128];
		int j;

		for (j = 0; j < gran; j++) {
			ua_tot += pb->current_ua[idx];
			idx = (idx + 1) % pb->max_samples;
		}

		print_address(mab, addr, sizeof(addr));
		printc("%15d %15.01f %s\n", i * pb->interval_us,
			((double)ua_tot) / (double)gran, addr);
	}

	printc("\n");
}

static int sc_info(powerbuf_t pb)
{
	int sess_num = powerbuf_num_sessions(pb);
	int i;

	printc("Sample granularity is %d us\n", pb->interval_us);
	printc("%d sessions:\n", sess_num);

	for (i = sess_num - 1; i >= 0; i--) {
		printc("\n");
		print_header(pb, i);
	}

	return 0;
}

static int sc_clear(powerbuf_t pb)
{
	powerbuf_clear(pb);
	return 0;
}

static int parse_granularity(powerbuf_t pb, char **arg, int *gran_out)
{
	const char *text = get_arg(arg);
	int request = 10000;
	int gran;

	if (text)
		request = atoi(text);

	if (request <= 0) {
		printc_err("power: invalid granularity: %d us\n", request);
		return -1;
	}

	gran = (request + (pb->interval_us / 2)) / pb->interval_us;

	if (gran <= 0)
		gran = 1;

	*gran_out = gran;
	return 0;
}

static int sc_all(powerbuf_t pb, char **arg)
{
	int i;
	int gran;

	if (parse_granularity(pb, arg, &gran) < 0)
		return -1;

	for (i = powerbuf_num_sessions(pb) - 1; i >= 0; i--)
		dump_session_data(pb, i, gran);

	return 0;
}

static int sc_session(powerbuf_t pb, char **arg)
{
	const char *sess_text = get_arg(arg);
	int sess;
	int gran;

	if (!sess_text) {
		printc_err("power: you must specify a session number\n");
		return -1;
	}

	sess = atoi(sess_text);
	if (sess < 0 || sess >= powerbuf_num_sessions(pb)) {
		printc_err("power: invalid session: %d\n", sess);
		return -1;
	}

	if (parse_granularity(pb, arg, &gran) < 0)
		return -1;

	dump_session_data(pb, sess, gran);
	return 0;
}

int cmd_power(char **arg)
{
	powerbuf_t pb = device_default->power_buf;
	char *subcmd = get_arg(arg);

	if (!pb) {
		printc_err("power: power profiling is not supported "
			   "by this device.\n");
		return -1;
	}

	if (!subcmd) {
		printc_err("power: need to specify a subcommand "
			   "(try \"help power\")\n");
		return -1;
	}

	if (!strcasecmp(subcmd, "info"))
		return sc_info(pb);
	if (!strcasecmp(subcmd, "clear"))
		return sc_clear(pb);
	if (!strcasecmp(subcmd, "all"))
		return sc_all(pb, arg);
	if (!strcasecmp(subcmd, "session"))
		return sc_session(pb, arg);

	printc_err("power: unknown subcommand: %s (try \"help power\")\n",
		   subcmd);
	return -1;
}
