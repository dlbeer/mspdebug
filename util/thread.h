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

#ifndef THREAD_H_
#define THREAD_H_

/* Thread start routine signature for all OSes */
typedef void (*thread_func_t)(void *user_data);

#ifdef __Windows__
#include <windows.h>

/* Windows threads. Threads are identified by a HANDLE, which becomes
 * signalled when the thread exits.
 *
 * thread_create() returns 0 on success or -1 if an error occurs.
 */
typedef HANDLE thread_t;

static inline int thread_create(thread_t *t, thread_func_t func, void *arg)
{
	*t = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg,
			  0, NULL);

	return (*t) ? 0 : -1;
}

static inline void thread_join(thread_t t)
{
	WaitForSingleObject(t, INFINITE);
}

/* Windows mutexes. We use critical sections, because we don't need to
 * share between processes.
 *
 * None of these functions are expected to fail, although
 * InitializeCriticalSection may raise an exception on some versions of
 * Windows under low memory conditions.
 */
typedef CRITICAL_SECTION thread_lock_t;

static inline void thread_lock_init(thread_lock_t *lock)
{
	InitializeCriticalSection(lock);
}

static inline void thread_lock_destroy(thread_lock_t *lock)
{
	DeleteCriticalSection(lock);
}

static inline void thread_lock_acquire(thread_lock_t *lock)
{
	EnterCriticalSection(lock);
}

static inline void thread_lock_release(thread_lock_t *lock)
{
	LeaveCriticalSection(lock);
}

/* Windows condition variables. These are simulated using kernel event
 * objects. Note that this implementation is correct _only_ for the
 * case of a single waiter.
 */
typedef HANDLE thread_cond_t;

static inline void thread_cond_init(thread_cond_t *c)
{
	*c = CreateEvent(0, TRUE, FALSE, NULL);
}

static inline void thread_cond_destroy(thread_cond_t *c) {
	CloseHandle(*c);
}

static inline void thread_cond_wait(thread_cond_t *c, thread_lock_t *m)
{
	thread_lock_release(m);
	WaitForSingleObject(*c, INFINITE);
	thread_lock_acquire(m);
	ResetEvent(*c);
}

static inline void thread_cond_notify(thread_cond_t *c)
{
	SetEvent(*c);
}
#else /* __Windows__ */

#include <pthread.h>

/* POSIX thread creation. */
typedef pthread_t thread_t;

static inline int thread_create(thread_t *t, thread_func_t func, void *arg)
{
	return pthread_create(t, NULL, (void *(*)(void *))func, arg);
}

static inline void thread_join(thread_t t)
{
	pthread_join(t, NULL);
}

/* POSIX mutexes. */
typedef pthread_mutex_t thread_lock_t;

static inline void thread_lock_init(thread_lock_t *lock)
{
	pthread_mutex_init(lock, NULL);
}

static inline void thread_lock_destroy(thread_lock_t *lock)
{
	pthread_mutex_destroy(lock);
}

static inline void thread_lock_acquire(thread_lock_t *lock)
{
	pthread_mutex_lock(lock);
}

static inline void thread_lock_release(thread_lock_t *lock)
{
	pthread_mutex_unlock(lock);
}

/* POSIX condition variables. */
typedef pthread_cond_t thread_cond_t;

static inline void thread_cond_init(thread_cond_t *c)
{
	pthread_cond_init(c, NULL);
}

static inline void thread_cond_destroy(thread_cond_t *c)
{
	pthread_cond_destroy(c);
}

static inline void thread_cond_wait(thread_cond_t *c, thread_lock_t *m)
{
	pthread_cond_wait(c, m);
}

static inline void thread_cond_notify(thread_cond_t *c)
{
	pthread_cond_signal(c);
}
#endif

#endif
