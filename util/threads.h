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

#ifndef THREADS_H_
#define THREADS_H_

/* Portable thread utilities interface. */

#ifdef WIN32
#include <windows.h>

typedef CRITICAL_SECTION threads_lock_t;

static inline void threads_lock_init(threads_lock_t *lock)
{
	InitializeCriticalSection(lock);
}

static inline void threads_lock_destroy(threads_lock_t *lock)
{
	DeleteCriticalSection(lock);
}

static inline void threads_lock_acquire(threads_lock_t *lock)
{
	EnterCriticalSection(lock);
}

static inline void threads_lock_release(threads_lock_t *lock)
{
	LeaveCriticalSection(lock);
}

#else /* WIN32 */

#include <pthread.h>

typedef pthread_mutex_t threads_lock_t;

static inline void threads_lock_init(threads_lock_t *lock)
{
	pthread_mutex_init(lock, NULL);
}

static inline void threads_lock_destroy(threads_lock_t *lock)
{
	pthread_mutex_destroy(lock);
}

static inline void threads_lock_acquire(threads_lock_t *lock)
{
	pthread_mutex_lock(lock);
}

static inline void threads_lock_release(threads_lock_t *lock)
{
	pthread_mutex_unlock(lock);
}
#endif


#endif
