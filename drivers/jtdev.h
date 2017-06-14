/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2012 Daniel Beer
 * Copyright (C) 2012 Peter Bägel
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

#ifndef JTDEV_H_
#define JTDEV_H_

#include <stdint.h>

struct jtdev_func;
struct jtdev {
	int		port;
	uint8_t		data_register;
	uint8_t		control_register;
	int		failed;
	const struct jtdev_func * f;
};

struct jtdev_func{
/* Initialize/destroy a parallel-port JTAG interface. jtdev_open()
 * returns 0 on success or -1 if an error occurs.
 *
 * All other JTAG IO functions indicate errors by setting the failed
 * field in the jtdev structure.
 */
	int (*jtdev_open)(struct jtdev *p, const char *device);
	void (*jtdev_close)(struct jtdev *p);

/* Connect/release JTAG */
	void (*jtdev_power_on)(struct jtdev *p);
	void (*jtdev_power_off)(struct jtdev *p);
	void (*jtdev_connect)(struct jtdev *p);
	void (*jtdev_release)(struct jtdev *p);

/* Low-level IO */
	void (*jtdev_tck)(struct jtdev *p, int out);
	void (*jtdev_tms)(struct jtdev *p, int out);
	void (*jtdev_tdi)(struct jtdev *p, int out);
	void (*jtdev_rst)(struct jtdev *p, int out);
	void (*jtdev_tst)(struct jtdev *p, int out);
	int (*jtdev_tdo_get)(struct jtdev *p);

/* TCLK management */
	void (*jtdev_tclk)(struct jtdev *p, int out);
	int (*jtdev_tclk_get)(struct jtdev *p);
	void (*jtdev_tclk_strobe)(struct jtdev *p, unsigned int count);

/* LED indicators */
	void (*jtdev_led_green)(struct jtdev *p, int out);
	void (*jtdev_led_red)(struct jtdev *p, int out);

};

extern const struct jtdev_func jtdev_func_pif;
extern const struct jtdev_func jtdev_func_gpio;
extern const struct jtdev_func jtdev_func_bp;

#endif
