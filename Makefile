# MSPDebug - debugging tool for the eZ430
# Copyright (C) 2009, 2010 Daniel Beer
# Copyright (C) 2010 Andrew Armenia
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

CC = gcc
INSTALL = /usr/bin/install
PREFIX ?= /usr/local
LDFLAGS ?= -s

ifdef WITHOUT_READLINE
	READLINE_CFLAGS =
	READLINE_LIBS =
else
	READLINE_CFLAGS = -DUSE_READLINE
	READLINE_LIBS = -lreadline
endif

UNAME := $(shell sh -c 'uname -s')
ifeq ($(UNAME),Darwin) # Mac OS X/MacPorts stuff
	PORTS_CFLAGS = -I/opt/local/include
	PORTS_LDFLAGS = -L/opt/local/lib
else
  ifeq ($(UNAME),OpenBSD) # OpenBSD Ports stuff
	PORTS_CFLAGS = `pkg-config --cflags libelf libusb`
	PORTS_LDFLAGS = `pkg-config --libs libelf libusb` -ltermcap
  else
	PORTS_CFLAGS =
	PORTS_LDFLAGS =
  endif
endif

MSPDEBUG_CFLAGS = -O1 -Wall -Wno-char-subscripts -ggdb

all: mspdebug

clean:
	/bin/rm -f *.o
	/bin/rm -f mspdebug

install: mspdebug mspdebug.man
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	mkdir -p $(DESTDIR)$(PREFIX)/share/man/man1
	$(INSTALL) -m 0755 mspdebug $(DESTDIR)$(PREFIX)/bin/mspdebug
	$(INSTALL) -m 0644 mspdebug.man $(DESTDIR)$(PREFIX)/share/man/man1/mspdebug.1

.SUFFIXES: .c .o

mspdebug: main.o fet.o rf2500.o dis.o uif.o olimex.o ihex.o elf32.o stab.o \
          util.o bsl.o sim.o symmap.o gdb.o btree.o rtools.o sym.o devcmd.o \
	  reader.o vector.o output_util.o expr.o fet_error.o binfile.o \
	  fet_db.o usbutil.o titext.o srec.o device.o coff.o opdb.o output.o \
	  cmddb.o stdcmd.o prog.o flash_bsl.o list.o simio.o simio_tracer.o \
	  simio_timer.o simio_wdt.o simio_hwmult.o simio_gpio.o aliasdb.o \
	  gdb_proto.o gdbc.o
	$(CC) $(LDFLAGS) $(PORTS_LDFLAGS) -o $@ $^ -lusb $(READLINE_LIBS)

.c.o:
	$(CC) $(CFLAGS) $(PORTS_CFLAGS) $(READLINE_CFLAGS) $(MSPDEBUG_CFLAGS) -o $@ -c $*.c
