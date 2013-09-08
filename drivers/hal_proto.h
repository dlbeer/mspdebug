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

#ifndef HAL_PROTO_H_
#define HAL_PROTO_H_

#include <stdint.h>
#include <stddef.h>
#include "transport.h"

/* Low-level HAL message types */
typedef enum {
	HAL_PROTO_TYPE_UP_INIT			= 0x51,
	HAL_PROTO_TYPE_UP_ERASE			= 0x52,
	HAL_PROTO_TYPE_UP_WRITE			= 0x53,
	HAL_PROTO_TYPE_UP_READ			= 0x54,
	HAL_PROTO_TYPE_UP_CORE			= 0x55,
	HAL_PROTO_TYPE_DCDC_CALIBRATE		= 0x56,
	HAL_PROTO_TYPE_DCDC_INIT_INTERFACE	= 0x57,
	HAL_PROTO_TYPE_DCDC_SUB_MCU_VERSION	= 0x58,
	HAL_PROTO_TYPE_DCDC_LAYER_VERSION	= 0x59,
	HAL_PROTO_TYPE_DCDC_POWER_DOWN		= 0x60,
	HAL_PROTO_TYPE_DCDC_SET_VCC		= 0x61,
	HAL_PROTO_TYPE_DCDC_RESTART		= 0x62,
	HAL_PROTO_TYPE_CMD_LEGACY		= 0x7e,
	HAL_PROTO_TYPE_CMD_SYNC			= 0x80,
	HAL_PROTO_TYPE_CMD_EXECUTE		= 0x81,
	HAL_PROTO_TYPE_CMD_EXECUTE_LOOP		= 0x82,
	HAL_PROTO_TYPE_CMD_LOAD			= 0x83,
	HAL_PROTO_TYPE_CMD_LOAD_CONTINUED	= 0x84,
	HAL_PROTO_TYPE_CMD_DATA			= 0x85,
	HAL_PROTO_TYPE_CMD_KILL			= 0x86,
	HAL_PROTO_TYPE_CMD_MOVE			= 0x87,
	HAL_PROTO_TYPE_CMD_UNLOAD		= 0x88,
	HAL_PROTO_TYPE_CMD_BYPASS		= 0x89,
	HAL_PROTO_TYPE_CMD_EXECUTE_LOOP_CONT	= 0x8a,
	HAL_PROTO_TYPE_CMD_COM_RESET		= 0x8b,
	HAL_PROTO_TYPE_CMD_PAUSE_LOOP		= 0x8c,
	HAL_PROTO_TYPE_CMD_RESUME_LOOP		= 0x8d,
	HAL_PROTO_TYPE_ACKNOWLEDGE		= 0x91,
	HAL_PROTO_TYPE_EXCEPTION		= 0x92,
	HAL_PROTO_TYPE_DATA			= 0x93,
	HAL_PROTO_TYPE_DATA_REQUEST		= 0x94,
	HAL_PROTO_TYPE_STATUS			= 0x95
} hal_proto_type_t;

typedef enum {
	HAL_PROTO_CHECKSUM	= 0x01
} hal_proto_flags_t;

#define HAL_MAX_PAYLOAD		253

struct hal_proto {
	transport_t		trans;
	hal_proto_flags_t	flags;
	uint8_t			ref_id;

	/* Receive parameters */
	hal_proto_type_t	type;
	uint8_t			ref;
	uint8_t			seq;

	/* Execute data */
	int			length;
	uint8_t			payload[4096];
};

/* Initialize a HAL protocol interpreter */
void hal_proto_init(struct hal_proto *p, transport_t trans,
		    hal_proto_flags_t flags);

/* Send a low-level HAL command */
int hal_proto_send(struct hal_proto *p, hal_proto_type_t type,
		   const uint8_t *data, int length);

/* Receive a low-level HAL response */
int hal_proto_receive(struct hal_proto *p, uint8_t *buf, int max_len);

/* Execute a high-level function. The reply data is kept in the payload
 * buffer.
 */
int hal_proto_execute(struct hal_proto *p, uint8_t fid,
		      const uint8_t *data, int len);

#endif
