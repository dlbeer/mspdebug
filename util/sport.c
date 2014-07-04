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

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>

#include "sport.h"
#include "util.h"
#include "output.h"
#include "ctrlc.h"

#ifdef __linux__
#include <linux/serial.h>
#endif

#define TIMEOUT_S	30

#ifndef __Windows__

#include <sys/select.h>

#ifndef B460800
#define B460800 460800
#endif

#ifndef B500000
#define B500000 500000
#endif

struct baud_rate {
	int             rate;
	int             code;
};

static const struct baud_rate baud_rates[] = {
	{9600,          B9600},
	{115200,        B115200},
	{460800,        B460800},
	{500000,        B500000}
};

static int rate_to_code(int rate)
{
	int i;

	for (i = 0; i < ARRAY_LEN(baud_rates); i++)
		if (baud_rates[i].rate == rate)
			return baud_rates[i].code;

	return -1;
}

#if defined(__linux__)
static int set_nonstandard_rate(int fd, struct termios *attr, int rate)
{
	struct serial_struct ss;

	/* We need to set the rate code to B38400 on Linux for
	 * the non-standard rate to take effect.
	 */
	cfsetispeed(attr, B38400);
	cfsetospeed(attr, B38400);

	if (ioctl(fd, TIOCGSERIAL, &ss) < 0) {
		pr_error("sport: TIOCGSERIAL failed");
		return -1;
	}

	ss.custom_divisor = ss.baud_base / rate;
	ss.flags = ASYNC_SPD_CUST;

	if (ioctl(fd, TIOCSSERIAL, &ss) < 0) {
		pr_error("sport: TIOCSSERIAL failed");
		return -1;
	}

	return 0;
}
#elif defined(__OpenBSD__) || defined(__FreeBSD__) || \
      defined(__NetBSD__) || defined(__DragonFly__)
static int set_nonstandard_rate(int fd, struct termios *attr, int rate)
{
	cfsetispeed(attr, rate);
	cfsetospeed(attr, rate);

	return 0;
}
#else
static int set_nonstandard_rate(int fd, struct termios *attr, int rate)
{
	printc_err("sport: Can't set non-standard baud rate %d on "
		   "this platform\n", rate);
	return -1;
}
#endif

sport_t sport_open(const char *device, int rate, int flags)
{
	int fd = open(device, O_RDWR | O_NOCTTY);
	struct termios attr;
	int rate_code = rate_to_code(rate);

	if (fd < 0)
		return -1;

	tcgetattr(fd, &attr);

#ifdef __sun__
	attr.c_iflag &= ~(IMAXBEL | IGNBRK | BRKINT | PARMRK | ISTRIP |
			  INLCR | IGNCR | ICRNL | IXON);
	attr.c_oflag &= ~OPOST;
	attr.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	attr.c_cflag &= ~(CSIZE | PARENB);
	attr.c_cflag |= CS8;
#else
	cfmakeraw(&attr);
#endif

	if (rate_code >= 0) {
		cfsetispeed(&attr, rate_code);
		cfsetospeed(&attr, rate_code);
	} else if (set_nonstandard_rate(fd, &attr, rate) < 0) {
		close(fd);
		return -1;
	}

	if (flags & SPORT_EVEN_PARITY)
		attr.c_cflag |= PARENB;

	if (tcsetattr(fd, TCSAFLUSH, &attr) < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

void sport_close(sport_t s)
{
	close(s);
}

int sport_flush(sport_t s)
{
	return tcflush(s, TCIFLUSH);
}

int sport_set_modem(sport_t s, int bits)
{
	return ioctl(s, TIOCMSET, &bits);
}

int sport_read(sport_t s, uint8_t *data, int len)
{
	int r;

	do {
		struct timeval tv = {
			.tv_sec = TIMEOUT_S,
			.tv_usec = 0
		};

		fd_set set;

		FD_ZERO(&set);
		FD_SET(s, &set);

		r = select(s + 1, &set, NULL, NULL, &tv);
		if (r > 0)
			r = read(s, data, len);

		if (!r)
			errno = ETIMEDOUT;
		if (r <= 0)
			return -1;
	} while (r <= 0);

	return r;
}

int sport_write(sport_t s, const uint8_t *data, int len)
{
	return write(s, data, len);
}

#else /* __Windows__ */

sport_t sport_open(const char *device, int rate, int flags)
{
	HANDLE hs = CreateFile(device, GENERIC_READ | GENERIC_WRITE,
			       0, 0, OPEN_EXISTING,
			       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
			       0);
	DCB params = {0};
	COMMTIMEOUTS timeouts = {0};

	if (hs == INVALID_HANDLE_VALUE)
		return INVALID_HANDLE_VALUE;

	if (!GetCommState(hs, &params)) {
		CloseHandle(hs);
		return INVALID_HANDLE_VALUE;
	}

	params.BaudRate = rate;
	params.ByteSize = 8;
	params.StopBits = ONESTOPBIT;
	params.Parity = (flags & SPORT_EVEN_PARITY) ? EVENPARITY : NOPARITY;

	if (!SetCommState(hs, &params)) {
		CloseHandle(hs);
		return INVALID_HANDLE_VALUE;
	}

	timeouts.ReadIntervalTimeout = 50;
	if (!SetCommTimeouts(hs, &timeouts)) {
		CloseHandle(hs);
		return INVALID_HANDLE_VALUE;
	}

	return hs;
}

void sport_close(sport_t s)
{
	CloseHandle(s);
}

int sport_flush(sport_t s)
{
	if (!PurgeComm(s, PURGE_RXABORT | PURGE_RXCLEAR))
		return -1;

	return 0;
}

int sport_set_modem(sport_t s, int bits)
{
	if (!EscapeCommFunction(s, (bits & SPORT_MC_DTR) ? SETDTR : CLRDTR))
		return -1;

	if (!EscapeCommFunction(s, (bits & SPORT_MC_RTS) ? SETRTS : CLRRTS))
		return -1;

	return 0;
}

static int xfer_wait(sport_t s, LPOVERLAPPED ovl)
{
	DWORD result = 0;

	ResetEvent(ctrlc_win32_event());

	while (!GetOverlappedResult(s, ovl, &result, FALSE)) {
		DWORD r;

		if (GetLastError() != ERROR_IO_INCOMPLETE)
			return -1;

		if (ctrlc_check()) {
			CancelIo(s);
			SetLastError(ERROR_OPERATION_ABORTED);
			return -1;
		}

		r = WaitForSingleObject(ctrlc_win32_event(), TIMEOUT_S * 1000);
		if (r == WAIT_TIMEOUT) {
			CancelIo(s);
			SetLastError(WAIT_TIMEOUT);
			return -1;
		}
	}

	return result;
}

int sport_read(sport_t s, uint8_t *data, int len)
{
	OVERLAPPED ovl = {0};

	ovl.hEvent = ctrlc_win32_event();

	ReadFile(s, (void *)data, len, NULL, &ovl);
	return xfer_wait(s, &ovl);
}

int sport_write(sport_t s, const uint8_t *data, int len)
{
	OVERLAPPED ovl = {0};

	ovl.hEvent = ctrlc_win32_event();

	WriteFile(s, (void *)data, len, NULL, &ovl);
	return xfer_wait(s, &ovl);
}

#endif

int sport_read_all(sport_t s, uint8_t *data, int len)
{
	while (len) {
		int r = sport_read(s, data, len);

		if (r <= 0)
			return -1;

		data += r;
		len -= r;
	}

	return 0;
}

int sport_write_all(sport_t s, const uint8_t *data, int len)
{
	while (len) {
		int r = sport_write(s, data, len);

		if (r <= 0)
			return -1;

		data += r;
		len -= r;
	}

	return 0;
}
