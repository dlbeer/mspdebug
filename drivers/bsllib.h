/* MSPDebug - debugging tool for the eZ430
 * Copyright (C) 2009, 2010 Daniel Beer
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

#ifndef BSLLIB_H_
#define BSLLIB_H_

#include "util/sport.h"

/* Execute the given sequence specifier with the modem control lines */
int bsllib_seq_do(sport_t sport, const char *seq);
int bsllib_seq_do_gpio(int rts, int dtr, const char *seq);

/* Skip to the next part of a sequence specified */
const char *bsllib_seq_next(const char *seq);

#endif
