/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2011 Daniel Beer
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

#include "dynload.h"
#include "util.h"

#ifdef __Windows__

dynload_handle_t dynload_open(const char *filename)
{
	return LoadLibrary(filename);
}

void dynload_close(dynload_handle_t hnd)
{
	FreeLibrary(hnd);
}

void *dynload_sym(dynload_handle_t hnd, const char *name)
{
	return GetProcAddress(hnd, name);
}

const char *dynload_error(void)
{
	return last_error();
}

#else /* __Windows__ */

#include <dlfcn.h>

dynload_handle_t dynload_open(const char *filename)
{
	return dlopen(filename, RTLD_LAZY);
}

void dynload_close(dynload_handle_t hnd)
{
	dlclose(hnd);
}

void *dynload_sym(dynload_handle_t hnd, const char *name)
{
	return dlsym(hnd, name);
}

const char *dynload_error(void)
{
	return dlerror();
}

#endif
