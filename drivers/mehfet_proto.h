/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2021 sys64738@disroot.org
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

#ifndef MEHFET_PROTO_H_
#define MEHFET_PROTO_H_

#include "transport.h"

#define MEHFET_PROTO_VER 0x0001
#define MEHFET_PROTO_VER_MIN_SUPPORTED 0x0001

enum mehfet_cmd {
	mehfet_info          = 0x01,
	mehfet_status        = 0x02,
	mehfet_connect       = 0x03,
	mehfet_disconnect    = 0x04,
	mehfet_delay         = 0x05,
	mehfet_set_clkspeed  = 0x06,
	mehfet_get_old_lines = 0x07,
	mehfet_tdio_seq      = 0x08,
	mehfet_tms_seq       = 0x09,
	mehfet_tclk_edge     = 0x0a,
	mehfet_tclk_burst    = 0x0b,
	mehfet_reset_tap     = 0x0c,
	mehfet_irshift       = 0x0d,
	mehfet_drshift       = 0x0e,
};

enum mehfet_caps {
	mehfet_cap_jtag_noentry  = 1<<0,
	mehfet_cap_jtag_entryseq = 1<<1,
	mehfet_cap_sbw_entryseq  = 1<<2,

	mehfet_cap_has_reset_tap = 1<< 8,
	mehfet_cap_has_irshift   = 1<< 9,
	mehfet_cap_has_drshift   = 1<<10,
	mehfet_cap_has_loop      = 1<<11,
};

enum mehfet_conn {
	mehfet_conn_none = 0,
	mehfet_conn_auto = 0,

	mehfet_conn_jtag_noentry  = 1,
	mehfet_conn_jtag_entryseq = 2,
	mehfet_conn_sbw_entryseq  = 3,

	mehfet_conn_typemask = 0x7f,
	mehfet_conn_nrstmask = 0x80
};

enum mehfet_lines {
	mehfet_line_tclk = 1<<0,
	mehfet_line_tms  = 1<<1,
	mehfet_line_tdi  = 1<<2
};

enum mehfet_resettap_flags {
	mehfet_rsttap_do_reset  = 1<<0, // reset TAP to run-test/idle state
	mehfet_rsttap_fuse_do   = 1<<1, // perform fuse check procedure (TMS pulses) on target
	mehfet_rsttap_fuse_read = 1<<2, // check whether the JTAG fuse has been blown
};
enum mehfet_resettap_status {
	mehfet_rsttap_fuse_blown = 0x80
};

struct mehfet_info {
	char* devicename;
	enum mehfet_caps caps;
	uint32_t packet_buf_size;
	uint16_t proto_version;
};


int mehfet_cmd_info(transport_t t, struct mehfet_info* info);
int mehfet_cmd_status(transport_t t, enum mehfet_conn* stat);
int mehfet_cmd_connect(transport_t t, enum mehfet_conn conn);
int mehfet_cmd_disconnect(transport_t t);
int mehfet_cmd_delay(transport_t t, bool us, bool exact, uint32_t time);
int mehfet_cmd_set_clkspeed(transport_t t, bool fast);
int mehfet_cmd_get_old_lines(transport_t t, enum mehfet_lines* lines);

int mehfet_cmd_tdio_seq(transport_t t, uint32_t nbits, bool tms, const uint8_t* tdi, uint8_t* tdo);
int mehfet_cmd_tms_seq(transport_t t, uint32_t nbits, bool tdi, const uint8_t* tms);
int mehfet_cmd_tclk_edge(transport_t t, bool newtclk);
int mehfet_cmd_tclk_burst(transport_t t, uint32_t ncyc);

int mehfet_cmd_reset_tap(transport_t t, enum mehfet_resettap_flags flags, enum mehfet_resettap_status* stat);
int mehfet_cmd_irshift(transport_t t, uint8_t newir, uint8_t* oldir);
int mehfet_cmd_drshift(transport_t t, uint32_t nbits, const uint8_t* newdr, uint8_t* olddr);

#endif

