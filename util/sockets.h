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

#ifndef SOCKETS_H_
#define SOCKETS_H_

#ifdef __Windows__
#include <winsock2.h>
#include <stdio.h>

typedef int socklen_t;

#define SOCKET_ISERR(x) ((x) == INVALID_SOCKET)
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>

#define closesocket close

typedef int SOCKET;

#define SOCKET_ISERR(x) ((x) < 0)
#endif

/* These are versions of the blocking IO calls which can be interrupted
 * by the user pressing Ctrl+C.
 */
SOCKET sockets_accept(SOCKET s, struct sockaddr *addr, socklen_t *addrlen);
int sockets_connect(SOCKET s, const struct sockaddr *addr, socklen_t addrlen);
ssize_t sockets_send(SOCKET s, const void *buf, size_t len, int flags);
ssize_t sockets_recv(SOCKET s, void *buf, size_t len, int flags,
		     int timeout_ms, int *was_timeout);

#endif
