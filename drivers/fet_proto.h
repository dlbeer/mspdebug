/* MSPDebug - debugging tool for the eZ430
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

#ifndef FET_PROTO_H_
#define FET_PROTO_H_

#include <stdint.h>

#include "transport.h"

/* Send data in separate packets, as in the RF2500 */
#define FET_PROTO_SEPARATE_DATA		0x01

/* Received packets have an extra trailing byte */
#define FET_PROTO_EXTRA_RECV		0x02

/* Command packets have no leading \x7e */
#define FET_PROTO_NOLEAD_SEND		0x04

/* Data transfer limits */
#define FET_PROTO_MAX_PARAMS		16
#define FET_PROTO_MAX_BLOCK		4096

/* Protocol parser structure */
struct fet_proto {
	transport_t			transport;
	int				proto_flags;

	/* Raw packet buffer */
	uint8_t                         fet_buf[65538];
	int                             fet_len;

	/* Received packet is parsed into these fields */
	int				command_code;
	int				state;
	int				error;

	int				argc;
	uint32_t			argv[FET_PROTO_MAX_PARAMS];

	uint8_t				*data;
	int				datalen;
};

/* Initialize a FET protocol parser */
void fet_proto_init(struct fet_proto *p, transport_t trans, int proto_flags);

/* Perform a command-response transfer */
int fet_proto_xfer(struct fet_proto *p,
		   int command_code, const uint8_t *data, int datalen,
		   int nparams, ...);

#endif
