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

#ifndef BYTES_H_
#define BYTES_H_

#include <stdint.h>

/* Read/write big-endian 16-bit */
static inline uint16_t r16be(const uint8_t *buf)
{
	return (((uint16_t)buf[0]) << 8) | ((uint16_t)buf[1]);
}

static inline void w16be(uint8_t *buf, uint16_t v)
{
	buf[0] = v >> 8;
	buf[1] = v;
}

/* Read/write little-endian 16-bit */
static inline uint16_t r16le(const uint8_t *buf)
{
	return (((uint16_t)buf[1]) << 8) | ((uint16_t)buf[0]);
}

static inline void w16le(uint8_t *buf, uint16_t v)
{
	buf[1] = v >> 8;
	buf[0] = v;
}

/* Read/write big-endian 32-bit */
static inline uint32_t r32be(const uint8_t *buf)
{
	return (((uint32_t)buf[0]) << 24) |
	       (((uint32_t)buf[1]) << 16) |
	       (((uint32_t)buf[2]) << 8) |
	       ((uint32_t)buf[3]);
}

static inline void w32be(uint8_t *buf, uint32_t v)
{
	buf[0] = v >> 24;
	buf[1] = v >> 16;
	buf[2] = v >> 8;
	buf[3] = v;
}

/* Read/write little-endian 32-bit */
static inline uint32_t r32le(const uint8_t *buf)
{
	return (((uint32_t)buf[3]) << 24) |
	       (((uint32_t)buf[2]) << 16) |
	       (((uint32_t)buf[1]) << 8) |
	       ((uint32_t)buf[0]);
}

static inline void w32le(uint8_t *buf, uint32_t v)
{
	buf[3] = v >> 24;
	buf[2] = v >> 16;
	buf[1] = v >> 8;
	buf[0] = v;
}

#endif
