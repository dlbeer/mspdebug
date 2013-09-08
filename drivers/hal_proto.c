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

#include <string.h>
#include "util.h"
#include "output.h"
#include "hal_proto.h"

void hal_proto_init(struct hal_proto *p, transport_t trans,
		    hal_proto_flags_t flags)
{
	memset(p, 0, sizeof(*p));

	p->trans = trans;
	p->flags = flags;
	p->ref_id = 0;
}

int hal_proto_send(struct hal_proto *p, hal_proto_type_t type,
		   const uint8_t *data, int length)
{
	uint8_t buf[512];
	size_t len = 0;

	if (length > HAL_MAX_PAYLOAD) {
		printc_err("hal_proto_send: payload too long: %d\n", length);
		return -1;
	}

	buf[len++] = length + 3;
	buf[len++] = type;
	buf[len++] = p->ref_id;
	buf[len++] = 0;

	p->ref_id = (p->ref_id + 1) & 0x7f;

	memcpy(buf + len, data, length);
	len += length;

	if (len & 1)
		buf[len++] = 0;

	if (p->flags & HAL_PROTO_CHECKSUM) {
		size_t i;
		uint8_t sum_l = 0xff;
		uint8_t sum_h = 0xff;

		for (i = 0; i < len; i += 2) {
			sum_l ^= buf[i];
			sum_h ^= buf[i + 1];
		}

		buf[len++] = sum_l;
		buf[len++] = sum_h;
	}

	if (p->trans->ops->send(p->trans, buf, len) < 0) {
		printc_err("hal_proto_send: type: 0x%02x\n", type);
		return -1;
	}

	return 0;
}

int hal_proto_receive(struct hal_proto *p, uint8_t *buf, int max_len)
{
	uint8_t rx_buf[512];
	uint8_t sum_h = 0xff;
	uint8_t sum_l = 0xff;
	int rx_len = 0;
	int len;
	int i;

	for (;;) {
		int r = p->trans->ops->recv(p->trans, rx_buf + rx_len,
					    sizeof(rx_buf) - rx_len);

		if (r <= 0) {
			printc_err("hal_proto_recv: read error\n");
			return -1;
		}

		rx_len += r;

		if (rx_len) {
			const size_t expect_len =
				rx_buf[0] + 4 - (rx_buf[0] & 1);

			if (rx_len == expect_len)
				break;

			if (rx_len > expect_len) {
				printc_err("hal_proto_recv: length "
					   "mismatch\n");
				return -1;
			}
		}
	}

	if (rx_len < 6) {
		printc_err("hal_proto_recv: short read: %d\n", rx_len);
		return -1;
	}

	for (i = 0; i < rx_len; i += 2) {
		sum_h ^= rx_buf[i];
		sum_l ^= rx_buf[i + 1];
	}

	if (sum_h || sum_l) {
		printc_err("hal_proto_recv: bad checksum\n");
		return -1;
	}

	len = rx_buf[0] - 3;
	p->type = rx_buf[1];
	p->ref = rx_buf[2];
	p->seq = rx_buf[3];

	if (len > max_len) {
		printc_err("hal_proto_recv: reply too long\n");
		return -1;
	}

	memcpy(buf, rx_buf + 4, len);
	return len;
}

int hal_proto_execute(struct hal_proto *p, uint8_t fid,
		      const uint8_t *data, int len)
{
	uint8_t fdata[HAL_MAX_PAYLOAD];

	if (len + 2 > HAL_MAX_PAYLOAD) {
		printc_err("hal_proto_execute: payload too big: %d\n", len);
		return -1;
	}

	fdata[0] = fid;
	fdata[1] = 0;
	memcpy(fdata + 2, data, len);

	if (hal_proto_send(p, HAL_PROTO_TYPE_CMD_EXECUTE, fdata, len + 2) < 0)
		goto fail;

	p->length = 0;

	do {
		int r = hal_proto_receive(p, p->payload + p->length,
					  sizeof(p->payload) - p->length);

		if (r < 0)
			goto fail;

		if ((p->type == HAL_PROTO_TYPE_EXCEPTION) && (r >= 2)) {
			printc_err("hal_proto_execute: HAL exception: 0x%04x\n",
				   LE_WORD(p->payload, p->length));
			goto fail;
		}

		if (p->type == HAL_PROTO_TYPE_ACKNOWLEDGE)
			break;

		if (p->type != HAL_PROTO_TYPE_DATA) {
			printc_err("hal_proto_execute: no data "
				   "(got type 0x%02x)\n", p->type);
			goto fail;
		}

		if (hal_proto_send(p, HAL_PROTO_TYPE_ACKNOWLEDGE, NULL, 0) < 0)
			goto fail;

		p->length += r;
	} while (p->ref & 0x80);

	return 0;

fail:
	printc_err("hal_proto_execute: fid: 0x%02x\n", fid);
	return -1;
}
