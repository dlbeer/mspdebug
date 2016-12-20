/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2011 Daniel Beer
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

#ifndef SPORT_H_
#define SPORT_H_

#include <stdint.h>
#ifndef __Windows__


#include <termios.h>
#include <sys/ioctl.h>

typedef int sport_t;

#define SPORT_ISERR(x) ((x) < 0)

#define SPORT_MC_DTR		TIOCM_DTR
#define SPORT_MC_RTS		TIOCM_RTS

#else /* __Windows__ */

#include <windows.h>

typedef HANDLE sport_t;

#define SPORT_ISERR(x) ((x) == INVALID_HANDLE_VALUE)

#define SPORT_MC_DTR		0x01
#define SPORT_MC_RTS		0x02

#endif

/* Various utility functions for IO */

#define SPORT_EVEN_PARITY	0x01

sport_t sport_open(const char *device, int rate, int flags);
void sport_close(sport_t s);

int sport_flush(sport_t s);
int sport_set_modem(sport_t s, int bits);

/* Read/write a serial port. These functions return the number of
 * bytes transferred, or -1 on error.
 */
int sport_read(sport_t s, uint8_t *data, int len);
int sport_write(sport_t s, const uint8_t *data, int len);

/* Same as above, but requires that all data be transferred. */
int sport_read_all(sport_t s, uint8_t *data, int len);
int sport_write_all(sport_t s, const uint8_t *data, int len);

#endif
