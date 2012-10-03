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

#ifndef POWERBUF_H_
#define POWERBUF_H_

/* This header file describes a data structure for recording power
 * profiling data.
 *
 * Power profile data consists of zero or more discontiguous "sessions".
 * Within each session is a sequence of evenly spaced current samples
 * and the corresponding MAB values.
 */

#include <time.h>
#include "util.h"

/* Per-session information header. */
struct powerbuf_session {
	/* Time that this session started. */
	time_t			wall_clock;

	/* Index of first sample in sample buffer corresponding to this
	 * session.
	 */
	unsigned int		start_index;

	/* Integral of current consumed over this session. */
	unsigned long long	total_ua;
};

#define POWERBUF_MAX_SESSIONS		8
#define POWERBUF_DEFAULT_SAMPLES	131072

/* Power buffer data structure. The power buffer contains three circular
 * buffers, two of which are dynamically allocated. Helper functions are
 * provided for managing access.
 */
struct powerbuf {
	/* These parameters are set at the time of construction and
	 * shouldn't be modified.
	 */
	unsigned int			interval_us;
	unsigned int			max_samples;

	/* Session circular buffer. New sessions are created by
	 * overwriting the head and advancing it. Old sessions drop out
	 * the end of the buffer.
	 */
	struct powerbuf_session		sessions[POWERBUF_MAX_SESSIONS];
	unsigned int			session_head;
	unsigned int			session_tail;

	/* Sample circular buffer. A single head/tail pair indicates the
	 * extent of both the current and MAB samples, since they are
	 * synchronous with respect to one another.
	 *
	 * Adding samples to the buffer causes old samples to fall out
	 * the tail end. If enough samples are pushed, old sessions will
	 * also drop out above.
	 */
	unsigned int			*current_ua;
	address_t			*mab;
	unsigned int			current_head;
	unsigned int			current_tail;

	/* Index by MAB. This is a flat array which points to indices
	 * within current_ua/mab. The indices are sorted in order of
	 * increasing MAB.
	 *
	 * Note that this array is invalidated by any modification to
	 * the sample buffers. You need to call powerbuf_sort() before
	 * accessing it.
	 */
	int				sort_valid;
	unsigned int			*sorted;
};

typedef struct powerbuf *powerbuf_t;

/* Allocate/destroy a power buffer. The number of samples is fixed and
 * can't change over the lifetime of the buffer.
 */
powerbuf_t powerbuf_new(unsigned int max_samples, unsigned int interval_us);
void powerbuf_free(powerbuf_t pb);

/* Clear all sessions and samples from the buffer. */
void powerbuf_clear(powerbuf_t pb);

/* Begin a new session. This may cause an old session to drop out the
 * end of the buffer. If the current session is empty, it will be
 * overwritten.
 *
 * powerbuf_end_session() simply discards any empty session previously
 * created by powerbuf_begin_session().
 */
void powerbuf_begin_session(powerbuf_t pb, time_t when);
void powerbuf_end_session(powerbuf_t pb);

/* This interface provides a convenient way of accessing the session
 * circular buffer. Rather than using direct indices, we present an
 * interface that mimics a flat array. Indices (rev_idx) start at 0,
 * with 0 being the index of the most recent session.
 */
unsigned int powerbuf_num_sessions(powerbuf_t pb);
const struct powerbuf_session *powerbuf_session_info(powerbuf_t pb,
	unsigned int rev_idx, unsigned int *length);

/* Push samples into the buffer. The number of elements in both the
 * current_ua and mab arrays is given by the parameter "count".
 */
void powerbuf_add_samples(powerbuf_t pb, unsigned int count,
			  const unsigned int *current_ua,
			  const address_t *mab);

/* Retrieve the last known MAB for this session, or 0 if none exists. */
address_t powerbuf_last_mab(powerbuf_t pb);

/* Prepare the sorted MAB index. */
void powerbuf_sort(powerbuf_t pb);

/* Obtain charge consumption data by MAB over all sessions. This
 * automatically calls powerbuf_sort() if necessary.
 *
 * Returns the number of samples found on success. The sum of all
 * current samples is written to the sum_ua argument.
 */
int powerbuf_get_by_mab(powerbuf_t pb, address_t mab,
			unsigned long long *sum_ua);

#endif
