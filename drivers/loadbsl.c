/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2013 Daniel Beer
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

#include "output.h"
#include "util.h"
#include "loadbsl.h"
#include "loadbsl_fw.h"
#include "bslhid.h"

#define BSL_MAX_CORE			62
#define BSL_MAX_BLOCK			52

#define BSL_CMD_RX_BLOCK		0x10
#define BSL_CMD_RX_BLOCK_FAST		0x1B
#define BSL_CMD_RX_PASSWORD		0x11
#define BSL_CMD_ERASE_SEGMENT		0x12
#define BSL_CMD_UNLOCK_LOCK_INFO	0x13
#define BSL_CMD_MASS_ERASE		0x15
#define BSL_CMD_CRC_CHECK		0x16
#define BSL_CMD_LOAD_PC			0x17
#define BSL_CMD_TX_BLOCK		0x18
#define BSL_CMD_TX_VERSION		0x19
#define BSL_CMD_TX_BUFSIZE		0x1A

#define BSL_PACKET_HEADER		0x80
#define BSL_PACKET_ACK			0x90

/* BSL error codes: from SLAU319C ("MSP430 Programming via the Bootstrap
 * Loader").
 */
static const char *const bsl_error_table[9] = {
	[0x00] = "Success",
	[0x01] = "Flash write check failed",
	[0x02] = "Flash fail bit set",
	[0x03] = "Voltage change during program",
	[0x04] = "BSL locked",
	[0x05] = "BSL password error",
	[0x06] = "Byte write forbidden",
	[0x07] = "Unknown command",
	[0x08] = "Packet length exceeds buffer size"
};

static const char *bsl_error_message(int code)
{
	const char *text = NULL;

	if (code >= 0 && code < ARRAY_LEN(bsl_error_table))
		text = bsl_error_table[code];

	if (text)
		return text;

	return "Unknown error code";
}

struct loadbsl_device {
	struct device		base;
	transport_t		trans;
};

static int send_command(transport_t trans, uint8_t cmd,
			address_t addr, const uint8_t *data, int datalen)
{
	uint8_t outbuf[BSL_MAX_CORE];
	const int addrlen = (addr != ADDRESS_NONE) ? 3 : 0;
	const int corelen = datalen + addrlen + 1;

	if (datalen > BSL_MAX_BLOCK) {
		printc_err("loadbsl: send_command: MAX_BLOCK exceeded: %d\n",
			   datalen);
		return -1;
	}

	outbuf[0] = cmd;

	if (addrlen > 0) {
		outbuf[1] = addr & 0xff;
		outbuf[2] = (addr >> 8) & 0xff;
		outbuf[3] = (addr >> 16) & 0xff;
	}

	memcpy(outbuf + 1 + addrlen, data, datalen);

	if (trans->ops->send(trans, outbuf, corelen) < 0) {
		printc_err("loadbsl: send_command failed\n");
		return -1;
	}

	return 0;
}

static int recv_packet(transport_t trans, uint8_t *data, int max_len)
{
	uint8_t inbuf[BSL_MAX_CORE];
	int len = trans->ops->recv(trans, inbuf, sizeof(inbuf));
	int type;
	int code;

	if (len < 0) {
		printc_err("loadbsl: recv_packet: transport error\n");
		return -1;
	}

	if (len < 1) {
		printc_err("loadbsl: recv_packet: zero-length packet\n");
		return -1;
	}

	type = inbuf[0];
	if (type == 0x3a) {
		const int data_len = len - 1;

		if (!data)
			return 0;

		if (data_len > max_len) {
			printc_err("loadbsl: recv_packet: packet too "
				   "long for buffer (%d bytes)\n", data_len);
			return -1;
		}

		memcpy(data, inbuf + 1, data_len);
		return data_len;
	}

	if (type != 0x3b) {
		printc_err("loadbsl: recv_packet: unknown packet type: "
			   "0x%02x\n", type);
		return -1;
	}

	if (len < 2) {
		printc_err("loadbsl: recv_packet: missing response code\n");
		return -1;
	}

	code = inbuf[1];
	if (code) {
		printc_err("loadbsl: recv_packet: BSL error code: %d (%s)\n",
			   code, bsl_error_message(code));
		return -1;
	}

	return 0;
}

/* Retrieve and display BSL version info. Returns API version byte. */
static int version_check(transport_t trans)
{
	uint8_t data[4];
	int r;

	if (send_command(trans, BSL_CMD_TX_VERSION,
			 ADDRESS_NONE, NULL, 0) < 0) {
		printc_err("loadbsl: failed to send TX_VERSION command\n");
		return -1;
	}

	r = recv_packet(trans, data, 4);
	if (r < 0) {
		printc_err("loadbsl: failed to receive version\n");
		return -1;
	}

	if (r < 4) {
		printc_err("loadbsl: short version response\n");
		return -1;
	}

	printc_dbg("BSL version: [vendor: %02x, int: %02x, "
		   "API: %02x, per: %02x]\n",
		   data[0], data[1], data[2], data[3]);

	return data[2];
}

static int do_writemem(transport_t trans,
		       address_t addr, const uint8_t *mem, address_t len)
{
	while (len) {
		int plen = len;

		if (plen > BSL_MAX_BLOCK)
			plen = BSL_MAX_BLOCK;

		if (send_command(trans, BSL_CMD_RX_BLOCK_FAST,
				 addr, mem, plen) < 0) {
			printc_err("loadbsl: failed to write block "
				   "to 0x%04x\n", addr);
			return -1;
		}

		addr += plen;
		mem += plen;
		len -= plen;
	}

	return 0;
}

static int rx_password(transport_t trans)
{
	uint8_t password[32];

	memset(password, 0xff, sizeof(password));
	if (send_command(trans, BSL_CMD_RX_PASSWORD, ADDRESS_NONE,
			 password, sizeof(password)) < 0 ||
	    recv_packet(trans, NULL, 0) < 0) {
		printc_err("loadbsl: rx_password failed\n");
		return -1;
	}

	return 0;
}

static int check_and_load(transport_t trans)
{
	const struct loadbsl_fw *fw = &loadbsl_fw_usb5xx;
	int api_version = version_check(trans);

	if ((api_version >= 0) && (api_version != 0x80))
		return 0;

	printc_dbg("Uploading BSL firmware (%d bytes at address 0x%04x)...\n",
		   fw->size, fw->prog_addr);

	if (do_writemem(trans, fw->prog_addr, fw->data, fw->size) < 0) {
		printc_err("loadbsl: firmware upload failed\n");
		return -1;
	}

	printc_dbg("Starting new firmware (PC: 0x%04x)...\n", fw->entry_point);

	if (send_command(trans, BSL_CMD_LOAD_PC, fw->entry_point,
			 NULL, 0) < 0) {
		printc_err("loadbsl: PC load failed\n");
		return -1;
	}

	if (trans->ops->suspend && trans->ops->resume &&
	    trans->ops->suspend(trans) < 0) {
		printc_err("loadbsl: transport suspend failed\n");
		return -1;
	}

	printc_dbg("Done, waiting for startup\n");
	delay_ms(1000);

	if (trans->ops->suspend && trans->ops->resume &&
	    trans->ops->resume(trans) < 0) {
		printc_err("loadbsl: transport resume failed\n");
		return -1;
	}

	if (rx_password(trans) < 0) {
		printc_err("loadbsl: failed to unlock new firmware\n");
		return -1;
	}

	return version_check(trans);
}

static void loadbsl_destroy(device_t base)
{
	struct loadbsl_device *dev = (struct loadbsl_device *)base;
	static const uint8_t puc_word[] = {0, 0};

	/* Write 0x0000 to WDTCTL, triggering a PUC */
	if (send_command(dev->trans, BSL_CMD_RX_BLOCK_FAST,
			 0x15c, puc_word, sizeof(puc_word)) < 0)
		printc_err("warning: loadbsl: failed to trigger PUC\n");

	dev->trans->ops->destroy(dev->trans);
	free(dev);
}

static int loadbsl_readmem(device_t base, address_t addr,
			   uint8_t *mem, address_t len)
{
	struct loadbsl_device *dev = (struct loadbsl_device *)base;

	while (len) {
		int plen = len;
		uint8_t len_param[2];
		int r;

		if (plen > BSL_MAX_BLOCK)
			plen = BSL_MAX_BLOCK;

		len_param[0] = plen & 0xff;
		len_param[1] = plen >> 8;

		if (send_command(dev->trans, BSL_CMD_TX_BLOCK,
				 addr, len_param, 2) < 0)
			goto fail;

		r = recv_packet(dev->trans, mem, plen);
		if (r < 0)
			goto fail;

		if (r < plen) {
			printc_err("loadbsl: short response to "
				   "memory read\n");
			return -1;
		}

		addr += plen;
		mem += plen;
		len -= plen;
	}

	return 0;

fail:
	printc_err("loadbsl: failed to read block from 0x%04x\n", addr);
	return -1;
}

static int loadbsl_writemem(device_t base, address_t addr,
			    const uint8_t *mem, address_t len)
{
	struct loadbsl_device *dev = (struct loadbsl_device *)base;

	return do_writemem(dev->trans, addr, mem, len);
}

static int loadbsl_getregs(device_t base, address_t *regs)
{
	(void)base;
	(void)regs;

	printc_err("loadbsl: register fetch is not implemented\n");
	return -1;
}

static int loadbsl_setregs(device_t base, const address_t *regs)
{
	(void)base;
	(void)regs;

	printc_err("loadbsl: register store is not implemented\n");
	return -1;
}

static int loadbsl_erase(device_t base, device_erase_type_t type,
			 address_t addr)
{
	struct loadbsl_device *dev = (struct loadbsl_device *)base;

	switch (type) {
	case DEVICE_ERASE_ALL:
		printc_err("loadbsl: ERASE_ALL not supported\n");
		return -1;

	case DEVICE_ERASE_MAIN:
		if (send_command(dev->trans, BSL_CMD_MASS_ERASE,
				 ADDRESS_NONE, NULL, 0) < 0 ||
		    recv_packet(dev->trans, NULL, 0) < 0) {
			printc_err("loadbsl: ERASE_MAIN failed\n");
			return -1;
		}
		break;

	case DEVICE_ERASE_SEGMENT:
		if (send_command(dev->trans, BSL_CMD_ERASE_SEGMENT,
				 addr, NULL, 0) < 0 ||
		    recv_packet(dev->trans, NULL, 0) < 0) {
			printc_err("loadbsl: ERASE_SEGMENT failed\n");
			return -1;
		}
		break;
	}

	return 0;
}

static int loadbsl_ctl(device_t base, device_ctl_t type)
{
	(void)base;

	switch (type) {
	case DEVICE_CTL_HALT:
	case DEVICE_CTL_RESET:
		return 0;

	default:
		printc_err("loadbsl: CPU control is not possible\n");
		return -1;
	}

	return 0;
}

static device_status_t loadbsl_poll(device_t base)
{
	(void)base;

	return DEVICE_STATUS_HALTED;
}

static device_t loadbsl_open(const struct device_args *args)
{
	struct loadbsl_device *dev;

	if (args->flags & DEVICE_FLAG_TTY) {
		printc_err("loadbsl: this driver does not support "
			   "tty access\n");
		return NULL;
	}

	dev = malloc(sizeof(*dev));
	memset(dev, 0, sizeof(*dev));

	dev->base.type = &device_loadbsl;
	dev->base.max_breakpoints = 0;

#if defined(__APPLE__)
	dev->trans = bslosx_open(args->path, args->requested_serial);
#else
	dev->trans = bslhid_open(args->path, args->requested_serial);
#endif
	if (!dev->trans) {
		free(dev);
		return NULL;
	}

	if (rx_password(dev->trans) < 0) {
		printc_dbg("loadbsl: retrying password...\n");

		if (rx_password(dev->trans) < 0) {
			dev->trans->ops->destroy(dev->trans);
			free(dev);
			return NULL;
		}
	}

	if (check_and_load(dev->trans) < 0) {
		dev->trans->ops->destroy(dev->trans);
		free(dev);
		return NULL;
	}

	return &dev->base;
}

const struct device_class device_loadbsl = {
	.name		= "load-bsl",
	.help		= "Loadable USB BSL driver (USB 5xx/6xx).",
	.open		= loadbsl_open,
	.destroy	= loadbsl_destroy,
	.readmem	= loadbsl_readmem,
	.writemem	= loadbsl_writemem,
	.erase		= loadbsl_erase,
	.getregs	= loadbsl_getregs,
	.setregs	= loadbsl_setregs,
	.ctl		= loadbsl_ctl,
	.poll		= loadbsl_poll,
	.getconfigfuses = NULL
};
