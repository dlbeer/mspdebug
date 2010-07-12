# MSPDebug - debugging tool for the eZ430
# Copyright (C) 2009, 2010 Daniel Beer
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

ifdef WITHOUT_READLINE
	READLINE_CFLAGS =
	READLINE_LIBS =
else
	READLINE_CFLAGS = -DUSE_READLINE
	READLINE_LIBS = -lreadline
endif

# Mac OS X/MacPorts stuff
UNAME :=$(shell sh -c 'uname -s')
ifeq ($(UNAME),Darwin)
	MACPORTS_CFLAGS = -I/opt/local/include
	MACPORTS_LDFLAGS = -L/opt/local/lib
else
	MACPORTS_CFLAGS =
	MACPORTS_LDFLAGS =
endif

MSPDEBUG_CFLAGS = -O1 -Wall -Wno-char-subscripts -ggdb

all: mspdebug

clean:
	/bin/rm -f *.o
	/bin/rm -f mspdebug

install: mspdebug mspdebug.man
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	mkdir -p $(DESTDIR)$(PREFIX)/share/man/man1
	$(INSTALL) -m 0755 -s mspdebug $(DESTDIR)$(PREFIX)/bin/mspdebug
	$(INSTALL) -m 0644 mspdebug.man $(DESTDIR)$(PREFIX)/share/man/man1/mspdebug.1

.SUFFIXES: .c .o

mspdebug: main.o fet.o rf2500.o dis.o uif.o olimex.o ihex.o elf32.o stab.o \
          util.o bsl.o sim.o symmap.o gdb.o btree.o rtools.o sym.o devcmd.o \
	  cproc.o vector.o cproc_util.o expr.o fet_error.o binfile.o fet_db.o \
	  usbutil.o titext.o srec.o
	$(CC) $(LDFLAGS) $(MACPORTS_LDFLAGS) -o $@ $^ -lusb $(READLINE_LIBS)

.c.o:
	$(CC) $(CFLAGS) $(MACPORTS_CFLAGS) $(READLINE_CFLAGS) $(MSPDEBUG_CFLAGS) -o $@ -c $*.c
