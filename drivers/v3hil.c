/* MSPDebug - debugging tool for MSP430 MCUs
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

#include <stdbool.h>
#include <string.h>
#include "bytes.h"
#include "v3hil.h"
#include "dis.h"
#include "output.h"
#include "opdb.h"

#ifdef DEBUG_V3HIL
#define dbg_printc(fmt, ...) printc_dbg("v3hil: " fmt, ##__VA_ARGS__)
#else
#define dbg_printc(fmt, ...) do{}while(0)
#endif

/* HAL function IDs */
typedef enum {
	HAL_PROTO_FID_INIT			= 0x01,
	HAL_PROTO_FID_SET_VCC			= 0x02,
	HAL_PROTO_FID_GET_VCC			= 0x03,
	HAL_PROTO_FID_START_JTAG		= 0x04,
	HAL_PROTO_FID_START_JTAG_ACT_CODE	= 0x05,
	HAL_PROTO_FID_STOP_JTAG			= 0x06,
	HAL_PROTO_FID_CONFIGURE			= 0x07,
	HAL_PROTO_FID_GET_FUSES			= 0x08,
	HAL_PROTO_FID_BLOW_FUSE			= 0x09,
	HAL_PROTO_FID_WAIT_FOR_EEM		= 0x0a,
	HAL_PROTO_FID_BIT_SEQUENCE		= 0x0b,
	HAL_PROTO_FID_GET_JTAG_ID		= 0x0c,
	HAL_PROTO_FID_SET_DEVICE_CHAIN_INFO	= 0x0d,
	HAL_PROTO_FID_SET_CHAIN_CONFIGURATION	= 0x0e,
	HAL_PROTO_FID_GET_NUM_DEVICES		= 0x0f,
	HAL_PROTO_FID_GET_INTERFACE_MODE	= 0x10,
	HAL_PROTO_FID_GET_DEVICE_ID_PTR		= 0x11,
	HAL_PROTO_FID_SJ_ASSERT_POR_SC		,
	HAL_PROTO_FID_SJ_CONDITIONAL_SC		,
	HAL_PROTO_FID_RC_RELEASE_JTAG		,
	HAL_PROTO_FID_READ_MEM_BYTES		,
	HAL_PROTO_FID_READ_MEM_WORDS		,
	HAL_PROTO_FID_READ_MEM_QUICK		,
	HAL_PROTO_FID_WRITE_MEM_BYTES		,
	HAL_PROTO_FID_WRITE_MEM_WORDS		,
	HAL_PROTO_FID_EEM_DX			,
	HAL_PROTO_FID_EEM_DX_AFE2XX		,
	HAL_PROTO_FID_SINGLE_STEP		,
	HAL_PROTO_FID_READ_ALL_CPU_REGS		,
	HAL_PROTO_FID_WRITE_ALL_CPU_REGS	,
	HAL_PROTO_FID_PSA			,
	HAL_PROTO_FID_EXECUTE_FUNCLET		, /* 0x20 */
	HAL_PROTO_FID_EXECUTE_FUNCLET_JTAG	,
	HAL_PROTO_FID_GET_DCO_FREQUENCY		,
	HAL_PROTO_FID_GET_DCO_FREQUENCY_JTAG	,
	HAL_PROTO_FID_GET_FLL_FREQUENCY		,
	HAL_PROTO_FID_GET_FLL_FREQUENCY_JTAG	,
	HAL_PROTO_FID_WAIT_FOR_STORAGE		,
	HAL_PROTO_FID_SJ_ASSERT_POR_SC_X	,
	HAL_PROTO_FID_SJ_CONDITIONAL_SC_X	,
	HAL_PROTO_FID_RC_RELEASE_JTAG_X		,
	HAL_PROTO_FID_READ_MEM_BYTES_X		,
	HAL_PROTO_FID_READ_MEM_WORDS_X		,
	HAL_PROTO_FID_READ_MEM_QUICK_X		,
	HAL_PROTO_FID_WRITE_MEM_BYTES_X		,
	HAL_PROTO_FID_WRITE_MEM_WORDS_X		,
	HAL_PROTO_FID_EEM_DX_X			,
	HAL_PROTO_FID_SINGLE_STEP_X		,
	HAL_PROTO_FID_READ_ALL_CPU_REGS_X	,
	HAL_PROTO_FID_WRITE_ALL_CPU_REGS_X	, /* 0x30 */
	HAL_PROTO_FID_PSA_X			,
	HAL_PROTO_FID_EXECUTE_FUNCLET_X		,
	HAL_PROTO_FID_GET_DCO_FREQUENCY_X	,
	HAL_PROTO_FID_GET_FLL_FREQUENCY_X	,
	HAL_PROTO_FID_WAIT_FOR_STORAGE_X	,
	HAL_PROTO_FID_BLOW_FUSE_XV2		,
	HAL_PROTO_FID_BLOW_FUSE_FRAM		,
	HAL_PROTO_FID_SJ_ASSERT_POR_SC_XV2	,
	HAL_PROTO_FID_SJ_CONDITIONAL_SC_XV2	,
	HAL_PROTO_FID_RC_RELEASE_JTAG_XV2	,
	HAL_PROTO_FID_READ_MEM_WORDS_XV2	,
	HAL_PROTO_FID_READ_MEM_QUICK_XV2	,
	HAL_PROTO_FID_WRITE_MEM_WORDS_XV2	,
	HAL_PROTO_FID_EEM_DX_XV2		,
	HAL_PROTO_FID_SINGLE_STEP_XV2		, /* 0x40 */
	HAL_PROTO_FID_READ_ALL_CPU_REGS_XV2	,
	HAL_PROTO_FID_WRITE_ALL_CPU_REGS_XV2	,
	HAL_PROTO_FID_PSA_XV2			,
	HAL_PROTO_FID_EXECUTE_FUNCLET_XV2	,
	HAL_PROTO_FID_UNLOCK_DEVICE_XV2		,
	HAL_PROTO_FID_MAGIC_PATTERN		,
	HAL_PROTO_FID_UNLOCK_C092		,
	HAL_PROTO_FID_HIL_COMMAND		,
	HAL_PROTO_FID_POLL_JSTATE_REG		,
	HAL_PROTO_FID_POLL_JSTATE_REG_FR57XX	,
	HAL_PROTO_FID_IS_JTAG_FUSE_BLOWN	,
	HAL_PROTO_FID_RESET_XV2			,
	HAL_PROTO_FID_WRITE_FRAM_QUICK_XV2	,
	HAL_PROTO_FID_SEND_JTAG_MAILBOX_XV2	,
	HAL_PROTO_FID_SINGLE_STEP_JSTATE_XV2	,
	HAL_PROTO_FID_POLL_JSTATE_REG_ET8	,
	HAL_PROTO_FID_RESET_STATIC_GLOBAL_VARS	, /* 0x50 */
	HAL_PROTO_FID_RESET_430I		,
	HAL_PROTO_FID_POLL_JSTATE_REG_430I	,
	HAL_PROTO_FID_POLL_JSTATE_REG_20	,
	HAL_PROTO_FID_SWITCH_MOSFET		,
	HAL_PROTO_FID_RESET_L092		,
	HAL_PROTO_FID_DUMMY_MACRO		,
	HAL_PROTO_FID_RESET_5438XV2		,
	HAL_PROTO_FID_LEA_SYNC_COND		,
	HAL_PROTO_FID_GET_JTAG_ID_CODE_ARM	,
	HAL_PROTO_FID_SCAN_AP_ARM		,
	HAL_PROTO_FID_MEM_AP_TRANSACTION_ARM	,
	HAL_PROTO_FID_READ_ALL_CPU_REGS_ARM	,
	HAL_PROTO_FID_WRITE_ALL_CPU_REGS_ARM	,
	HAL_PROTO_FID_ENABLE_DEBUG_ARM		,
	HAL_PROTO_FID_DISABLE_DEBUG_ARM		,
	HAL_PROTO_FID_RUN_ARM			, /* 0x60 */
	HAL_PROTO_FID_HALT_ARM			,
	HAL_PROTO_FID_RESET_ARM			,
	HAL_PROTO_FID_SINGLE_STEP_ARM		,
	HAL_PROTO_FID_WAIT_FOR_DEBUG_HALT_ARM	,
	HAL_PROTO_FID_MEM_AP_TRANSACTION_ARM_SWD,
	HAL_PROTO_FID_GET_ITF_MODE_ARM		,
	HAL_PROTO_FID_POLL_DSTATE_PCREG_ET	,
	HAL_PROTO_FID_GET_CPU_ID_ARM		,
	HAL_PROTO_FID_CHECK_DAP_LOCK_ARM	,
	HAL_PROTO_FID_UNLOCK_DAP		,
	HAL_PROTO_FID_USS_SYNC_COND		, /* 0x6b */
} hal_proto_fid_t;

/* Argument types for HAL_PROTO_FID_CONFIGURE */
typedef enum {
	HAL_PROTO_CONFIG_ENHANCED_PSA			= 0x01,
	HAL_PROTO_CONFIG_PSA_TCKL_HIGH			= 0x02,
	HAL_PROTO_CONFIG_DEFAULT_CLK_CONTROL		= 0x03,
	HAL_PROTO_CONFIG_POWER_TESTREG_MASK		= 0x04,
	HAL_PROTO_CONFIG_TESTREG_ENABLE_LPMX5		= 0x05,
	HAL_PROTO_CONFIG_TESTREG_DISABLE_LPMX5		= 0x06,
	HAL_PROTO_CONFIG_POWER_TESTREG3V_MASK		= 0x07,
	HAL_PROTO_CONFIG_TESTREG3V_ENABLE_LPMX5		= 0x08,
	HAL_PROTO_CONFIG_TESTREG3V_DISABLE_LPMX5	= 0x09,
	HAL_PROTO_CONFIG_CLK_CONTROL_TYPE		= 0x0a,
	HAL_PROTO_CONFIG_JTAG_SPEED			= 0x0b,
	HAL_PROTO_CONFIG_SFLLDEH			= 0x0c,
	HAL_PROTO_CONFIG_NO_BSL				= 0x0d,
	HAL_PROTO_CONFIG_ALT_ROM_ADDR_FOR_CPU_READ	= 0x0e,
	HAL_PROTO_CONFIG_ASSERT_BSL_VALID_BIT		= 0x0f,
	HAL_PROTO_CONFIG_POWER_TESTREG_DEFAULT		= 0x10,
	HAL_PROTO_CONFIG_POWER_TESTREGV3_DEFAULT	= 0x11,
	HAL_PROTO_CONFIG_WDT_ADDRESS_5XX		= 0x12,
	HAL_PROTO_CONFIG_SCS_BASE_ADDRESS		= 0x13,
	HAL_PROTO_CONFIG_FPB_BASE_ADDRESS		= 0x14,
	HAL_PROTO_CONFIG_INTERRUPT_OPTIONS		= 0x15,
	HAL_PROTO_CONFIG_ULP_MSP432			= 0x16,
	HAL_PROTO_CONFIG_JTAG_LOCK_5XX			= 0x17,
} hal_proto_config_t;

static hal_proto_fid_t map_ver(const struct v3hil *h, hal_proto_fid_t src)
{
	hal_proto_fid_t dst;

	if (h->proto_ver < 0x0300 && src > HAL_PROTO_FID_GET_DEVICE_ID_PTR) {
		dst = src - 1;
	} else {
		dst = src;
	}

	dbg_printc("map ver: %02x -> %02x\n", src, dst);
	return dst;
}

static hal_proto_fid_t map_fid(const struct v3hil *h, hal_proto_fid_t src)
{
	hal_proto_fid_t dst = h->chip->v3_functions[src];

	if (dst == 0) {
		dst = src;
	}

	dbg_printc("map fid: %02x -> %02x\n", src, dst);
	return map_ver(h, dst);
}

void v3hil_init(struct v3hil *h, transport_t trans,
		hal_proto_flags_t flags)
{
	memset(h, 0, sizeof(*h));
	hal_proto_init(&h->hal, trans, flags);
}

int v3hil_set_vcc(struct v3hil *h, int vcc_mv)
{
	uint8_t data[2];

	w16le(data, vcc_mv);
	dbg_printc("Setting VCC...\n");
	return hal_proto_execute(&h->hal, map_ver(h, HAL_PROTO_FID_SET_VCC), data, 2);
}

int v3hil_comm_init(struct v3hil *h)
{
	const uint8_t ver_payload = 0;

	printc_dbg("Reset communications...\n");
	if (hal_proto_send(&h->hal, HAL_PROTO_TYPE_EXCEPTION, NULL, 0) < 0)
		return -1;

	if (hal_proto_execute(&h->hal, 0, &ver_payload, 1) < 0)
		return -1;
	if (h->hal.length < 8) {
		printc_err("warning: v3hil: short reply to version request\n");
	} else if (h->hal.length == 40) {
#ifdef DEBUG_V3HIL
		printc_dbg("v3hil: Version:");
		for (int i = 0; i < h->hal.length; i++)
			printc_dbg(" %02x", h->hal.payload[i]);
		printc_dbg("\n");
#endif
		const uint16_t sw_version = r32le(h->hal.payload + 0);
		const uint16_t sw_build = r32le(h->hal.payload + 2);
		const uint32_t hw_thing = r32le(h->hal.payload + 4);
		const uint32_t tool_id = r16le(h->hal.payload + 8);
		const uint16_t core_version = r16le(h->hal.payload + 10);
		const uint16_t hil_version = r16le(h->hal.payload + 12);
		const uint16_t dcdc_layer_version = r16le(h->hal.payload + 14);
		const uint16_t dcdc_mcu_version = r16le(h->hal.payload + 16);
		const uint16_t com_version = r16le(h->hal.payload + 18);

		const uint16_t hil_crc = r16le(h->hal.payload + 20);
		const uint16_t hal_crc = r16le(h->hal.payload + 22);
		const uint16_t dcdc_crc = r16le(h->hal.payload + 24);
		const uint16_t core_crc = r16le(h->hal.payload + 26);
		const uint16_t com_crc = r16le(h->hal.payload + 28);

		const uint16_t fpga_version = r16le(h->hal.payload + 30);
		const uint16_t n_rx_queues = r16le(h->hal.payload + 32);
		const uint16_t rx_queue_size = r16le(h->hal.payload + 34);

		const uint8_t major = (sw_version >> 14) + 1;
		const uint8_t minor = (sw_version >> 8) & 0x3f;
		const uint8_t patch = sw_version & 0xff;
		const uint8_t build = sw_build;

		printc_dbg("Version: %d.%d.%d.%d Core version: 0x%02x, HIL version: 0x%02x, HW: 0x%04x\n",
				major, minor, patch, build,
				core_version, hil_version, hw_thing);

		h->proto_ver = (major << 8) | minor;
	} else {
		const uint8_t major = h->hal.payload[1] >> 6;
		const uint8_t minor = h->hal.payload[1] & 0x3f;
		const uint8_t patch = h->hal.payload[0];
		const uint16_t flavour = r16le(h->hal.payload + 2);

		printc_dbg("Version: %d.%d.%d.%d, HW: 0x%04x\n",
			   major, minor, patch, flavour,
			   r32le(h->hal.payload + 4));

		h->proto_ver = (major << 8) | minor;
	}

	printc_dbg("Reset firmware...\n");
	if (hal_proto_execute(&h->hal,
			map_ver(h, HAL_PROTO_FID_RESET_STATIC_GLOBAL_VARS), NULL, 0) < 0)
		return -1;

	return 0;
}

int v3hil_start_jtag(struct v3hil *h, v3hil_jtag_type_t type)
{
	uint8_t data = type;
	uint8_t chain_id[2] = {0, 0};

	dbg_printc("Start JTAG...\n");
	if (hal_proto_execute(&h->hal, map_ver(h, HAL_PROTO_FID_START_JTAG),
			      &data, 1) < 0)
		return -1;

	if (!h->hal.length) {
		printc_err("v3hil: short reply\n");
		return -1;
	}

	if (!h->hal.payload[0]) {
		printc_err("v3hil: no devices present\n");
		return -1;
	}

	printc_dbg("Device count: %d\n", h->hal.payload[0]);
	return hal_proto_execute(&h->hal,
			map_ver(h, HAL_PROTO_FID_SET_DEVICE_CHAIN_INFO), chain_id, 2);
}

int v3hil_stop_jtag(struct v3hil *h)
{
	dbg_printc("Stop JTAG...\n");
	if (hal_proto_execute(&h->hal, map_ver(h, HAL_PROTO_FID_STOP_JTAG), NULL, 0) < 0)
		return -1;

	dbg_printc("Reset communications...\n");
	h->hal.ref_id = 0;
	if (hal_proto_send(&h->hal, HAL_PROTO_TYPE_EXCEPTION, NULL, 0) < 0) {
		h->hal.ref_id = 0;
		return -1;
	}
	h->hal.ref_id = 0;

	return 0;
}

int v3hil_sync(struct v3hil *h)
{
	uint8_t data[32], datalen = 21;

	h->cal.is_cal = 0;

	memset(data, 0, sizeof(data));
	data[0] = (h->jtag_id == 0x89) ? 0x20 : 0x5c; /* WDTCTL */
	data[1] = 0x01;
	data[2] = 0x80; /* WDTHOLD */
	data[3] = 0x5a; /* WDTPW */
	data[4] = h->jtag_id;

	/* ETW codes (?) */
	if (h->chip) {
		int i;

		for (i = 0; i < 16; i++) {
			dbg_printc("clock map %d = %02x -> %d\n",
					i, h->chip->clock_map[i].value, 20-i);
			data[16 + 4 - i] = h->chip->clock_map[i].value;
		}

		if (h->proto_ver >= 0x0308) {
			datalen = 21+16;

			for (i = 16; i < 32; i++) {
				dbg_printc("clock map2 %d = %02x -> %d\n",
						i, h->chip->clock_map[i].value, 16+4+16*2 - i);
				data[16+4+16*2 - i] = h->chip->clock_map[i].value;
			}
		}
	} else {
		data[5] = 1;
		data[15] = 40;
	}

	/* We can't use map_fid() because h->chip might be NULL -- this
	 * function will be called before identification is complete.
	 */
	hal_proto_fid_t cmdid = (h->jtag_id == 0x89/*0x90*/)
		? HAL_PROTO_FID_SJ_ASSERT_POR_SC
		: HAL_PROTO_FID_SJ_ASSERT_POR_SC_XV2;

	dbg_printc("Sync: assert POR\n");
	if (h->chip) {
		if (hal_proto_execute(&h->hal, map_fid(h, cmdid), data, datalen) < 0) {
			return -1;
		}
	} else {
		/* Need to do something for X/Xv2 devices, so try each in turn... */
		cmdid = map_ver(h, cmdid);
		if (hal_proto_execute(&h->hal, cmdid, data, datalen) < 0) {
			cmdid = map_ver(h, HAL_PROTO_FID_SJ_ASSERT_POR_SC_X);
			if (hal_proto_execute(&h->hal, cmdid, data, datalen) < 0) {
				cmdid = map_ver(h, HAL_PROTO_FID_SJ_ASSERT_POR_SC_XV2);
				if (hal_proto_execute(&h->hal, cmdid, data, datalen) < 0) {
					return -1;
				}
			}
		}
	}

#ifdef DEBUG_V3HIL
	printc_dbg("v3hil: POR result: (len %d) ", h->hal.length);
	for (int i = 0; i < h->hal.length; ++i) {
		printc_dbg("%02x%s", h->hal.payload[i],
			(i == h->hal.length - 1) ? "\n" : " ");
	}
#endif

	if (h->hal.length < 8) {
		printc_err("v3hil: short reply: %d\n", h->hal.length);
		return -1;
	}

	h->wdtctl = h->hal.payload[0];
	h->regs[MSP430_REG_PC] = r32le(h->hal.payload + 2);
	h->regs[MSP430_REG_SR] = r16le(h->hal.payload + 6);
	return 0;
}

int v3hil_read(struct v3hil *h, address_t addr,
	       uint8_t *mem, address_t size)
{
	const struct chipinfo_memory *m = NULL;
	uint8_t req[12];

	if (h->chip) {
		size = check_range(h->chip, addr, size, &m);
		if (!m) {
			memset(mem, 0x55, size);
			return size;
		}
	}

	w32le(req, addr);
	w32le(req + 4, (m->bits == 8) ? size : (size >> 1));
	w32le(req + 8, h->regs[MSP430_REG_PC]);

	dbg_printc("do read\n");
	if (hal_proto_execute(&h->hal,
		map_fid(h, (m->bits == 8) ? HAL_PROTO_FID_READ_MEM_BYTES :
					    HAL_PROTO_FID_READ_MEM_WORDS),
		req, 8) < 0)
		goto fail;

	if (h->hal.length < size) {
		printc_err("v3hil: short reply: %d\n", h->hal.length);
		goto fail;
	}

	memcpy(mem, h->hal.payload, size);
	return size;
fail:
	printc_err("v3hil: failed reading %d bytes from 0x%05x\n",
		   size, addr);
	return -1;
}

const struct chipinfo_memory *find_ram(const struct chipinfo *c)
{
	const struct chipinfo_memory *m;
	const struct chipinfo_memory *best = NULL;

	if (!c)
		goto fail;

	for (m = c->memory; m->name; m++) {
		if (m->type != CHIPINFO_MEMTYPE_RAM)
			continue;
		if (!best || m->size > best->size)
			best = m;
	}

	if (!best)
		goto fail;

	return best;

fail:
	printc_err("v3hil: can't find RAM region in chip database\n");
	return NULL;
}

static int calibrate_dco(struct v3hil *h, uint8_t max_bcs)
{
	const struct chipinfo_memory *ram = find_ram(h->chip);
	uint8_t data[6];
	uint8_t mem_write[16];

	if (!ram)
		goto fail;

	printc_dbg("Calibrate DCO...\n");

	w16le(data, ram->offset);
	w16le(data + 2, max_bcs);

	dbg_printc("calibrate dco: get freq\n");
	if (hal_proto_execute(&h->hal,
		map_fid(h, HAL_PROTO_FID_GET_DCO_FREQUENCY),
		data, 6) < 0)
		goto fail;
	if (h->hal.length < 6) {
		printc_err("v3hil: short reply: %d\n", h->hal.length);
		goto fail;
	}

	h->cal.cal0 = r16le(data);
	h->cal.cal1 = r16le(data + 2);

	w32le(mem_write, 0x56); /* addr of DCO */
	w32le(mem_write + 4, 3);
	mem_write[8] = data[0]; /* DCO */
	mem_write[9] = data[2]; /* BCS1 */
	mem_write[10] = data[4]; /* BCS2 */
	mem_write[11] = 0; /* pad */
	dbg_printc("calibrate dco: write\n");
	if (hal_proto_execute(&h->hal,
		    map_fid(h, HAL_PROTO_FID_WRITE_MEM_BYTES),
		    mem_write, 12) < 0) {
		printc_err("v3hil: failed to load DCO settings\n");
		goto fail;
	}

	return 0;

fail:
	printc_err("v3hil: DCO calibration failed\n");
	return -1;
}

static int calibrate_fll(struct v3hil *h)
{
	const struct chipinfo_memory *ram = find_ram(h->chip);
	uint8_t data[10];
	uint8_t mem_write[16];

	if (!ram)
		goto fail;

	printc_dbg("Calibrate FLL...\n");

	w16le(data, ram->offset);
	w16le(data + 2, 0);

	dbg_printc("calibrate fll: get dco freq\n");
	if (hal_proto_execute(&h->hal,
		map_fid(h, HAL_PROTO_FID_GET_DCO_FREQUENCY),
		data, 10) < 0)
		goto fail;
	if (h->hal.length < 10) {
		printc_err("v3hil: short reply: %d\n", h->hal.length);
		goto fail;
	}

	h->cal.cal0 = 0;
	h->cal.cal1 = r16le(data + 2);

	w32le(mem_write, 0x50); /* addr of SCFI0 */
	w32le(mem_write + 4, 5);
	mem_write[8] = data[0]; /* SCFI0 */
	mem_write[9] = data[2]; /* SCFI1 */
	mem_write[10] = data[4]; /* SCFQCTL */
	mem_write[11] = data[6]; /* FLLCTL0 */
	mem_write[12] = data[8]; /* FLLCTL1 */
	mem_write[13] = 0; /* pad */

	dbg_printc("calibrate fll: write\n");
	if (hal_proto_execute(&h->hal,
		    map_fid(h, HAL_PROTO_FID_WRITE_MEM_BYTES),
		    mem_write, 14) < 0) {
		printc_err("v3hil: failed to load FLL settings\n");
		goto fail;
	}

	return 0;

fail:
	printc_err("v3hil: FLL calibration failed\n");
	return -1;
}

static int calibrate(struct v3hil *h)
{
	int r;

	if (h->cal.is_cal)
		return 0;

	switch (h->chip->clock_sys) {
	case CHIPINFO_CLOCK_SYS_BC_1XX:
		r = calibrate_dco(h, 0x7);
		break;

	case CHIPINFO_CLOCK_SYS_BC_2XX:
		r = calibrate_dco(h, 0xf);
		break;

	case CHIPINFO_CLOCK_SYS_FLL_PLUS:
		r = calibrate_fll(h);
		break;

	default:
		r = 0;
		h->cal.cal0 = 0;
		h->cal.cal1 = 0;
		break;
	}

	if (r < 0)
		return -1;

	h->cal.is_cal = 1;
	return 0;
}

static int upload_funclet(struct v3hil *h,
			  const struct chipinfo_memory *ram,
			  const struct chipinfo_funclet *f)
{
	uint32_t addr = ram->offset;
	const uint16_t *code = f->code;
	uint16_t num_words = f->code_size;

	if (num_words * 2 > ram->size) {
		printc_err("v3hil: funclet too big for RAM\n");
		return -1;
	}

	while (num_words) {
		uint8_t data[512];
		uint16_t n = num_words > 112 ? 112 : num_words;
		int i;

		w32le(data, addr);
		w32le(data + 4, n);
		for (i = 0; i < n; i++)
			w16le(data + 8 + i * 2, code[i]);

		dbg_printc("upload funclet: %d\n", n);
		if (hal_proto_execute(&h->hal,
				map_fid(h, HAL_PROTO_FID_WRITE_MEM_WORDS),
				data, n * 2 + 8) < 0) {
			printc_err("v3hil: funclet upload "
				   "failed at 0x%04x (%d words)\n",
				   addr, n);
			return -1;
		}

		addr += n * 2;
		code += n;
		num_words -= n;
	}

	return 0;
}

static int write_flash(struct v3hil *h, address_t addr,
		       const uint8_t *mem, address_t size)
{
	const struct chipinfo_memory *ram = find_ram(h->chip);
	const struct chipinfo_funclet *f = h->chip->v3_write;
	uint8_t data[256];
	uint16_t avail;

	if (!ram)
		return -1;

	if (!f) {
		printc_err("v3hil: no funclet defined for flash write\n");
		return -1;
	}

	dbg_printc("write flash: calibrate\n");
	if (calibrate(h) < 0)
		return -1;
	dbg_printc("write flash: upload funclet\n");
	if (upload_funclet(h, ram, f) < 0)
		return -1;

	if (size > 128)
		size = 128;

	avail = ram->size - f->code_size * 2;
	if (avail > f->max_payload)
		avail = f->max_payload;

	w16le(data, ram->offset);
	w16le(data + 2, avail);
	w16le(data + 4, ram->offset + f->entry_point);

	w32le(data + 6, addr);
	w32le(data + 10, size >> 1);
	w32le(data + 14, 0);
	/* If FPERM_LOCKED_FLASH is set, info A is UNLOCKED */
	w16le(data + 16, (opdb_read_fperm() & FPERM_LOCKED_FLASH) ?
			0xa548 : 0xa508);
	w16le(data + 18, h->cal.cal0);
	w16le(data + 20, h->cal.cal1);
	memcpy(data + 22, mem, size);

	dbg_printc("exec write flash funclet\n");
	if (hal_proto_execute(&h->hal,
		    map_fid(h, HAL_PROTO_FID_EXECUTE_FUNCLET),
		    data, size + 22) < 0) {
		printc_err("v3hil: failed to program %d bytes at 0x%04x\n",
			   size, addr);
		return -1;
	}

	return size;
}

static int write_ram(struct v3hil *h, const struct chipinfo_memory *m,
		     address_t addr, const uint8_t *mem, address_t size)
{
	uint8_t data[256];
	bool fram = false;
	if (h->chip->features & CHIPINFO_FEATURE_FRAM) {
		if (!strcmp(m->name, "Main") || !strcmp(m->name, "Info")) {
			dbg_printc("write ram: to FRAM!\n");
			fram = true;
		}
	}

	w32le(data, addr);
	w32le(data + 4, (m->bits == 8 || fram) ? size : (size >> 1));

	memcpy(data + 8, mem, size);

	dbg_printc("write ram\n");
	hal_proto_fid_t fid = (m->bits == 8) ? HAL_PROTO_FID_WRITE_MEM_BYTES
			: HAL_PROTO_FID_WRITE_MEM_WORDS;
	if (fram) fid = HAL_PROTO_FID_WRITE_FRAM_QUICK_XV2;
	if (hal_proto_execute(&h->hal, map_fid(h, fid), data, size + 8) < 0) {
		printc_err("v3hil: failed writing %d bytes to 0x%05x\n",
			   size, addr);
		return -1;
	}

	return size;
}

int v3hil_write(struct v3hil *h, address_t addr,
		const uint8_t *mem, address_t size)
{
	const struct chipinfo_memory *m = NULL;

	if (h->chip) {
		size = check_range(h->chip, addr, size, &m);
		if (!m)
			return size;
	}

	if (size > 128)
		size = 128;

	if (m->type == CHIPINFO_MEMTYPE_FLASH) {
		dbg_printc("write: call write flash\n");
		return write_flash(h, addr, mem, size);
	}

	dbg_printc("write: call write ram\n");
	return write_ram(h, m, addr, mem, size);
}

static int call_erase(struct v3hil *h,
		      const struct chipinfo_memory *ram,
		      const struct chipinfo_funclet *f,
		      address_t addr, uint16_t type)
{
	uint8_t data[32];

	printc_dbg("Erase segment @ 0x%04x\n", addr);

	w16le(data, ram->offset);
	w16le(data + 2, 0);
	w16le(data + 4, ram->offset + f->entry_point);
	w32le(data + 6, addr);
	w32le(data + 10, 2);
	w16le(data + 14, type);
	w16le(data + 16, (opdb_read_fperm() & FPERM_LOCKED_FLASH) ?
			 0xa548 : 0xa508);
	w16le(data + 18, h->cal.cal0);
	w16le(data + 20, h->cal.cal1);
	w32le(data + 22, 0xdeadbeef);

	dbg_printc("erase: call funclet\n");
	if (hal_proto_execute(&h->hal,
			map_fid(h, HAL_PROTO_FID_EXECUTE_FUNCLET),
			data, 26) < 0) {
		printc_err("v3hil: failed to erase at 0x%04x\n",
			   addr);
		return -1;
	}

	return 0;
}

int v3hil_erase(struct v3hil *h, address_t segment)
{
	const struct chipinfo_memory *ram = find_ram(h->chip);
	const struct chipinfo_funclet *f = h->chip->v3_erase;
	const struct chipinfo_memory *flash;

	if (!ram)
		return -1;

	if (!f) {
		printc_err("v3hil: no funclet defined for flash erase\n");
		return -1;
	}

	if (segment == ADDRESS_NONE)
		flash = chipinfo_find_mem_by_name(h->chip, "main");
	else
		flash = chipinfo_find_mem_by_addr(h->chip, segment);

	if (!flash)
		printc_err("v3hil: can't find appropriate flash region\n");

	dbg_printc("erase: calibrate\n");
	if (calibrate(h) < 0)
		return -1;
	dbg_printc("erase: upload funclet\n");
	if (upload_funclet(h, ram, f) < 0)
		return -1;

	if (segment == ADDRESS_NONE) {
		int bank_size = flash->size;
		int i;

		if (flash->banks)
			bank_size /= flash->banks;

		for (i = flash->banks; i >= 0; i--) {
			dbg_printc("Erase bank %d\n", i);

			if (call_erase(h, ram, f,
				flash->offset + i * bank_size - 2, 0xa502) < 0)
				return -1;
		}
	} else {
		segment &= ~(flash->seg_size - 1);
		segment |= flash->seg_size - 2;

		if (call_erase(h, ram, f, segment, 0xa502) < 0)
			return -1;
	}

	return 0;
}

int v3hil_update_regs(struct v3hil *h)
{
	const hal_proto_fid_t fid = map_fid(h, HAL_PROTO_FID_READ_ALL_CPU_REGS);
	const int reg_size = (fid == HAL_PROTO_FID_READ_ALL_CPU_REGS
							|| fid == HAL_PROTO_FID_READ_ALL_CPU_REGS - 1)
						? 2 : 3;

	int i;
	int sptr = 0;

	dbg_printc("Read regs\n");
	if (hal_proto_execute(&h->hal, fid, NULL, 0) < 0) {
		printc_err("v3hil: can't read CPU registers\n");
		return -1;
	}

	if (h->hal.length < reg_size * 13) {
		printc_err("v3hil: short read: %d\n", h->hal.length);
		return -1;
	}

	for (i = 0; i < DEVICE_NUM_REGS; i++){
		address_t r = 0;
		int j;

		if ((i == MSP430_REG_PC) ||
		    (i == MSP430_REG_SR) ||
		    (i == MSP430_REG_R3))
			continue;

		for (j = 0; j < reg_size; j++)
			r |= ((address_t)(h->hal.payload[sptr++])) <<
				(j << 3);

		h->regs[i] = r;
	}

	return 0;
}

int v3hil_flush_regs(struct v3hil *h)
{
	const hal_proto_fid_t fid = map_fid(h, HAL_PROTO_FID_WRITE_ALL_CPU_REGS);
	const int reg_size = (fid == HAL_PROTO_FID_WRITE_ALL_CPU_REGS
							|| fid == HAL_PROTO_FID_WRITE_ALL_CPU_REGS - 1)
						? 2 : 3;
	int i;
	int dptr = 0;
	uint8_t data[64];

	for (i = 0; i < DEVICE_NUM_REGS; i++){
		address_t r = h->regs[i];
		int j;

		if ((i == MSP430_REG_PC) ||
		    (i == MSP430_REG_SR) ||
		    (i == MSP430_REG_R3))
			continue;

		for (j = 0; j < reg_size; j++) {
			data[dptr++] = r;
			r >>= 8;
		}
	}

	dbg_printc("Write regs\n");
	if (hal_proto_execute(&h->hal, fid, data, reg_size * 13) < 0) {
		printc_err("v3hil: can't write CPU registers\n");
		return -1;
	}

	return 0;
}

int v3hil_context_restore(struct v3hil *h, int free)
{
	uint8_t data[32];

	memset(data, 0, sizeof(data));
	data[0] = (h->jtag_id == 0x89) ? 0x20 : 0x5c; /* WDTCTL */
	data[1] = 0x01;
	data[2] = h->wdtctl;
	data[3] = 0x5a; /* WDTPW */
	w32le(data + 4, h->regs[MSP430_REG_PC]);
	data[8] = h->regs[MSP430_REG_SR];
	data[9] = h->regs[MSP430_REG_SR] >> 8;
	data[10] = free ? 7 : 6;
	data[14] = free ? 1 : 0;

	dbg_printc("Context restore\n");
	if (hal_proto_execute(&h->hal,
		    map_fid(h, HAL_PROTO_FID_RC_RELEASE_JTAG),
		    data, 18) < 0) {
		printc_err("v3hil: failed to restore context\n");
		return -1;
	}

	return 0;
}

int v3hil_context_save(struct v3hil *h)
{
	uint8_t data[32];

	h->cal.is_cal = 0;

	memset(data, 0, sizeof(data));
	data[0] = (h->jtag_id == 0x89) ? 0x20 : 0x5c; /* WDTCTL */
	data[1] = 0x01;
	data[2] = h->wdtctl | 0x80;
	data[3] = 0x5a; /* WDTPW */

	dbg_printc("Context save\n");
	if (hal_proto_execute(&h->hal,
		map_fid(h, HAL_PROTO_FID_SJ_CONDITIONAL_SC),
		    data, 8) < 0)
		return -1;
	if (h->hal.length < 8) {
		printc_err("v3hil: short reply: %d\n", h->hal.length);
		return -1;
	}

	h->wdtctl = r16le(h->hal.payload);
	h->regs[MSP430_REG_PC] = r32le(h->hal.payload + 2);
	h->regs[MSP430_REG_SR] = r16le(h->hal.payload + 6);
	return 0;
}

int v3hil_single_step(struct v3hil *h)
{
	uint8_t data[32];

	h->cal.is_cal = 0;

	memset(data, 0, sizeof(data));
	data[0] = (h->jtag_id == 0x89) ? 0x20 : 0x5c; /* WDTCTL */
	data[1] = 0x01;
	data[2] = h->wdtctl;
	data[3] = 0x5a; /* WDTPW */
	w32le(data + 4, h->regs[MSP430_REG_PC]);
	data[8] = h->regs[MSP430_REG_SR];
	data[9] = h->regs[MSP430_REG_SR] >> 8;
	data[10] = 7;

	dbg_printc("Single-stepping...\n");
	if (hal_proto_execute(&h->hal,
		    map_fid(h, HAL_PROTO_FID_SINGLE_STEP),
		    data, 18) < 0) {
		printc_err("do_step: single-step failed\n");
		return -1;
	}

	if (h->hal.length < 8) {
		printc_err("do_step: short reply: %d\n", h->hal.length);
		return -1;
	}

	h->wdtctl = r16le(h->hal.payload);
	h->regs[MSP430_REG_PC] = r32le(h->hal.payload + 2);
	h->regs[MSP430_REG_SR] = r16le(h->hal.payload + 6);
	return 0;
}

/************************************************************************
 * Identification/config
 */

static int set_param(struct v3hil *fet, hal_proto_config_t cfg,
		     uint32_t value)
{
	uint8_t data[8] = {0};
	int i;

	for (i = 0; i < 4; i++) {
		data[i + 4] = value;
		value >>= 8;
	}

	data[0] = cfg;

	dbg_printc("Set param 0x%02x to 0x%08x\n", cfg, value);
	if (hal_proto_execute(&fet->hal, map_ver(fet, HAL_PROTO_FID_CONFIGURE),
			      data, 8) < 0) {
		printc_err("v3hil: can't set param 0x%02x to 0x%08x\n",
			   cfg, value);
		return -1;
	}

	return 0;
}

static int idproc_89(struct v3hil *fet, uint32_t id_data_addr,
		     struct chipinfo_id *id)
{
	uint8_t data[32];

	printc_dbg("Identify (89)...\n");
	printc_dbg("Read device ID bytes at 0x%05x...\n", id_data_addr);
	memset(data, 0, 8);
	w32le(data, id_data_addr);
	data[4] = 8;
	if (hal_proto_execute(&fet->hal, map_ver(fet, HAL_PROTO_FID_READ_MEM_WORDS),
			      data, 8) < 0)
		return -1;
	if (fet->hal.length < 16) {
		printc_err("v3hil: short reply: %d\n", fet->hal.length);
		return -1;
	}

	id->ver_id = r16le(fet->hal.payload);
	id->ver_sub_id = 0;
	id->revision = r16le(fet->hal.payload + 2);
	id->fab = fet->hal.payload[3];
	id->self = r16le(fet->hal.payload + 4);
	id->config = fet->hal.payload[13] & 0x7f;

	printc_dbg("Read fuses...\n");
	if (hal_proto_execute(&fet->hal, map_ver(fet, HAL_PROTO_FID_GET_FUSES), NULL, 0) < 0)
		return -1;
	if (!fet->hal.length) {
		printc_err("v3hil: short reply: %d\n", fet->hal.length);
		return -1;
	}

	id->fuses = fet->hal.payload[0];
	return 0;
}

static int idproc_9x(struct v3hil *fet, uint32_t dev_id_ptr,
		     struct chipinfo_id *id)
{
	uint8_t data[32];
	uint8_t info_len;
	int i;
	int tlv_size;

	printc_dbg("Identify (9x)...\n");
	printc_dbg("Read device ID bytes at 0x%05x...\n", dev_id_ptr);
	memset(data, 0, 8);
	w32le(data, dev_id_ptr);
	data[4] = 4;
	if (hal_proto_execute(&fet->hal, map_ver(fet, HAL_PROTO_FID_READ_MEM_QUICK_XV2),
			      data, 8) < 0)
		return -1;
	if (fet->hal.length < 8) {
		printc_err("v3hil: short reply: %d\n", fet->hal.length);
		return -1;
	}

	info_len = fet->hal.payload[0];
	id->ver_id = r16le(fet->hal.payload + 4);
	id->revision = fet->hal.payload[6];
	id->config = fet->hal.payload[7];
	id->fab = 0x55;
	id->self = 0x5555;
	id->fuses = 0x55;

	if ((info_len < 1) || (info_len > 11))
		return 0;

	printc_dbg("Read TLV...\n");
	tlv_size = ((1 << info_len) - 2) << 2;
	w32le(data, dev_id_ptr);
	w32le(data + 4, tlv_size >> 1);
	w32le(data + 8, fet->regs[MSP430_REG_PC]);
	if (hal_proto_execute(&fet->hal, map_ver(fet, HAL_PROTO_FID_READ_MEM_QUICK_XV2),
			      data, 8) < 0)
		return -1;
	if (fet->hal.length < tlv_size) {
		printc_err("v3hil: short reply: %d\n", fet->hal.length);
		return -1;
	}

	/* Search TLV for sub-ID */
	i = 8;
	while (i + 3 < tlv_size) {
		uint8_t tag = fet->hal.payload[i++];
		uint8_t len = fet->hal.payload[i++];

		if (tag == 0xff)
			break;

		if ((tag == 0x14) && (len >= 2))
			id->ver_sub_id = r16le(fet->hal.payload);

		i += len;
	}

	return 0;
}

int v3hil_identify(struct v3hil *fet)
{
	struct chipinfo_id id;
	uint32_t dev_id_ptr;
	uint32_t id_data_addr;
	int i;

	printc_dbg("Fetching JTAG ID...\n");
	if (hal_proto_execute(&fet->hal, map_ver(fet, HAL_PROTO_FID_GET_JTAG_ID),
			      NULL, 0) < 0)
		return -1;

	printc_dbg("ID:");
	for (i = 0; i < fet->hal.length; i++)
		printc_dbg(" %02x", fet->hal.payload[i]);
	printc_dbg("\n");

	if (fet->hal.length < 12) {
		if (fet->hal.length == 2) {
			fet->jtag_id = fet->hal.payload[0];

			if (hal_proto_execute(&fet->hal, map_ver(fet, HAL_PROTO_FID_GET_DEVICE_ID_PTR),
						  NULL, 0) < 0)
				return -1;

#ifdef DEBUG_V3HIL
			dbg_printc("len: %d\n", fet->hal.length);
			printc_dbg("v3hil: IDPtr:");
			for (i = 0; i < fet->hal.length; i++)
				printc_dbg(" %02x", fet->hal.payload[i]);
			printc_dbg("\n");
#endif

			if (fet->hal.length < 10) {
				printc_err("v3hil: short reply: %d\n", fet->hal.length);
				return -1;
			} else {
				dev_id_ptr = r32le(fet->hal.payload + 0);

				if (dev_id_ptr == 0) {
					/* welp sometimes it's this instead (JTAG ID == 0x89?) */
					dev_id_ptr = r32le(fet->hal.payload + 4);
				}

				id_data_addr = dev_id_ptr; /* idk */
			}
		} else {
			printc_err("v3hil: short reply: %d\n", fet->hal.length);
			return -1;
		}
	} else {
		/* Byte at 0 is JTAG ID. 0x91, 0x95, 0x99 means CPUxV2. 0x89
		 * means old CPU.
		 */
		fet->jtag_id = fet->hal.payload[0];
		dev_id_ptr = r32le(fet->hal.payload + 4);
		id_data_addr = r32le(fet->hal.payload + 8);
	}

	/* Pick fail-safe configuration */
	printc_dbg("Reset parameters...\n");
	if (set_param(fet, HAL_PROTO_CONFIG_CLK_CONTROL_TYPE, 0) < 0 ||
	    set_param(fet, HAL_PROTO_CONFIG_SFLLDEH, 0) < 0 ||
	    set_param(fet, HAL_PROTO_CONFIG_DEFAULT_CLK_CONTROL, 0x040f) ||
	    set_param(fet, HAL_PROTO_CONFIG_ENHANCED_PSA, 0) < 0 ||
	    set_param(fet, HAL_PROTO_CONFIG_PSA_TCKL_HIGH, 0) < 0 ||
	    set_param(fet, HAL_PROTO_CONFIG_POWER_TESTREG_MASK, 0) < 0 ||
	    set_param(fet, HAL_PROTO_CONFIG_POWER_TESTREG3V_MASK, 0) < 0 ||
	    set_param(fet, HAL_PROTO_CONFIG_ALT_ROM_ADDR_FOR_CPU_READ, 0) < 0)
		return -1;
	set_param(fet, HAL_PROTO_CONFIG_NO_BSL, 0); /* is allowed to fail */

	printc_dbg("Check JTAG fuse...\n");
	if (hal_proto_execute(&fet->hal, map_ver(fet, HAL_PROTO_FID_IS_JTAG_FUSE_BLOWN),
			      NULL, 0) < 0)
		return -1;
	if ((fet->hal.length >= 2) &&
	    (fet->hal.payload[0] == 0x55) &&
	    (fet->hal.payload[1] == 0x55)) {
		printc_err("v3hil: JTAG fuse is blown!\n");
		return -1;
	}

	memset(&id, 0, sizeof(id));
	printc_dbg("Sync JTAG...\n");
	if (v3hil_sync(fet) < 0)
		return -1;

	if (fet->jtag_id == 0x89) {
		if (idproc_89(fet, id_data_addr, &id) < 0)
			return -1;
	} else {
		if (idproc_9x(fet, dev_id_ptr, &id) < 0)
			return -1;
	}

	printc_dbg("  ver_id:         %04x\n", id.ver_id);
	printc_dbg("  ver_sub_id:     %04x\n", id.ver_sub_id);
	printc_dbg("  revision:       %02x\n", id.revision);
	printc_dbg("  fab:            %02x\n", id.fab);
	printc_dbg("  self:           %04x\n", id.self);
	printc_dbg("  config:         %02x\n", id.config);
	printc_dbg("  fuses:          %02x\n", id.fuses);
	printc_dbg("  activation_key: %08x\n", id.activation_key);

	fet->chip = chipinfo_find_by_id(&id);
	if (!fet->chip) {
		printc_err("v3hil: unknown chip ID\n");
		return -1;
	}

	return 0;
}

int v3hil_configure(struct v3hil *fet)
{
	printc_dbg("Configuring for %s...\n", fet->chip->name);

	if (set_param(fet, HAL_PROTO_CONFIG_CLK_CONTROL_TYPE,
		      fet->chip->clock_control) < 0 ||
	    set_param(fet, HAL_PROTO_CONFIG_SFLLDEH,
		      (fet->chip->features &
		       CHIPINFO_FEATURE_SFLLDH) ? 1 : 0) < 0 ||
	    set_param(fet, HAL_PROTO_CONFIG_DEFAULT_CLK_CONTROL,
		      fet->chip->mclk_control) < 0 ||
	    set_param(fet, HAL_PROTO_CONFIG_ENHANCED_PSA,
		      (fet->chip->psa == CHIPINFO_PSA_ENHANCED) ? 1 : 0) < 0 ||
	    set_param(fet, HAL_PROTO_CONFIG_PSA_TCKL_HIGH,
		      (fet->chip->features &
		       CHIPINFO_FEATURE_PSACH) ? 1 : 0) < 0 ||
	    set_param(fet, HAL_PROTO_CONFIG_POWER_TESTREG_MASK,
		      fet->chip->power.reg_mask) < 0 ||
	    set_param(fet, HAL_PROTO_CONFIG_TESTREG_ENABLE_LPMX5,
		      fet->chip->power.enable_lpm5) < 0 ||
	    set_param(fet, HAL_PROTO_CONFIG_TESTREG_DISABLE_LPMX5,
		      fet->chip->power.disable_lpm5) < 0 ||
	    set_param(fet, HAL_PROTO_CONFIG_POWER_TESTREG3V_MASK,
		      fet->chip->power.reg_mask_3v) < 0 ||
	    set_param(fet, HAL_PROTO_CONFIG_TESTREG3V_ENABLE_LPMX5,
		      fet->chip->power.enable_lpm5_3v) < 0 ||
	    set_param(fet, HAL_PROTO_CONFIG_TESTREG3V_DISABLE_LPMX5,
		      fet->chip->power.disable_lpm5_3v) < 0 ||
	    set_param(fet, HAL_PROTO_CONFIG_ALT_ROM_ADDR_FOR_CPU_READ,
		      (fet->chip->features &
		       CHIPINFO_FEATURE_1337) ? 1 : 0) < 0)
		return -1;
	/* is allowed to fail */
	set_param(fet, HAL_PROTO_CONFIG_NO_BSL,
		  (fet->chip->features & CHIPINFO_FEATURE_NO_BSL) ? 1 : 0);

	return 0;
}
