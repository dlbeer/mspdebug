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

#include "util.h"
#include "output.h"
#include "ctrlc.h"
#include "jtaglib.h"
#include "mehfet_proto.h"
#include "mehfet_xport.h"

#include "mehfet.h"


struct mehfet_device {
	struct device base;
	struct jtdev jtag;
	transport_t trans;
	enum mehfet_conn connstat;
};


/*extern void __builtin_trap(void);*/
#define NO_LL_JTAG_BP 0
#define no_ll_jtag() \
	do { \
		printc_err("mehfet: %s: low-lewel JTAG function not implemented!\n", __func__); \
		p->failed = 1; \
		if (NO_LL_JTAG_BP) /*__builtin_trap()*/; \
	} while (0) \


#define JTMF_GET_DEV(p) ((struct mehfet_device*)((size_t)(p) - \
			offsetof(struct mehfet_device, jtag))) \


static int jtmf_open(struct jtdev* p, const char* device)
{
	printc_err("mehfet: %s should not get called.\n", __func__);
	p->failed = 1;
	return -1;
}
static void jtmf_close(struct jtdev* p)
{
	printc_err("mehfet: %s should not get called.\n", __func__);
	p->failed = 1;
}
static void jtmf_power_on(struct jtdev* p)
{
	printc_err("mehfet: %s should not get called.\n", __func__);
	p->failed = 1;
}
static void jtmf_power_off(struct jtdev* p)
{
	printc_err("mehfet: %s should not get called.\n", __func__);
	p->failed = 1;
}
static void jtmf_connect(struct jtdev* p)
{
	printc_err("mehfet: %s should not get called.\n", __func__);
	p->failed = 1;
}
static void jtmf_release(struct jtdev* p)
{
	printc_err("mehfet: %s should not get called.\n", __func__);
	p->failed = 1;
}

static void jtmf_tck(struct jtdev* p, int _) { (void)_; no_ll_jtag(); }
static void jtmf_tms(struct jtdev* p, int _) { (void)_; no_ll_jtag(); }
static void jtmf_tdi(struct jtdev* p, int _) { (void)_; no_ll_jtag(); }
static void jtmf_rst(struct jtdev* p, int _) { (void)_; no_ll_jtag(); }
static void jtmf_tst(struct jtdev* p, int _) { (void)_; no_ll_jtag(); }
static int jtmf_tdo_get(struct jtdev* p) { no_ll_jtag(); return 0; }

static void jtmf_tclk(struct jtdev* p, int out)
{
	struct mehfet_device* d = JTMF_GET_DEV(p);

	int r = mehfet_cmd_tclk_edge(d->trans, out);
	if (r < 0) p->failed = 1;
}
static int jtmf_tclk_get(struct jtdev* p)
{
	struct mehfet_device* d = JTMF_GET_DEV(p);
	int ret = 1;

	enum mehfet_lines lines = 0;
	int r = mehfet_cmd_get_old_lines(d->trans, &lines);
	if (r < 0) {
		p->failed = 1;
	} else {
		ret = (lines & mehfet_line_tclk) ? 1 : 0;
	}

	return ret;
}
static void jtmf_tclk_strobe(struct jtdev* p, unsigned int count)
{
	struct mehfet_device* d = JTMF_GET_DEV(p);

	int r = mehfet_cmd_tclk_burst(d->trans, count);
	if (r < 0) p->failed = 1;
}

/* MehFET has no leds */
static void jtmf_led_green(struct jtdev* p, int out) { (void)p; (void)out; }
static void jtmf_led_red  (struct jtdev* p, int out) { (void)p; (void)out; }

static uint8_t jtmf_ir_shift(struct jtdev *p, uint8_t ir)
{
	struct mehfet_device* d = JTMF_GET_DEV(p);

	uint8_t ret = 0;
	int r = mehfet_cmd_irshift(d->trans, ir, &ret);
	if (r < 0) p->failed = 1;

	return ret;
}
static uint8_t jtmf_dr_shift_8(struct jtdev *p, uint8_t dr)
{
	struct mehfet_device* d = JTMF_GET_DEV(p);

	uint8_t ret = 0;
	int r = mehfet_cmd_drshift(d->trans, 8, &dr, &ret);
	if (r < 0) p->failed = 1;

	return ret;
}
static uint16_t jtmf_dr_shift_16(struct jtdev *p, uint16_t dr)
{
	struct mehfet_device* d = JTMF_GET_DEV(p);

	uint8_t inbuf[2] = { dr & 0xff, (dr >> 8) & 0xff };
	uint8_t outbuf[2];
	int r = mehfet_cmd_drshift(d->trans, 16, inbuf, outbuf);
	if (r < 0) p->failed = 1;

	return outbuf[0] | ((uint16_t)outbuf[1] << 8);
}
static void jtmf_tms_sequence(struct jtdev *p, int bits, unsigned int value)
{
	struct mehfet_device* d = JTMF_GET_DEV(p);

	enum mehfet_lines lines = 0;
	int r = mehfet_cmd_get_old_lines(d->trans, &lines);
	if (r < 0) {
		p->failed = 1;
		return;
	}

	uint8_t dbuf[4] = { value & 0xff, (value >> 8) & 0xff,
						(value >> 16) & 0xff, (value >> 24) & 0xff };
	r = mehfet_cmd_tms_seq(d->trans, lines & mehfet_line_tdi, bits, dbuf);
	if (r < 0) p->failed = 1;
}
static void jtmf_init_dap(struct jtdev *p)
{
	struct mehfet_device* dev = JTMF_GET_DEV(p);

	enum mehfet_resettap_status stat = 0;
	int r = mehfet_cmd_reset_tap(dev->trans, mehfet_rsttap_do_reset
			| mehfet_rsttap_fuse_do, &stat);

	if (r < 0) p->failed = 1;
}


static const struct jtdev_func jtdev_func_mehfet = {
	.jtdev_open        = jtmf_open,
	.jtdev_close       = jtmf_close,
	.jtdev_power_on    = jtmf_power_on,
	.jtdev_power_off   = jtmf_power_off,
	.jtdev_connect     = jtmf_connect,
	.jtdev_release     = jtmf_release,
	.jtdev_tck         = jtmf_tck,
	.jtdev_tms         = jtmf_tms,
	.jtdev_tdi         = jtmf_tdi,
	.jtdev_rst         = jtmf_rst,
	.jtdev_tst         = jtmf_tst,
	.jtdev_tdo_get     = jtmf_tdo_get,
	.jtdev_tclk        = jtmf_tclk,
	.jtdev_tclk_get    = jtmf_tclk_get,
	.jtdev_tclk_strobe = jtmf_tclk_strobe,
	.jtdev_led_green   = jtmf_led_green,
	.jtdev_led_red     = jtmf_led_red,

	.jtdev_ir_shift    = jtmf_ir_shift,
	.jtdev_dr_shift_8  = jtmf_dr_shift_8,
	.jtdev_dr_shift_16 = jtmf_dr_shift_16,
	.jtdev_tms_sequence= jtmf_tms_sequence,
	.jtdev_init_dap    = jtmf_init_dap
};


/*---------------------------------------------------------------------------*/

// TODO: these five are kinda copied from pif.c, should be deduplicated

static int read_words(device_t dev_base, const struct chipinfo_memory *m,
		address_t addr, address_t len, uint8_t *data)
{
#ifdef DEBUG_MEHFET_DRIVER
	printc_dbg("mehfet: read_words: addr=0x%04x, len=0x%x\n", addr, len);
#endif

	struct mehfet_device* dev = (struct mehfet_device*)dev_base;
	struct jtdev *p = &dev->jtag;

	for (unsigned int index = 0; index < len; index += 2) {
		unsigned int word = jtag_read_mem(p, 16, addr+index);
		data[index  ] =  word       & 0x00ff;
		data[index+1] = (word >> 8) & 0x00ff;
	}

	return p->failed ? -1 : len;
}

static int write_ram_word(struct jtdev *p, address_t addr, uint16_t value)
{
	jtag_write_mem(p, 16, addr, value);

	return p->failed ? -1 : 0;
}

static int write_flash_block(struct jtdev *p, address_t addr,
		address_t len, const uint8_t *data)
{
	uint16_t* word = malloc( len / 2 * sizeof(*word) );
	if (!word) {
		pr_error("mehfet: failed to allocate memory");
		return -1;
	}

	for (unsigned int i = 0; i < len/2; i++) {
		word[i]=data[2*i] + (((uint16_t)data[2*i+1]) << 8);
	}
	jtag_write_flash(p, addr, len/2, word);

	free(word);

	return p->failed ? -1 : 0;
}

/* Write a word-aligned block to any kind of memory.
 * returns the number of bytes written or -1 on failure
 */
static int write_words(device_t dev_base, const struct chipinfo_memory *m,
		address_t addr, address_t len, const uint8_t *data)
{
	struct mehfet_device* dev = (struct mehfet_device*)dev_base;
	struct jtdev *p = &dev->jtag;
	int r;

	if (m->type != CHIPINFO_MEMTYPE_FLASH) {
#ifdef DEBUG_MEHFET_DRIVER
		printc_dbg("mehfet: write_words: addr=0x%04x, len=0x%x data=0x%04x\n",
				addr, len, r16le(data));
		if (len != 2) {
			printc_dbg("mehfet: WARN: write_words: len != 2! but 0x%04x\n", len);
			__builtin_trap();
		}
#endif
		len = 2;
		r = write_ram_word(p, addr, r16le(data));
	} else {
#ifdef DEBUG_MEHFET_DRIVER
		printc_dbg("mehfet: write_flash_block: addr=0x%04x, len=0x%x\n", addr, len);
#endif
		r = write_flash_block(p, addr, len, data);
	}

	if (r < 0) {
		printc_err("mehfet: write_words at address 0x%x failed\n", addr);
		return -1;
	}

	return len;
}

/*---------------------------------------------------------------------------*/

static int mehfet_readmem(device_t dev_base, address_t addr,
			   uint8_t *mem, address_t len)
{
	struct mehfet_device* dev = (struct mehfet_device*)dev_base;
	dev->jtag.failed = 0;
	return readmem(dev_base, addr, mem, len, read_words);
}

static int mehfet_writemem(device_t dev_base, address_t addr,
			    const uint8_t *mem, address_t len)
{
	struct mehfet_device* dev = (struct mehfet_device*)dev_base;
	dev->jtag.failed = 0;
	return writemem(dev_base, addr, mem, len, write_words, read_words);
}

static int mehfet_setregs(device_t dev_base, const address_t *regs)
{
	struct mehfet_device* dev = (struct mehfet_device*)dev_base;

	dev->jtag.failed = 0;

#ifdef DEBUG_MEHFET_DRIVER
	printc_dbg("mehfet: set regs\n");
#endif
	for (int i = 0; i < DEVICE_NUM_REGS; i++) {
#ifdef DEBUG_MEHFET_DRIVER
		printc_dbg("mehfet:  [%d] = 0x%04x\n", i, regs[i]);
#endif
		jtag_write_reg(&dev->jtag, i, regs[i]);
	}
	return dev->jtag.failed ? -1 : 0;
}

static int mehfet_getregs(device_t dev_base, address_t *regs)
{
	struct mehfet_device* dev = (struct mehfet_device*)dev_base;

	dev->jtag.failed = 0;

#ifdef DEBUG_MEHFET_DRIVER
	printc_dbg("mehfet: get regs\n");
#endif
	for (int i = 0; i < DEVICE_NUM_REGS; i++) {
		regs[i] = jtag_read_reg(&dev->jtag, i);
#ifdef DEBUG_MEHFET_DRIVER
		printc_dbg("mehfet:  [%d] = 0x%04x\n", i, regs[i]);
#endif
	}

	return dev->jtag.failed ? -1 : 0;
}

static int mehfet_ctl(device_t dev_base, device_ctl_t type)
{
	struct mehfet_device* dev = (struct mehfet_device*)dev_base;

	dev->jtag.failed = 0;

	switch (type) {
	case DEVICE_CTL_RESET:
		/* perform soft reset */
#ifdef DEBUG_MEHFET_DRIVER
		printc_dbg("mehfet: soft reset (PUC)\n");
#endif
		jtag_execute_puc(&dev->jtag);
		break;

	case DEVICE_CTL_RUN:
#ifdef DEBUG_MEHFET_DRIVER
		printc_dbg("mehfet: set breakpoints\n");
#endif
		/* transfer changed breakpoints to device */
		if (jtag_refresh_bps("mehfet", &dev->base, &dev->jtag) < 0) return -1;

#ifdef DEBUG_MEHFET_DRIVER
		printc_dbg("mehfet: run @ current PC\n");
#endif
		/* start program execution at current PC */
		jtag_release_device(&dev->jtag, 0xffff);
		break;

	case DEVICE_CTL_HALT:
#ifdef DEBUG_MEHFET_DRIVER
		printc_dbg("mehfet: halt\n");
#endif
		/* take device under JTAG control */
		jtag_get_device(&dev->jtag);
		break;

	case DEVICE_CTL_STEP:
#ifdef DEBUG_MEHFET_DRIVER
		printc_dbg("mehfet: single-step\n");
#endif
		/* execute next instruction at current PC */
		jtag_single_step(&dev->jtag);
		break;

	default:
		printc_err("mehfet: unsupported operation %d\n", type);
		return -1;
	}

	return dev->jtag.failed ? -1 : 0;
}

static device_status_t mehfet_poll(device_t dev_base)
{
	struct mehfet_device* dev = (struct mehfet_device*)dev_base;

	if (delay_ms(100) < 0 || ctrlc_check())
		return DEVICE_STATUS_INTR;

	int r = jtag_cpu_state(&dev->jtag);

#ifdef DEBUG_MEHFET_DRIVER
	printc_dbg("mehfet: cpu state: %d\n", r);
#endif

	if (r == 1) return DEVICE_STATUS_HALTED;

	return DEVICE_STATUS_RUNNING;
}

static int mehfet_erase(device_t dev_base, device_erase_type_t type,
			 address_t addr)
{
	struct mehfet_device* dev = (struct mehfet_device*)dev_base;

	dev->jtag.failed = 0;

	switch (type) {
	case DEVICE_ERASE_MAIN:
		jtag_erase_flash(&dev->jtag, JTAG_ERASE_MAIN, addr);
		break;
	case DEVICE_ERASE_ALL:
		jtag_erase_flash(&dev->jtag, JTAG_ERASE_MASS, addr);
		break;
	case DEVICE_ERASE_SEGMENT:
		jtag_erase_flash(&dev->jtag, JTAG_ERASE_SGMT, addr);
		break;
	default: return -1;
	}

#ifdef DEBUG_MEHFET_DRIVER
	printc_dbg("mehfet: erase flash %d at %04x: %s\n", type, addr, dev->jtag.failed ? "failed" : "succeeded");
#endif

	return dev->jtag.failed ? -1 : 0;
}

static int mehfet_getconfigfuses(device_t dev_base)
{
	struct mehfet_device* dev = (struct mehfet_device*)dev_base;

	int r = jtag_get_config_fuses(&dev->jtag);

#ifdef DEBUG_MEHFET_DRIVER
	printc_dbg("mehfet: get_config_fuses: %d\n", r);
#endif

	return r;
}

/*---------------------------------------------------------------------------*/

static int check_dev_ok(struct mehfet_device* dev, const struct device_args* args,
		enum mehfet_conn* useconn) {
	transport_t t = dev->trans;
	struct mehfet_info info;
	int r;

	r = mehfet_cmd_info(t, &info);
	if (r < 0) return r;

	printc_dbg("mehfet: MehFET %s\n", info.devicename);
	free(info.devicename);

	if (info.proto_version < MEHFET_PROTO_VER_MIN_SUPPORTED) {
		printc_err("mehfet: device has protocol version %04x, "
				"need at least %04x\n", info.proto_version,
				MEHFET_PROTO_VER_MIN_SUPPORTED);
		return -1;
	}
	if (info.proto_version > MEHFET_PROTO_VER) {
		printc_err("mehfet: device has newer protocol version %04x "
				"supporting at most %04x\n", info.proto_version,
				MEHFET_PROTO_VER);
		return -1;
	}

	if (args->flags & DEVICE_FLAG_JTAG) {
		if (!(info.caps & (mehfet_cap_jtag_noentry | mehfet_cap_jtag_entryseq))) {
			printc_err("mehfet: Cannot do JTAG, device doesn't have the capability\n");
			return -1;
		}

		// devices that don't need one will probably work when given an entry
		// sequence. no good way of doing a proper autodetect sadly
		if (info.caps & mehfet_cap_jtag_entryseq)
			*useconn = mehfet_conn_jtag_entryseq;
		else
			*useconn = mehfet_conn_jtag_noentry;
	} else {
		if (!(info.caps & mehfet_cap_sbw_entryseq)) {
			printc_err("mehfet: Cannot do Spy-Bi-Wire, device doesn't have the capability\n");
			return -1;
		}
		*useconn = mehfet_conn_sbw_entryseq;
	}

	if (args->flags & DEVICE_FLAG_FORCE_RESET)
		*useconn |= mehfet_conn_nrstmask;

	mehfet_transport_set_buf_size(t, (int)info.packet_buf_size);

	return 0;
}

static int init_device(struct mehfet_device* dev) {
	printc_dbg("Starting JTAG\n");

	unsigned int jtagid = jtag_init(&dev->jtag);
	if (dev->jtag.failed) return -1;

	printc("JTAG ID: 0x%02x\n", jtagid);
	if (jtagid != 0x89 && jtagid != 0x91) {
		printc_err("mehfet: unexpected JTAG ID: 0x%02x\n", jtagid);
		jtag_release_device(&dev->jtag, 0xfffe);
		return -1;
	}

	// JTAG fuse check has been performed, so we can now switch to a
	// higher-speed physical transport suitable for ~350 kHz TCLK strobes used
	// in (and required for) flash programming
	int r = mehfet_cmd_set_clkspeed(dev->trans, true);
	if (r < 0) {
		jtag_release_device(&dev->jtag, 0xfffe);
		return -1;
	}

	return 0;
}


static void mehfet_destroy(device_t dev_base) {
	struct mehfet_device* dev = (struct mehfet_device*)dev_base;

	if (!dev) return;

#ifdef DEBUG_MEHFET_DRIVER
	printc_dbg("mehfet: releasing device & disconnecting\n");
#endif

	if (dev->trans) {
		jtag_release_device(&dev->jtag, 0xfffe); // 0xfffe=reset address : POR

		mehfet_cmd_disconnect(dev->trans);

		dev->trans->ops->destroy(dev->trans);
		dev->trans = NULL;
	}

	free(dev);
}

static device_t mehfet_open(const struct device_args* args) {
	struct mehfet_device* dev;
	int r;

	if ((args->flags & DEVICE_FLAG_TTY)) {
		printc_err("mehfet: this driver does not support TTY access\n");
		return NULL;
	}

	dev = calloc(1, sizeof(*dev));
	if (!dev) {
		printc_err("mehfet: malloc: %s\n", last_error());
		return NULL;
	}

	dev->base.type = &device_mehfet;
	dev->base.max_breakpoints = 2; // TODO
	dev->base.need_probe = 1; // TODO
	dev->jtag.f = &jtdev_func_mehfet;

	// the MehFET currently doesn't have a designated PID, so we can't really
	// default to one.
	dev->trans = mehfet_transport_open(args->path,
			((args->flags & DEVICE_FLAG_HAS_VID_PID) ? &args->vid : NULL),
			((args->flags & DEVICE_FLAG_HAS_VID_PID) ? &args->pid : NULL),
			args->requested_serial);
	if (!dev->trans) goto FAIL;

	enum mehfet_conn useconn;
	r = check_dev_ok(dev, args, &useconn);
	if (r < 0) goto FAIL;

	r = mehfet_cmd_connect(dev->trans, useconn);
	if (r < 0) goto FAIL;

	r = mehfet_cmd_status(dev->trans, &dev->connstat);
	if (r < 0) goto FAIL;

	if (dev->connstat != (useconn & mehfet_conn_typemask)) {
		printc_err("mehfet: could not create connection to device\n");
		goto FAIL;
	}

	r = init_device(dev);
	if (r < 0) goto FAIL;

#ifdef DEBUG_MEHFET_DRIVER
	printc_dbg("mehfet: device opened\n");
#endif

	return (device_t)dev;

FAIL:
	mehfet_destroy((device_t)dev);
	return NULL;
}

const struct device_class device_mehfet = {
	.name = "mehfet",
	.help = "MehFET USB JTAG/SBW device",
	.open     = mehfet_open,
	.destroy  = mehfet_destroy,
	.readmem  = mehfet_readmem,
	.writemem = mehfet_writemem,
	.getregs  = mehfet_getregs,
	.setregs  = mehfet_setregs,
	.ctl      = mehfet_ctl,
	.poll     = mehfet_poll,
	.erase    = mehfet_erase,
	.getconfigfuses = mehfet_getconfigfuses
};

