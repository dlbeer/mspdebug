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

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "mehfet_xport.h"
#include "output.h"
#include "util.h"

#include "mehfet_proto.h"

// TODO: replace '64' by transport buf size

int mehfet_cmd_info(transport_t t, struct mehfet_info* info) {
	if (!info) return -1;

	uint8_t buf[64];
	int r;

	r = mehfet_send_raw(t, mehfet_info, 0, NULL);
	if (r < 0) return r;

	uint8_t stat = 0;
	int len = sizeof buf;
	r = mehfet_recv_raw(t, &stat, &len, buf);
	if (r < 0) return r;
	r = mehfet_err_on_stat("Info", stat, len, buf);
	if (r < 0) return r;

	if (len < 8) {
		printc_err("mehfet: Info response too short: %d < 8\n", len);
		return -1;
	}

	info->caps = buf[0] | ((uint32_t)buf[1] << 8)
		| ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
	info->proto_version = buf[4] | ((uint16_t)buf[5] << 8);
	info->packet_buf_size = 1u << buf[6];
	// buf[7]: reserved

	if (len > 9) { // len includes the null terminator
		char* str = calloc(len - 8, 1);
		memcpy(str, &buf[8], len - 8);
		str[len - 9] = 0;

		info->devicename = str;
	}

#ifdef DEBUG_MEHFET_PROTO_DRIVER
	printc_dbg("mehfet: Info(): '%s' caps=0x%x, protover=0x%04x, pktsize=0x%x\n",
			info->devicename ? info->devicename : "<none>",
			info->caps, info->proto_version, info->packet_buf_size);
#endif

	return 0;
}
int mehfet_cmd_status(transport_t t, enum mehfet_conn* ret) {
	if (!ret) return -1;

	uint8_t buf[64];
	int r;

	r = mehfet_send_raw(t, mehfet_status, 0, NULL);
	if (r < 0) return r;

	uint8_t stat = 0;
	int len = sizeof buf;
	r = mehfet_recv_raw(t, &stat, &len, buf);
	if (r < 0) return r;
	r = mehfet_err_on_stat("Status", stat, len, buf);
	if (r < 0) return r;

	if (len != 1) {
		printc_err("mehfet: Status response wrong length: %d != 1\n", len);
		return -1;
	}

	*ret = buf[0];

#ifdef DEBUG_MEHFET_PROTO_DRIVER
	printc_dbg("mehfet: Status(): %hhu\n", *ret);
#endif

	return 0;
}
int mehfet_cmd_connect(transport_t t, enum mehfet_conn conn) {
	uint8_t buf[64];
	int r;

	buf[0] = conn;
	r = mehfet_send_raw(t, mehfet_connect, 1, buf);
	if (r < 0) return r;

	uint8_t stat = 0;
	int len = sizeof buf;
	r = mehfet_recv_raw(t, &stat, &len, buf);
	if (r < 0) return r;
	r = mehfet_err_on_stat("Connect", stat, len, buf);
	if (r < 0) return r;

	if (len != 0) {
		printc_err("mehfet: Connect response wrong length: %d != 0\n", len);
		return -1;
	}

#ifdef DEBUG_MEHFET_PROTO_DRIVER
	printc_dbg("mehfet: Connect(0x%x)\n", conn);
#endif

	return 0;
}
int mehfet_cmd_disconnect(transport_t t) {
	uint8_t buf[64];
	int r;

	r = mehfet_send_raw(t, mehfet_disconnect, 0, NULL);
	if (r < 0) return r;

	uint8_t stat = 0;
	int len = sizeof buf;
	r = mehfet_recv_raw(t, &stat, &len, buf);
	if (r < 0) return r;
	r = mehfet_err_on_stat("Disconnect", stat, len, buf);
	if (r < 0) return r;

	if (len != 0) {
		printc_err("mehfet: Disconnect response wrong length: %d != 0\n", len);
		return -1;
	}

#ifdef DEBUG_MEHFET_PROTO_DRIVER
	printc_dbg("mehfet: Disconnect()\n");
#endif

	return 0;
}
int mehfet_cmd_delay(transport_t t, bool us, bool exact, uint32_t time) {
	if (time >= (1u << 30)) return -1; // too large

	uint8_t buf[64];
	int r;

	for (size_t i = 0; i < 4; ++i) buf[i] = (time >> (i*8)) & 0xff;
	if (us) buf[3] |= 0x40;
	if (exact) buf[3] |= 0x80;

	r = mehfet_send_raw(t, mehfet_delay, 4, buf);
	if (r < 0) return r;

	uint8_t stat = 0;
	int len = sizeof buf;
	r = mehfet_recv_raw(t, &stat, &len, buf);
	if (r < 0) return r;
	r = mehfet_err_on_stat("Delay", stat, len, buf);
	if (r < 0) return r;

	if (len != 0) {
		printc_err("mehfet: Delay response wrong length: %d != 0\n", len);
		return -1;
	}

#ifdef DEBUG_MEHFET_PROTO_DRIVER
	printc_dbg("mehfet: Delay(us=%c exact=%c time=%u)\n",
			us?'t':'f', exact?'t':'f', time);
#endif

	return 0;
}
int mehfet_cmd_set_clkspeed(transport_t t, bool fast) {
	uint8_t buf[64];
	int r;

	buf[0] = fast ? 0xff : 0;
	r = mehfet_send_raw(t, mehfet_set_clkspeed, 1, buf);
	if (r < 0) return r;

	uint8_t stat = 0;
	int len = sizeof buf;
	r = mehfet_recv_raw(t, &stat, &len, buf);
	if (r < 0) return r;
	r = mehfet_err_on_stat("SetClkSpeed", stat, len, buf);
	if (r < 0) return r;

	if (len != 0) {
		printc_err("mehfet: SetClkSpeed response wrong length: %d != 0\n", len);
		return -1;
	}

#ifdef DEBUG_MEHFET_PROTO_DRIVER
	printc_dbg("mehfet: SetClkSpeed(%s)\n", fast?"fast":"slow");
#endif

	return 0;
}
int mehfet_cmd_get_old_lines(transport_t t, enum mehfet_lines* lines) {
	if (!lines) return -1;

	uint8_t buf[64];
	int r;

	r = mehfet_send_raw(t, mehfet_get_old_lines, 0, NULL);
	if (r < 0) return r;

	uint8_t stat = 0;
	int len = sizeof buf;
	r = mehfet_recv_raw(t, &stat, &len, buf);
	if (r < 0) return r;
	r = mehfet_err_on_stat("GetOldLines", stat, len, buf);
	if (r < 0) return r;

	if (len != 1) {
		printc_err("mehfet: GetOldLines response wrong length: %d != 1\n", len);
		return -1;
	}

	*lines = buf[0];

#ifdef DEBUG_MEHFET_PROTO_DRIVER
	printc_dbg("mehfet: GetOldLines(): 0x%x\n", *lines);
#endif

	return 0;
}

int mehfet_cmd_tdio_seq(transport_t t, uint32_t nbits, bool tms, const uint8_t* tdi, uint8_t* tdo) {
	if (!tdi || !tdo || !nbits) return -1;

	int r;
	uint32_t nbytes = (nbits + 7) >> 3;
	uint8_t buf[(nbytes + 5) < 64 ? 64 : (nbytes + 5)]; // need min for recv

	for (size_t i = 0; i < 4; ++i) buf[i] = (nbits >> (i*8)) & 0xff;
	buf[4] = tms ? 0xff : 0;
	memcpy(&buf[5], tdi, nbytes);

	r = mehfet_send_raw(t, mehfet_tdio_seq, nbytes + 5, buf);
	if (r < 0) return r;

	uint8_t stat = 0;
	int len = sizeof buf; // is a max len
	r = mehfet_recv_raw(t, &stat, &len, buf);
	if (r < 0) return r;
	r = mehfet_err_on_stat("TdioSequence", stat, len, buf);
	if (r < 0) return r;

	if (len != nbytes) {
		printc_err("mehfet: TdioSequence response wrong length: %d != %d\n",
				len, nbytes);
		return -1;
	}

	memcpy(tdo, buf, nbytes);

#ifdef DEBUG_MEHFET_PROTO_DRIVER
	printc_dbg("mehfet: TdioSequence(%u, TMS=%c):\n", nbits, tms?'1':'0');
	debug_hexdump("\tTDI", tdi, nbytes);
	debug_hexdump("\tTDO", tdo, nbytes);
#endif

	return 0;
}
int mehfet_cmd_tms_seq(transport_t t, uint32_t nbits, bool tdi, const uint8_t* tms) {
	if (!tms || !nbits) return -1;

	int r;
	uint32_t nbytes = (nbits + 7) >> 3;
	uint8_t buf[(nbytes + 5) < 64 ? 64 : (nbytes + 5)]; // need min for recv

	for (size_t i = 0; i < 4; ++i) buf[i] = (nbits >> (i*8)) & 0xff;
	buf[4] = tdi ? 0xff : 0;
	memcpy(&buf[5], tms, nbytes);

	r = mehfet_send_raw(t, mehfet_tdio_seq, sizeof buf, buf);
	if (r < 0) return r;

	uint8_t stat = 0;
	int len = sizeof buf; // is a max len
	r = mehfet_recv_raw(t, &stat, &len, buf);
	if (r < 0) return r;
	r = mehfet_err_on_stat("TmsSequence", stat, len, buf);
	if (r < 0) return r;

	if (len != 0) {
		printc_err("mehfet: TmsSequence response wrong length: %d != 0\n", len);
		return -1;
	}

#ifdef DEBUG_MEHFET_PROTO_DRIVER
	printc_dbg("mehfet: TmsSequence(%u, TDI=%c):\n", nbits, tdi?'1':'0');
	debug_hexdump("\tTMS", tms, nbytes);
#endif

	return 0;
}
int mehfet_cmd_tclk_edge(transport_t t, bool newtclk) {
	uint8_t buf[64];
	int r;

	buf[0] = newtclk ? 0xff : 0;
	r = mehfet_send_raw(t, mehfet_tclk_edge, 1, buf);
	if (r < 0) return r;

	uint8_t stat = 0;
	int len = sizeof buf;
	r = mehfet_recv_raw(t, &stat, &len, buf);
	if (r < 0) return r;
	r = mehfet_err_on_stat("TclkEdge", stat, len, buf);
	if (r < 0) return r;

	if (len != 0) {
		printc_err("mehfet: TclkEdge response wrong length: %d != 0\n", len);
		return -1;
	}

#ifdef DEBUG_MEHFET_PROTO_DRIVER
	printc_dbg("mehfet: TclkEdge(TCLK=%c)\n", newtclk?'H':'L');
#endif

	return 0;
}
int mehfet_cmd_tclk_burst(transport_t t, uint32_t ncyc) {
	uint8_t buf[64];
	int r;

	for (size_t i = 0; i < 4; ++i) buf[i] = (ncyc >> (i*8)) & 0xff;
	r = mehfet_send_raw(t, mehfet_tclk_burst, 4, buf);
	if (r < 0) return r;

	uint8_t stat = 0;
	int len = sizeof buf;
	r = mehfet_recv_raw(t, &stat, &len, buf);
	if (r < 0) return r;
	r = mehfet_err_on_stat("TclkBurst", stat, len, buf);
	if (r < 0) return r;

	if (len != 0) {
		printc_err("mehfet: TclkBurst response wrong length: %d != 0\n", len);
		return -1;
	}

#ifdef DEBUG_MEHFET_PROTO_DRIVER
	printc_dbg("mehfet: TclkBurst(ncyc=%u)\n", ncyc);
#endif

	return 0;
}

int mehfet_cmd_reset_tap(transport_t t, enum mehfet_resettap_flags flags,
		enum mehfet_resettap_status* tstat) {
	if (!tstat) return -1;

	uint8_t buf[64];
	int r;

	buf[0] = flags;
	r = mehfet_send_raw(t, mehfet_reset_tap, 1, buf);
	if (r < 0) return r;

	uint8_t stat = 0;
	int len = sizeof buf;
	r = mehfet_recv_raw(t, &stat, &len, buf);
	if (r < 0) return r;
	r = mehfet_err_on_stat("ResetTAP", stat, len, buf);
	if (r < 0) return r;

	if (len != 1) {
		printc_err("mehfet: ResetTAP response wrong length: %d != 1\n", len);
		return -1;
	}

	*tstat = buf[0];

#ifdef DEBUG_MEHFET_PROTO_DRIVER
	printc_dbg("mehfet: ResetTAP(flags=0x%x) = 0x%x\n", flags, *tstat);
#endif

	return 0;
}

static uint8_t bitswap_nyb(uint8_t in) {
	return ((in >> 3) & 1) | ((in >> 1) & 2) | ((in << 1) & 4) | ((in << 3) & 8);
}
static uint8_t bitswap(uint8_t in) {
	return (bitswap_nyb(in&0xf) << 4) | bitswap_nyb(in>>4);
}

int mehfet_cmd_irshift(transport_t t, uint8_t newir, uint8_t* oldir) {
	if (!oldir) return -1;

	// NOTE: jtaglib.c uses bitswapped IR values, while MehFET uses values from
	//       the SLAU320 PDF, so we need to perform a bitswap here
	newir = bitswap(newir);

	uint8_t buf[64];
	int r;

	buf[0] = newir;
	r = mehfet_send_raw(t, mehfet_irshift, 1, buf);
	if (r < 0) return r;

	uint8_t stat = 0;
	int len = sizeof buf;
	r = mehfet_recv_raw(t, &stat, &len, buf);
	if (r < 0) return r;
	r = mehfet_err_on_stat("IRshift", stat, len, buf);
	if (r < 0) return r;

	if (len != 1) {
		printc_err("mehfet: IRshift response wrong length: %d != 1\n", len);
		return -1;
	}

	*oldir = buf[0];

#ifdef DEBUG_MEHFET_PROTO_DRIVER
	printc_dbg("mehfet: IRshift(new=0x%02x) = 0x%02x\n", newir, *oldir);
#endif

	return 0;
}
int mehfet_cmd_drshift(transport_t t, uint32_t nbits, const uint8_t* newdr, uint8_t* olddr) {
	if (!newdr || !olddr) return -1;

	uint32_t nbytes = (nbits + 7) >> 3;
	uint8_t buf[(nbytes + 4) < 64 ? 64 : (nbytes + 4)];
	int r;

	for (size_t i = 0; i < 4; ++i) buf[i] = (nbits >> (i * 8)) & 0xff;
	memcpy(&buf[4], newdr, nbytes);
	r = mehfet_send_raw(t, mehfet_drshift, nbytes + 4, buf);
	if (r < 0) return r;

	uint8_t stat = 0;
	int len = sizeof buf;
	r = mehfet_recv_raw(t, &stat, &len, buf);
	if (r < 0) return r;
	r = mehfet_err_on_stat("DRshift", stat, len, buf);
	if (r < 0) return r;

	if (len != (int)nbytes) {
		printc_err("mehfet: DRshift response wrong length: %d != %u\n", len, nbytes);
		return -1;
	}

	memcpy(olddr, buf, nbytes);

#ifdef DEBUG_MEHFET_PROTO_DRIVER
	printc_dbg("mehfet: DRshift(nbits=%u):\n", nbits);
	debug_hexdump("\tin ", newdr, nbytes);
	debug_hexdump("\tout", olddr, nbytes);
#endif

	return 0;
}

