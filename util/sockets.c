/* MSPDebug - debugging tool for MSP430 MCUs
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

#ifndef __Windows__
#include <sys/select.h>
#endif

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "sockets.h"
#include "util.h"
#include "ctrlc.h"

#ifdef __Windows__
#include <windows.h>
#endif

#ifdef __Windows__
static DWORD error_save = 0;

static void sockets_begin(SOCKET s, DWORD event)
{
	u_long mode = 1;

	ioctlsocket(s, FIONBIO, &mode);
	WSAEventSelect(s, ctrlc_win32_event(), event);

	/* We explicitly check for Ctrl+C after resetting the event in
	 * sockets_wait().
	 */
	ResetEvent(ctrlc_win32_event());
}

static int sockets_wait(DWORD timeout)
{
	DWORD r;

	error_save = WSAGetLastError();
	if (ctrlc_check())
		error_save = ERROR_OPERATION_ABORTED;
	if (error_save != WSAEWOULDBLOCK)
		return -1;

	r = WaitForSingleObject(ctrlc_win32_event(), timeout);

	if (r == WAIT_TIMEOUT) {
		error_save = WAIT_TIMEOUT;
		return -1;
	}

	return 0;
}

static void sockets_end(SOCKET s)
{
	u_long mode = 0;

	ioctlsocket(s, FIONBIO, &mode);
	WSAEventSelect(s, ctrlc_win32_event(), 0);
	WSASetLastError(error_save);
}

SOCKET sockets_accept(SOCKET s, struct sockaddr *addr, socklen_t *addrlen)
{
	SOCKET client = INVALID_SOCKET;

	sockets_begin(s, FD_ACCEPT);

	do {
		client = WSAAccept(s, addr, addrlen, NULL, 0);
	} while (SOCKET_ISERR(client) && !sockets_wait(INFINITE));

	sockets_end(s);
	return client;
}

int sockets_connect(SOCKET s, const struct sockaddr *addr, socklen_t addrlen)
{
	int ret = -1;

	sockets_begin(s, FD_CONNECT);

	connect(s, addr, addrlen);
	do {
		WSANETWORKEVENTS evts;

		WSAEnumNetworkEvents(s, NULL, &evts);
		if (evts.lNetworkEvents & FD_CONNECT) {
			error_save = evts.iErrorCode[FD_CONNECT_BIT];
			if (!error_save)
				ret = 0;
			break;
		}
	} while (!sockets_wait(INFINITE));

	sockets_end(s);
	return ret;
}

ssize_t sockets_send(SOCKET s, const void *buf, size_t len, int flags)
{
	int ret = -1;

	sockets_begin(s, FD_WRITE | FD_CLOSE);

	do {
		ret = send(s, buf, len, flags);
	} while (ret < 0 && !sockets_wait(INFINITE));

	sockets_end(s);
	return ret;
}

ssize_t sockets_recv(SOCKET s, void *buf, size_t len, int flags,
		     int timeout_ms, int *was_timeout)
{
	int ret = -1;
	DWORD to_arg = (timeout_ms >= 0) ? timeout_ms : INFINITE;

	sockets_begin(s, FD_READ | FD_CLOSE);

	do {
		ret = recv(s, buf, len, flags);
	} while (ret < 0 && !sockets_wait(to_arg));

	if (was_timeout)
		*was_timeout = (ret < 0 && (error_save == WAIT_TIMEOUT));

	sockets_end(s);
	return ret;
}
#else /* __Windows__ */
SOCKET sockets_accept(SOCKET s, struct sockaddr *addr, socklen_t *addrlen)
{
	return accept(s, addr, addrlen);
}

int sockets_connect(SOCKET s, const struct sockaddr *addr, socklen_t addrlen)
{
	return connect(s, addr, addrlen);
}

ssize_t sockets_send(SOCKET s, const void *buf, size_t len, int flags)
{
	return send(s, buf, len, flags);
}

ssize_t sockets_recv(SOCKET s, void *buf, size_t buf_len, int flags,
		     int timeout_ms, int *was_timeout)
{
	fd_set r;
	struct timeval to = {
		.tv_sec = timeout_ms / 1000,
		.tv_usec = timeout_ms % 1000
	};

	FD_ZERO(&r);
	FD_SET(s, &r);

	if (select(s + 1, &r, NULL, NULL,
		   timeout_ms < 0 ? NULL : &to) < 0)
		return -1;

	if (was_timeout)
		*was_timeout = !FD_ISSET(s, &r);

	if (!FD_ISSET(s, &r)) {
		errno = ETIMEDOUT;
		return 0;
	}

	return recv(s, buf, buf_len, flags);
}
#endif
