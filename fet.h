/* MSPDebug - debugging tool for the eZ430
 * Copyright (C) 2009 Daniel Beer
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

#ifndef FET_H_
#define FET_H_

#include <sys/types.h>

/* This structure is used to provide an interface to a lower-level
 * transport. The transport mechanism is viewed as a stream by the FET
 * controller, which handles packet encapsulation, checksums and other
 * high-level functions.
 */
struct fet_transport {
	int (*send)(const char *data, int len);
	int (*recv)(char *data, int max_len);
	void (*close)(void);
};

/* Start up the FET controller by specifying a transport, a voltage in
 * millivolts and a set of protocol mode flags.
 */
#define FET_PROTO_SPYBIWIRE	0x01
#define FET_PROTO_RF2500	0x02

int fet_open(const struct fet_transport *transport,
	     int proto_flags, int vcc_mv);

/* Shut down the connection to the FET. This also closes the underlying
 * transport.
 */
int fet_close(void);

/* Issue a reset to the CPU. The CPU can be reset via any combination
 * of three methods, and you can choose whether or not to leave the CPU
 * halted after reset.
 */
#define FET_RESET_PUC   0x01
#define FET_RESET_RST   0x02
#define FET_RESET_VCC   0x04
#define FET_RESET_ALL   0x07
#define FET_RESET_HALT	0x10

int fet_reset(int flags);

/* Retrieve and store register values. There are 16 16-bit registers in
 * the MSP430 CPU. regs must always be a pointer to an array of 16
 * u_int16_t.
 */
#define FET_NUM_REGS	16

int fet_get_context(u_int16_t *regs);
int fet_set_context(u_int16_t *regs);

/* Erase the CPU's internal flash. */
#define FET_ERASE_SEGMENT	0
#define FET_ERASE_MAIN		1
#define FET_ERASE_ALL		2

int fet_erase(int type, u_int16_t addr, int len);

/* Read and write memory. fet_write_mem can be used to reflash the
 * device, but only after an erase.
 */
int fet_read_mem(u_int16_t addr, char *buffer, int count);
int fet_write_mem(u_int16_t addr, char *buffer, int count);

/* Fetch the device status. If the device is currently running, then
 * the FET_POLL_RUNNING flag will be set. FET_POLL_BREAKPOINT is set
 * when the device hits the preset breakpoint, and then resets on the
 * next call to fet_poll().
 */
#define FET_POLL_RUNNING        0x01
#define FET_POLL_BREAKPOINT     0x02

int fet_poll(void);

/* CPU run/step/stop control. While the CPU is running, memory and
 * registers are inaccessible (only fet_poll() or fet_stop()) will
 * work. fet_step() is used to single-step the CPU.
 */
#define FET_RUN_FREE		1
#define FET_RUN_STEP		2
#define FET_RUN_BREAKPOINT	3

int fet_run(int type);
int fet_stop(void);

/* Set a breakpoint address */
int fet_break(int which, u_int16_t addr);

#endif
