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

#ifndef CTRLC_H_
#define CTRLC_H_

#ifdef __Windows__
#include <windows.h>
#endif

/* The Ctrl+C subsystem provides a mechanism for interrupting IO
 * operations in a controlled way. Relevant signals are captured (SIGINT
 * on Linux and console events on Windows) and the event is reported to
 * the system.
 *
 * The Ctrl+C state has the semantics of an event variable: it can be
 * either set, reset or checked via the interface provided.
 */

/* Set up Ctrl+C handling and register all necessary handlers. */
void ctrlc_init(void);
void ctrlc_exit(void);

/* Check the state of the Ctrl+C event variable. This function returns
 * non-zero if the event is raised.
 */
int ctrlc_check(void);

/* Manually reset the Ctrl+C event. This should be done before starting
 * the processing of a command.
 */
void ctrlc_clear(void);

/* Manually raise a Ctrl+C event. This function is safe to call from any
 * thread.
 */
void ctrlc_raise(void);

#ifdef __Windows__
/* On Unix systems, Ctrl+C generates a signal which will also interrupt
 * any IO operation currently in progress, after which the event will be
 * checked by the initiator of the operation.
 *
 * Under Windows, we don't have this facility, so we expose a kernel
 * object which becomes signalled when the Ctrl+C event is raised.
 * Implementations of Windows IO operations should allow operations to
 * be interrupted by the signalling of this object.
 *
 * The event can be manually cleared before IO operations, but this
 * doesn't clear the recorded Ctrl+C event. If the event is manually
 * cleared, the Ctrl+C event status should be checked *after* doing so.
 */
HANDLE ctrlc_win32_event(void);
#endif

#endif
