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

all: mspdebug

clean:
	/bin/rm -f *.o
	/bin/rm -f mspdebug

install: mspdebug mspdebug.man
	$(INSTALL) -o root -m 0755 -s mspdebug $(PREFIX)/bin/mspdebug
	$(INSTALL) -o root -m 0644 mspdebug.man $(PREFIX)/share/man/man1/mspdebug.1

.SUFFIXES: .c .o

mspdebug: main.o fet.o rf2500.o dis.o uif.o ihex.o elf32.o stab.o util.o \
	  bsl.o sim.o symmap.o gdb.o btree.o device.o rtools.o sym.o devcmd.o \
	  cproc.o vector.o cproc_util.o
	$(CC) $(LDFLAGS) -o $@ $^ -lusb $(READLINE_LIBS)

.c.o:
	$(CC) $(CFLAGS) $(READLINE_CFLAGS) -O1 -Wall -ggdb -o $@ -c $*.c
