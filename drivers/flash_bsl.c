/* MSPDebug - debugging tool for the eZ430
 * Copyright (C) 2009, 2010 Daniel Beer
 * Copyright (C) 2010 Andrew Armenia
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
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#include "flash_bsl.h"
#include "util.h"
#include "output.h"
#include "fet_error.h"
#include "sport.h"
#include "bsllib.h"

struct flash_bsl_device {
	struct device   base;

	sport_t		serial_fd;
	int		long_password;
	const struct device_args *args;

	const char	*seq;
};

#define MAX_BLOCK	256

/* This should be at least MAX_BLOCK + 4 */
#define MAX_PACKET	512

/* adapted from TI's published BSL source code */
#define CRC_INIT 0xffff
static uint16_t crc_ccitt(const uint8_t *data, int len) {
	uint16_t crc = CRC_INIT;
        uint16_t temp;
	int i;

	for (i = 0; i < len; ++i) {
		temp = ((crc >> 8) ^ data[i]) & 0xff;
		temp ^= (temp >> 4);
		crc = (crc << 8) ^ (temp << 12) ^ (temp << 5) ^ temp;
	}

	return crc;
}

static void crc_selftest(void) {
	/* These test vectors are from page 30 of TI doc SLAU319A */
	uint16_t crc_expected = 0x5590;
        uint16_t crc_actual = crc_ccitt((uint8_t *)"\x52\x02", 2);
	if (crc_expected != crc_actual) {
		printc_err("flash_bsl: CRC malfunction "
			   "(expected 0x%04x got 0x%04x)\n",
			crc_expected, crc_actual);
	}

	crc_expected = 0x121d;
	crc_actual = crc_ccitt((uint8_t *)"\x3a\x04\x01", 3);
	if (crc_expected != crc_actual) {
		printc_err("flash_bsl: CRC malfunction "
			   "(expected 0x%04x got 0x%04x)\n",
			crc_expected, crc_actual);
	}

	crc_expected = 0x528b;
	crc_actual = crc_ccitt((uint8_t *)"\x1a", 1);
	if (crc_expected != crc_actual) {
		printc_err("flash_bsl: CRC malfunction "
			   "(expected 0x%04x got 0x%04x)\n",
			crc_expected, crc_actual);
	}

}

#define RX_DATA_BLOCK 0x10
#define RX_DATA_BLOCK_FAST 0x1b
#define RX_PASSWORD 0x11
#define ERASE_SEGMENT 0x12
#define UNLOCK_LOCK_INFO 0x13
#define MASS_ERASE 0x15
#define CRC_CHECK 0x16
#define LOAD_PC 0x17
#define TX_DATA_BLOCK 0x18
#define TX_BSL_VERSION 0x19
#define TX_BUFFER_SIZE 0x1a

static int flash_bsl_send(struct flash_bsl_device *dev,
			  const uint8_t *data, int len)
{
	uint16_t crc;
	uint8_t cmd_buf[MAX_PACKET + 5];
	uint8_t response;

#if defined(FLASH_BSL_VERBOSE)
        debug_hexdump("flash_bsl: sending", data, len);
#endif

	crc = crc_ccitt(data, len);

	if (len > MAX_PACKET) {
		printc_err("flash_bsl: attempted to transmit "
			   "long packet (len=%d)\n", len);
		return -1;
	}

	cmd_buf[0] = 0x80;
	cmd_buf[1] = (len & 0xff);
	cmd_buf[2] = (len >> 8) & 0xff;
	memcpy(cmd_buf + 3, data, len);
	cmd_buf[len + 3] = (crc & 0xff);
	cmd_buf[len + 4] = (crc >> 8) & 0xff;

	if (sport_write_all(dev->serial_fd, cmd_buf, len + 5) < 0) {
		printc_err("flash_bsl: serial write failed: %s\n",
			   last_error());
		return -1;
	}

	if (sport_read_all(dev->serial_fd, &response, 1) < 0) {
		printc_err("flash_bsl: serial read failed: %s\n",
			   last_error());
		return -1;
	}

	if (response != 0) {
		switch (response) {
		case 0x51:
			printc_err("flash_bsl: BSL reports incorrect "
				   "packet header\n");
			break;
		case 0x52:
			printc_err("flash_bsl: BSL reports checksum "
				   "incorrect\n");
			break;
		case 0x53:
			printc_err("flash_bsl: BSL got zero-size packet\n");
			break;
		case 0x54:
			printc_err("flash_bsl: BSL receive buffer "
				   "overflowed\n");
			break;
		case 0x55:
			printc_err("flash_bsl: (known-)unknown error\n");
			break;
		case 0x56:
			printc_err("flash_bsl: unknown baud rate\n");
			break;
		default:
			printc_err("flash_bsl: unknown unknown error\n");
			break;
		}
		return -1;
	}

	return 0;
}

static int flash_bsl_recv(struct flash_bsl_device *dev,
                          uint8_t *recv_buf, int buf_len)
{
	uint8_t header[3];
	uint8_t crc_bytes[2];
	uint16_t recv_len;
	uint16_t crc_value;

	if (sport_read_all(dev->serial_fd, header, 3) < 0) {
		printc_err("flash_bsl: read response failed: %s\n",
			   last_error());
		return -1;
	}

	if (header[0] != 0x80) {
		printc_err("flash_bsl: incorrect response header received\n");
		return -1;
	}

	recv_len = header[2];
	recv_len <<= 8;
	recv_len |= header[1];

#if defined(FLASH_BSL_VERBOSE)
        printc_dbg("flash_bsl: incoming message length %d\n", recv_len);
#endif

	if (recv_len > buf_len) {
		printc_err("flash_bsl: insufficient buffer to receive data\n");
		return -1;
	}

	if (sport_read_all(dev->serial_fd, recv_buf, recv_len) < 0) {
		perror("receive message");
		printc_err("flash_bsl: error receiving message\n");
		return -1;
	}

	if (sport_read_all(dev->serial_fd, crc_bytes, 2) < 0) {
		perror("receive message CRC");
		printc_err("flash_bsl: error receiving message CRC\n");
		return -1;
	}

	crc_value = crc_bytes[1];
	crc_value <<= 8;
	crc_value |= crc_bytes[0];

	if (crc_ccitt(recv_buf, recv_len) != crc_value) {
		printc_err("flash_bsl: received message with bad CRC\n");
		return -1;
	}

#if defined(FLASH_BSL_VERBOSE)
        debug_hexdump("received message", recv_buf, recv_len);
#endif

	delay_ms(10);
	return recv_len;
}

static void flash_bsl_perror(uint8_t code) {
	switch (code) {
		case 0x00:
			printc_err("flash_bsl: success\n");
			break;
		case 0x01:
			printc_err("flash_bsl: FLASH verify failed\n");
			break;
		case 0x02:
			printc_err("flash_bsl: FLASH operation failed\n");
			break;
		case 0x03:
			printc_err("flash_bsl: voltage not constant during program\n");
			break;
		case 0x04:
			printc_err("flash_bsl: BSL is locked\n");
			break;
		case 0x05:
			printc_err("flash_bsl: incorrect password\n");
			break;
		case 0x06:
			printc_err("flash_bsl: attempted byte write to FLASH\n");
			break;
		case 0x07:
			printc_err("flash_bsl: unrecognized command\n");
			break;
		case 0x08:
			printc_err("flash_bsl: command was too long\n");
			break;
		default:
			printc_err("flash_bsl: unknown status message\n");
			break;

	}
}

static int flash_bsl_readmem(device_t dev_base,
			     address_t addr, uint8_t *mem, address_t len)
{
	struct flash_bsl_device *dev = (struct flash_bsl_device *)dev_base;
	uint8_t recv_buf[MAX_BLOCK*2];
	uint8_t send_buf[64];
	uint16_t read_size;
	int ret;

	if (addr > 0xfffff || addr + len > 0x100000) {
		printc_err("flash_bsl: read exceeds possible range\n");
		return -1;
	}

	while (len > 0) {
		if (len > MAX_BLOCK) {
			read_size = MAX_BLOCK;
		} else {
			read_size = len;
		}

		/* build command */
		send_buf[0] = TX_DATA_BLOCK; /* command: transmit data block */
		send_buf[1] = addr & 0xff;
		send_buf[2] = (addr >> 8) & 0xff;
		send_buf[3] = (addr >> 16) & 0xff;
		send_buf[4] = read_size & 0xff;
		send_buf[5] = (read_size >> 8) & 0xff;

		if (flash_bsl_send(dev, send_buf, 6) < 0) {
			printc_err("flash_bsl readmem: send failed\n");
			return -1;
		}

		ret = flash_bsl_recv(dev, recv_buf, read_size + 1);
		if (ret < 0) {
			printc_err("flash_bsl readmem: receive failed\n");
			return -1;
		} else if (ret < read_size) {
			printc_err("flash_bsl readmem: warning: not all requested data received\n");
		}

		if (recv_buf[0] == 0x3a) {
			memcpy(mem, recv_buf + 1, ret - 1);
			addr += ret - 1;
			len -= ret - 1;
			mem += ret - 1;
		} else if (recv_buf[0] == 0x3b) {
			flash_bsl_perror(recv_buf[1]);
		} else {
			printc_err("flash_bsl readmem: invalid response\n");
			return -1;
		}

	}

	return 0;
}

static int flash_bsl_erase(device_t dev_base, device_erase_type_t type,
			   address_t addr)
{
	struct flash_bsl_device *dev = (struct flash_bsl_device *)dev_base;
	uint8_t erase_cmd[4];
	uint8_t response_buffer[16];
	int ret;

	if (type == DEVICE_ERASE_ALL) {
		printc_err("flash_bsl_erase: simultaneous code/info erase not supported\n");
		return -1;
	} else if (type == DEVICE_ERASE_MAIN) {
		erase_cmd[0] = MASS_ERASE;
		if (flash_bsl_send(dev, erase_cmd, 1) < 0) {
			printc_err("flash_bsl_erase: failed to send erase command\n");
			return -1;
		}
        } else if (type == DEVICE_ERASE_SEGMENT) {
		erase_cmd[0] = ERASE_SEGMENT;
		erase_cmd[1] = addr & 0xff;
		erase_cmd[2] = (addr >> 8) & 0xff;
		erase_cmd[3] = (addr >> 16) & 0xff;
		if (flash_bsl_send(dev, erase_cmd, 4) < 0) {
			printc_err("flash_bsl_erase: failed to send erase command\n");
			return -1;
		}
	} else {
		printc_err("flash_bsl_erase: unsupported erase type\n");
		return -1;
	}

	ret = flash_bsl_recv(dev, response_buffer, sizeof(response_buffer));
	if (ret < 2) {
		printc_err("flash_bsl_erase: no response\n");
		return -1;
	}

	if (response_buffer[0] != 0x3b) {
		printc_err("flash_bsl_erase: incorrect response\n");
		return -1;
	}

	if (response_buffer[1] != 0) {
		flash_bsl_perror(response_buffer[1]);
		printc_err("flash_bsl_erase: erase failed\n");
		return -1;
	} else {
#if defined(FLASH_BSL_VERBOSE)
		printc_dbg("flash_bsl_erase: success\n");
#endif
	}

	return 0;
}

static int flash_bsl_unlock(struct flash_bsl_device *dev)
{
	/*
	 * after erase, the password will be 0xff * (16 or 32)
	 * (an empty interrupt vector table)
	 */
	uint8_t rx_password_cmd[33] = "\x11"
		"\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff";



	uint8_t response_buffer[16];
	int ret;

	/* mass erase - this might wipe Information Memory on some devices */
        /* (according to the documentation it should not) */
	if (flash_bsl_erase((device_t)dev, DEVICE_ERASE_MAIN, 0) < 0) {
		printc_err("flash_bsl_unlock: warning: erase failed\n");
	}

	/* send password (which is now erased FLASH) */
	if (dev->long_password) {
#if defined(FLASH_BSL_VERBOSE)
		printc_dbg("flash_bsl_unlock: using long password\n");
#endif
	}

	if (flash_bsl_send(dev, rx_password_cmd,
			   dev->long_password ? 33 : 17) < 0) {
		printc_err("flash_bsl_unlock: send password failed\n");
		return -1;
	}

	ret = flash_bsl_recv(dev, response_buffer, sizeof(response_buffer));

	if (ret < 2) {
		printc_err("flash_bsl_unlock: error receiving password response\n");
		return -1;
	}

	if (response_buffer[0] != 0x3b) {
		printc_err("flash_bsl_unlock: received invalid password response\n");
		return -1;
	}

	if (response_buffer[1] != 0x00) {
		flash_bsl_perror(response_buffer[1]);
		printc_err("flash_bsl_unlock: password error\n");
		return -1;
	}

	return 0;
}

static int flash_bsl_ctl(device_t dev_base, device_ctl_t type)
{
	(void)dev_base;

	switch (type) {
	case DEVICE_CTL_HALT:
		/* Ignore halt requests */
		return 0;

	case DEVICE_CTL_RESET:
		/* Ignore reset requests */
		return 0;

	default:
		printc_err("flash_bsl: CPU control is not possible\n");
	}

	return -1;
}

static device_status_t flash_bsl_poll(device_t dev_base)
{
	(void)dev_base;

	return DEVICE_STATUS_HALTED;
}

static int flash_bsl_getregs(device_t dev_base, address_t *regs)
{
	(void)dev_base;
	(void)regs;

	printc_err("flash_bsl: register fetch is not implemented\n");
	return -1;
}

static int flash_bsl_setregs(device_t dev_base, const address_t *regs)
{
	(void)dev_base;
	(void)regs;

	printc_err("flash_bsl: register store is not implemented\n");
	return -1;
}

static int flash_bsl_writemem(device_t dev_base,
			address_t addr, const uint8_t *mem, address_t len)
{
	struct flash_bsl_device *dev = (struct flash_bsl_device *)dev_base;
	uint8_t send_buf[2*MAX_BLOCK];
	uint8_t recv_buf[16];
	uint16_t write_size;
	int n_recv;

	if (addr > 0xfffff || addr + len > 0x100000) {
		printc_err("flash_bsl: write exceeds possible range\n");
		return -1;
	}

	while (len > 0) {
		/* compute size of this write operation */
		if (len > MAX_BLOCK) {
			write_size = MAX_BLOCK;
		} else {
			write_size = len;
		}

		/* build write command */
		/* command */
		send_buf[0] = RX_DATA_BLOCK;
		/* address */
		send_buf[1] = addr & 0xff;
		send_buf[2] = (addr >> 8) & 0xff;
		send_buf[3] = (addr >> 16) & 0xff;
		/* data */
		memcpy(&send_buf[4], mem, write_size);

		addr += write_size;
		mem += write_size;
		len -= write_size;

		/* send command */
		if (flash_bsl_send(dev, send_buf, write_size + 4) < 0) {
			printc_err("flash_bsl: send failed\n");
			return -1;
		}

		/* receive and check response */
		n_recv = flash_bsl_recv(dev, recv_buf, sizeof(recv_buf));

		if (n_recv < 0) {
			printc_err("flash_bsl write: error occurred receiving response\n");
			return -1;
		} else if (n_recv < 2) {
			printc_err("flash_bsl write: response too short\n");
			return -1;
		} else if (recv_buf[0] != 0x3b) {
			printc_err("flash_bsl write: invalid response received\n");
			return -1;
		} else if (recv_buf[1] != 0x00) {
			printc_err("flash_bsl write: BSL reported write error: ");
			flash_bsl_perror(recv_buf[1]);
			return -1;
		}
		/* else success! */
	}

	return 0;
}

static void flash_bsl_destroy(device_t dev_base)
{
	struct flash_bsl_device *dev = (struct flash_bsl_device *)dev_base;

	if ( dev->args->bsl_gpio_used )
	{
		bsllib_seq_do_gpio(dev->args->bsl_gpio_rts, dev->args->bsl_gpio_dtr,
							bsllib_seq_next(dev->seq));
	}
	else
	{
		bsllib_seq_do(dev->serial_fd, bsllib_seq_next(dev->seq));
	}
	sport_close(dev->serial_fd);
	free(dev);
}

static device_t flash_bsl_open(const struct device_args *args)
{
	struct flash_bsl_device *dev;
	uint8_t tx_bsl_version_command[] = { TX_BSL_VERSION };
	uint8_t tx_bsl_version_response[5];

	if (!(args->flags & DEVICE_FLAG_TTY)) {
		printc_err("This driver does not support raw USB access.\n");
		return NULL;
	}

	dev = malloc(sizeof(*dev));
	if (!dev) {
		pr_error("flash_bsl: can't allocate memory");
		return NULL;
	}


	crc_selftest( );

	memset(dev, 0, sizeof(*dev));
	dev->base.type = &device_flash_bsl;
	dev->args = args;

	dev->serial_fd = sport_open(args->path, 9600, SPORT_EVEN_PARITY);
	if (SPORT_ISERR(dev->serial_fd)) {
		printc_err("flash_bsl: can't open %s: %s\n",
			   args->path, last_error());
		free(dev);
		return NULL;
	}

	dev->seq = args->bsl_entry_seq;
	if (!dev->seq)
		dev->seq = "dR,r,R,r,R,D:dR,DR";

	dev->long_password = args->flags & DEVICE_FLAG_LONG_PW;

	/* enter bootloader */
	if ( args->bsl_gpio_used )
	{
		if (bsllib_seq_do_gpio(args->bsl_gpio_rts, args->bsl_gpio_dtr, dev->seq) < 0) {
      printc_err("BSL entry sequence failed\n");
			goto fail;
		}
	}
	else
	{
		if (bsllib_seq_do(dev->serial_fd, dev->seq) < 0) {
      printc_err("BSL entry sequence failed\n");
			goto fail;
		}
	}

	delay_ms(500);

	/* unlock device (erase then send password) */
	if (flash_bsl_unlock(dev) < 0) {
		goto fail;
	}


	if (flash_bsl_send(dev, tx_bsl_version_command,
			   sizeof(tx_bsl_version_command)) < 0) {
		printc_err("flash_bsl: failed to read BSL version");
		goto fail;
	}

	if (flash_bsl_recv(dev, tx_bsl_version_response,
	    sizeof(tx_bsl_version_response)) <
	    sizeof(tx_bsl_version_response)) {
		printc_err("flash_bsl: BSL responded with invalid version");
		goto fail;
	}

	debug_hexdump("BSL version", tx_bsl_version_response,
            sizeof(tx_bsl_version_response));

	return (device_t)dev;

 fail:
	sport_close(dev->serial_fd);
	free(dev);
	return NULL;
}

const struct device_class device_flash_bsl = {
	.name		= "flash-bsl",
	.help = "TI generic flash-based bootloader via RS-232",
	.open		= flash_bsl_open,
	.destroy	= flash_bsl_destroy,
	.readmem	= flash_bsl_readmem,
	.writemem	= flash_bsl_writemem,
	.getregs	= flash_bsl_getregs,
	.setregs	= flash_bsl_setregs,
	.ctl		= flash_bsl_ctl,
	.poll		= flash_bsl_poll,
	.erase		= flash_bsl_erase,
	.getconfigfuses = NULL
};
