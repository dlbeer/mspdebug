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

#include <unistd.h>
#include "ctrlc.h"

#ifdef __Windows__
#include <windows.h>

static int ctrlc_flag;
static HANDLE ctrlc_event;
static CRITICAL_SECTION ctrlc_cs;

static WINAPI BOOL ctrlc_handler(DWORD event)
{
	if ((event == CTRL_C_EVENT) || (event == CTRL_BREAK_EVENT)) {
		ctrlc_raise();
		return TRUE;
	}

	return FALSE;
}

void ctrlc_init(void)
{
	ctrlc_event = CreateEvent(0, TRUE, FALSE, NULL);
	InitializeCriticalSection(&ctrlc_cs);
	SetConsoleCtrlHandler(ctrlc_handler, TRUE);
}

void ctrlc_exit(void)
{
	SetConsoleCtrlHandler(NULL, TRUE);
	DeleteCriticalSection(&ctrlc_cs);
	CloseHandle(ctrlc_event);
}

int ctrlc_check(void)
{
	int cc;

	EnterCriticalSection(&ctrlc_cs);
	cc = ctrlc_flag;
	LeaveCriticalSection(&ctrlc_cs);

	return cc;
}

void ctrlc_clear(void)
{
	EnterCriticalSection(&ctrlc_cs);
	ctrlc_flag = 0;
	ResetEvent(ctrlc_event);
	LeaveCriticalSection(&ctrlc_cs);
}

void ctrlc_raise(void)
{
	EnterCriticalSection(&ctrlc_cs);
	ctrlc_flag = 1;
	SetEvent(ctrlc_event);
	LeaveCriticalSection(&ctrlc_cs);
}

HANDLE ctrlc_win32_event(void)
{
	return ctrlc_event;
}
#else /* __Windows__ */
#include <pthread.h>
#include <signal.h>

static volatile sig_atomic_t ctrlc_flag;
static pthread_t ctrlc_thread;

static void sigint_handler(int signum)
{
	(void)signum;

	ctrlc_flag = 1;
}

void ctrlc_init(void)
{
#ifndef __CYGWIN__
	static const struct sigaction siga = {
	       .sa_handler = sigint_handler,
	       .sa_flags = 0
	};
#endif
	ctrlc_thread = pthread_self();

#ifdef __CYGWIN__
	signal(SIGINT, sigint_handler);
#else
	sigaction(SIGINT, &siga, NULL);
#endif
}

void ctrlc_exit(void)
{
	signal(SIGINT, SIG_DFL);
}

void ctrlc_clear(void)
{
	ctrlc_flag = 0;
}

void ctrlc_raise(void)
{
	pthread_kill(ctrlc_thread, SIGINT);
}

int ctrlc_check(void)
{
#ifdef __CYGWIN__
	/* Cygwin's signal emulation seems to require the process to
	 * block.
	 */
	usleep(1);
#endif
	return ctrlc_flag;
}
#endif
