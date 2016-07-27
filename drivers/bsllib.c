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

#include "util/util.h"
#include "bsllib.h"
#include "gpio.h"

int bsllib_seq_do(sport_t fd, const char *seq)
{
	int state = 0;

	while (*seq && *seq != ':') {
		const char c = *(seq++);

		switch (c) {
		case 'R':
			state |= SPORT_MC_RTS;
			break;

		case 'r':
			state &= ~SPORT_MC_RTS;
			break;

		case 'D':
			state |= SPORT_MC_DTR;
			break;

		case 'd':
			state &= ~SPORT_MC_DTR;
			break;

		case ',':
			if (sport_set_modem(fd, state) < 0)
				return -1;
			delay_ms(50);
			break;
		}
	}

	if (sport_set_modem(fd, state) < 0)
		return -1;
	delay_ms(50);

	return 0;
}

int bsllib_seq_do_gpio(int rts, int dtr, const char *seq)
{
	int was_rts_exported = gpio_is_exported(rts);
	int was_dtr_exported = gpio_is_exported(dtr);

	gpio_export ( rts );
	gpio_set_dir ( rts, 1 );
	gpio_export ( dtr );
	gpio_set_dir ( dtr, 1 );

	while (*seq && *seq != ':') {
		const char c = *(seq++);

		// Logic is reversed!
		switch (c) {
			case 'R':
				gpio_set_value ( rts, 0 );
				break;

			case 'r':
				gpio_set_value ( rts, 1 );
				break;

			case 'D':
				gpio_set_value ( dtr, 0 );
				break;

			case 'd':
				gpio_set_value ( dtr, 1 );
				break;

			case ',':
				delay_ms(50);
				break;
		}
	}

	if (was_rts_exported == 0)
	{
		gpio_unexport ( rts );
	}
	if (was_dtr_exported == 0)
	{
		gpio_unexport ( dtr );
	}

	delay_ms(50);

	return 0;
}

const char *bsllib_seq_next(const char *seq)
{
	while (*seq && *seq != ':')
		seq++;

	if (*seq == ':')
		seq++;

	return seq;
}
