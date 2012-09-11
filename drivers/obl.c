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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "util.h"
#include "output.h"
#include "obl.h"

#define IMAGE_MAGIC		0xd1261176
#define FLASH_PAGE_SIZE		1024
#define COPY_OFFSET		0x38000
#define COPY_VALID_ADDR		0x7dff0

typedef enum {
	OBL_CMD_READ_RAM	= 0x01,
	OBL_CMD_WRITE_RAM	= 0x02,
	OBL_CMD_READ_FLASH	= 0x03,
	OBL_CMD_WRITE_FLASH	= 0x04,
	OBL_CMD_RF_SELF_TEST	= 0x05,
	OBL_CMD_SET_PROTECTION	= 0x06,
	OBL_CMD_DEV_RESET	= 0x07,
	OBL_CMD_DEV_VERSION	= 0x08,
	OBL_CMD_PROD_TEST	= 0x09
} obl_cmd_t;

typedef enum {
	OBL_RESULT_OK			= 0x00,
	OBL_RESULT_NRF_SPI_FAULT	= 0x01,
	OBL_RESULT_NRF_LINK_FAULT	= 0x02,
	OBL_RESULT_COMMAND_FAULT	= 0xff
} obl_result_t;

struct progress_meter {
	uint32_t	total;
	uint32_t	last;
	int		interval_shift;
};

static void progress_init(struct progress_meter *m, uint32_t size)
{
	m->total = size;
	m->last = 0;
	m->interval_shift = 0;

	while (size > 30) {
		size >>= 1;
		m->interval_shift++;
	}
}

static void progress_update(struct progress_meter *m, const char *label,
			    uint32_t cur)
{
	if (!((m->last ^ cur) >> m->interval_shift))
		return;

	m->last = cur;
	printc("%s: %8d/%8d [%3d%%]\n",
	       label, cur, m->total, cur * 100 / m->total);
}

static int transport_read_all(transport_t tr, uint8_t *data, int len)
{
	while (len > 0) {
		int r = tr->ops->recv(tr, data, len);

		if (r <= 0)
			return -1;

		data += r;
		len -= r;
	}

	return 0;
}

static int obl_xfer(transport_t tr, const uint8_t *command, int cmd_len,
		    uint8_t *recv_data, int recv_len)
{
	uint8_t result;

	if (tr->ops->set_modem(tr, TRANSPORT_MODEM_DTR) < 0) {
		printc_err("obl_xfer: failed to activate DTR\n");
		return -1;
	}

	if (tr->ops->send(tr, command, cmd_len) < 0) {
		printc_err("obl_xfer: failed to send command\n");
		goto fail;
	}

	if (tr->ops->recv(tr, &result, 1) < 0) {
		printc_err("obl_xfer: failed to read status byte\n");
		goto fail;
	}

	if (result != OBL_RESULT_OK) {
		printc_err("obl_xfer: device error code: 0x%02x\n",
			   result);
		goto fail;
	}

	if (recv_len && transport_read_all(tr, recv_data, recv_len) < 0) {
		printc_err("obl_xfer: failed to read data\n");
		goto fail;
	}

	tr->ops->set_modem(tr, 0);
	return 0;

fail:
	tr->ops->set_modem(tr, 0);
	return -1;
}

static uint8_t *read_file(const char *filename, unsigned int *len_ret)
{
	FILE *in = fopen(filename, "rb");
	unsigned int len;
	uint8_t *buf;

	if (!in) {
		printc_err("Can't open %s for reading: %s\n",
			   filename, last_error());
		return NULL;
	}

	if (fseek(in, 0, SEEK_END) < 0) {
		printc_err("Can't determine file size: %s: %s\n",
			   filename, last_error());
		fclose(in);
		return NULL;
	}

	len = ftell(in);
	rewind(in);

	buf = malloc(len);
	if (!buf) {
		printc_err("Can't allocate memory for "
			   "firmware image: %s: %s\n",
			   filename, last_error());
		fclose(in);
		return NULL;
	}

	if (fread(buf, len, 1, in) != 1) {
		printc_err("Failed to read %s: %s\n",
			   filename, last_error());
		free(buf);
		fclose(in);
		return NULL;
	}

	fclose(in);

	*len_ret = len;
	return buf;
}

static int obl_read_mem(transport_t tr, uint32_t addr,
			uint8_t *data, uint32_t size)
{
	uint8_t cmd[9];

	cmd[0] = OBL_CMD_READ_RAM;
	cmd[1] = addr & 0xff;
	cmd[2] = (addr >> 8) & 0xff;
	cmd[3] = (addr >> 16) & 0xff;
	cmd[4] = (addr >> 24) & 0xff;
	cmd[5] = size & 0xff;
	cmd[6] = (size >> 8) & 0xff;
	cmd[7] = (size >> 16) & 0xff;
	cmd[8] = (size >> 24) & 0xff;

	if (obl_xfer(tr, cmd, sizeof(cmd), data, size) < 0) {
		printc_err("obl_read_mem: failed to read %d bytes from "
			   "0x%x\n", size, addr);
		return -1;
	}

	return 0;
}

static int obl_write_flash(transport_t tr, uint32_t addr,
			   const uint8_t *data, uint32_t size)
{
	uint8_t cmd[size + 9];

	cmd[0] = OBL_CMD_WRITE_FLASH;
	cmd[1] = addr & 0xff;
	cmd[2] = (addr >> 8) & 0xff;
	cmd[3] = (addr >> 16) & 0xff;
	cmd[4] = (addr >> 24) & 0xff;
	cmd[5] = size & 0xff;
	cmd[6] = (size >> 8) & 0xff;
	cmd[7] = (size >> 16) & 0xff;
	cmd[8] = (size >> 24) & 0xff;

	memcpy(cmd + 9, data, size);

	if (obl_xfer(tr, cmd, size + 9, NULL, 0) < 0) {
		printc_err("obl_write_flash: failed to write %d bytes to "
			   "0x%x\n", size, addr);
		return -1;
	}

	return 0;
}

static int write_image(transport_t tr, uint32_t addr, uint32_t size,
		       const uint8_t *data)
{
	struct progress_meter pm;
	uint32_t i;

	progress_init(&pm, size);

	for (i = 0; i < size; i += FLASH_PAGE_SIZE) {
		const uint32_t offset = i + addr + COPY_OFFSET;
		int r;

		if (i + FLASH_PAGE_SIZE < size) {
			r = obl_write_flash(tr, offset,
					    data + i, FLASH_PAGE_SIZE);
		} else {
			uint8_t partial[FLASH_PAGE_SIZE];

			memset(partial, 0xff, sizeof(partial));
			memcpy(partial, data + i, size - i);
			r = obl_write_flash(tr, offset, partial,
					    FLASH_PAGE_SIZE);
		}

		if (r < 0) {
			printc_err("Write failed at offset 0x%x\n", i);
			return -1;
		}

		progress_update(&pm, "Writing", i);
	}

	return 0;
}

static int verify_image(transport_t tr, uint32_t addr, uint32_t size,
			const uint8_t *data)
{
	struct progress_meter pm;
	uint32_t i;

	progress_init(&pm, size);

	for (i = 0; i < size; i += FLASH_PAGE_SIZE) {
		const uint32_t offset = i + addr + COPY_OFFSET;
		uint8_t buf[FLASH_PAGE_SIZE];
		int is_ok = 1;

		if (obl_read_mem(tr, offset, buf, FLASH_PAGE_SIZE) < 0) {
			printc_err("Read error at offset 0x%x\n", i);
			return -1;
		}

		if (i + FLASH_PAGE_SIZE < size) {
			if (memcmp(buf, data + i, FLASH_PAGE_SIZE))
				is_ok = 0;
		} else {
			int j;

			if (memcmp(buf, data + i, size - i))
				is_ok = 0;
			for (j = size - i; j < FLASH_PAGE_SIZE; j++)
				if (buf[j] != 0xff)
					is_ok = 0;
		}

		if (!is_ok) {
			printc_err("Verification failed at flash "
				   "page offset 0x%x\n", i);
			return -1;
		}

		progress_update(&pm, "Verifying", i);
	}

	return 0;
}

static int write_valid_size(transport_t tr, uint32_t size)
{
	uint8_t buf[FLASH_PAGE_SIZE];
	const int page = COPY_VALID_ADDR & ~(FLASH_PAGE_SIZE - 1);
	const int offset = COPY_VALID_ADDR & (FLASH_PAGE_SIZE - 1);

	memset(buf, 0xff, sizeof(buf));
	buf[offset] = size & 0xff;
	buf[offset + 1] = (size >> 8) & 0xff;
	buf[offset + 2] = (size >> 16) & 0xff;
	buf[offset + 3] = (size >> 24) & 0xff;

	if (obl_write_flash(tr, page, buf, FLASH_PAGE_SIZE) < 0) {
		printc_err("Failed to write image-valid marker\n");
		return -1;
	}

	return 0;
}

int obl_get_version(transport_t tr, uint32_t *ver_ret)
{
	static const uint8_t cmd = OBL_CMD_DEV_VERSION;
	uint8_t buf[4];
	uint32_t version;

	if (obl_xfer(tr, &cmd, 1, buf, 4) < 0) {
		printc_err("warning: obl_get_version: unable to retrieve "
			   "Olimex firmware version\n");
		return -1;
	}

	version = LE_LONG(buf, 0);

	if (ver_ret)
		*ver_ret = version;

	return 0;
}

static int load_image(transport_t trans, const uint8_t *file_data,
		      unsigned int file_len, const char *image_filename)
{
	uint32_t image_offset;
	uint32_t image_size;

	if (file_len < 16 || LE_LONG(file_data, 0) != IMAGE_MAGIC) {
		printc_err("Invalid firmware image: %s\n", image_filename);
		return -1;
	}

	image_offset = LE_LONG(file_data, 8);
	image_size = LE_LONG(file_data, 12);

	printc_dbg("Firmware image version: %x: %d bytes at offset 0x%x\n",
		   LE_LONG(file_data, 4), image_size, image_offset);

	if (image_size + 16 != file_len) {
		printc_err("Image length mismatch: %s\n", image_filename);
		return -1;
	}

	if (write_image(trans, image_offset, image_size,
			file_data + 16) < 0)
		return -1;

	if (verify_image(trans, image_offset, image_size,
			 file_data + 16) < 0)
		return -1;

	if (write_valid_size(trans, image_size) < 0)
		return -1;

	printc("Firmware update successful\n");
	return 0;
}

int obl_update(transport_t trans, const char *image_filename)
{
	uint8_t *file_data;
	unsigned int file_len;

	file_data = read_file(image_filename, &file_len);
	if (!file_data)
		return -1;

	if (load_image(trans, file_data, file_len, image_filename) < 0) {
		free(file_data);
		return -1;
	}

	free(file_data);
	return 0;
}

int obl_reset(transport_t trans)
{
	const uint8_t cmd = OBL_CMD_DEV_RESET;

	if (obl_xfer(trans, &cmd, 1, NULL, 0) < 0) {
		printc_err("Device reset failed\n");
		return -1;
	}

	return 0;
}
