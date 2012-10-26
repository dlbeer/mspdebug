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

#include <stdlib.h>
#include <string.h>
#include "powerbuf.h"

powerbuf_t powerbuf_new(unsigned int max_samples, unsigned int interval_us)
{
	powerbuf_t pb = malloc(sizeof(*pb));

	if (!pb)
		return NULL;

	if (max_samples <= 0) {
		free(pb);
		return NULL;
	}

	memset(pb, 0, sizeof(*pb));

	pb->current_ua = malloc(sizeof(pb->current_ua[0]) * max_samples);
	if (!pb->current_ua) {
		free(pb);
		return NULL;
	}

	pb->mab = malloc(sizeof(pb->mab[0]) * max_samples);
	if (!pb->mab) {
		free(pb->current_ua);
		free(pb);
		return NULL;
	}

	pb->sorted = malloc(sizeof(pb->sorted[0]) * max_samples);
	if (!pb->sorted) {
		free(pb->current_ua);
		free(pb->mab);
		free(pb);
		return NULL;
	}

	pb->interval_us = interval_us;
	pb->max_samples = max_samples;

	pb->session_head = pb->session_tail = 0;
	pb->current_head = pb->current_tail = 0;

	return pb;
}

void powerbuf_free(powerbuf_t pb)
{
	free(pb->current_ua);
	free(pb->mab);
	free(pb->sorted);
	free(pb);
}

void powerbuf_clear(powerbuf_t pb)
{
	pb->session_head = pb->session_tail = 0;
	pb->current_head = pb->current_tail = 0;
	pb->sort_valid = 0;
}

static unsigned int session_length(powerbuf_t pb, unsigned int idx)
{
	const unsigned int next_idx = (idx + 1) % POWERBUF_MAX_SESSIONS;
	unsigned int end_index = pb->current_head;

	/* If a session follows this one, the end index is the start
	 * index of the following session. Otherwise, it's the end index
	 * of the entire current/MAB buffer.
	 */
	if (next_idx != pb->session_head)
		end_index = pb->sessions[next_idx].start_index;

	/* Return (end-start) modulo max_samples */
	return (end_index + pb->max_samples - pb->sessions[idx].start_index) %
		pb->max_samples;
}

static void pop_oldest_session(powerbuf_t pb)
{
	unsigned int length = session_length(pb, pb->session_tail);

	/* Remove corresponding samples from the tail of the current/MAB
	 * buffers.
	 */
	pb->current_tail = (pb->current_tail + length) % pb->max_samples;

	/* Remove the session from the session buffer. */
	pb->session_tail = (pb->session_tail + 1) % POWERBUF_MAX_SESSIONS;
}

void powerbuf_begin_session(powerbuf_t pb, time_t when)
{
	struct powerbuf_session *s;
	unsigned int next_head;

	/* If the most recent session is empty, remove it */
	powerbuf_end_session(pb);

	/* If the session buffer is full, drop the oldest */
	next_head = (pb->session_head + 1) % POWERBUF_MAX_SESSIONS;
	if (next_head == pb->session_tail)
		pop_oldest_session(pb);

	/* Copy data to the head */
	s = &pb->sessions[pb->session_head];
	s->wall_clock = when;
	s->start_index = pb->current_head;
	s->total_ua = 0;

	/* Advance the head pointer */
	pb->session_head = next_head;
}

/* Return the index of the nth most recent session */
static unsigned int rev_index(powerbuf_t pb, unsigned int n)
{
	return (pb->session_head + POWERBUF_MAX_SESSIONS - 1 - n) %
		 POWERBUF_MAX_SESSIONS;
}

void powerbuf_end_session(powerbuf_t pb)
{
	/* (head-1) modulo MAX_SESSIONS */
	const unsigned int last_idx = rev_index(pb, 0);

	/* If there are no sessions, do nothing */
	if (pb->session_head == pb->session_tail)
		return;

	/* If no samples were added since the session began, decrement
	 * the head pointer.
	 */
	if (pb->sessions[last_idx].start_index == pb->current_head)
		pb->session_head = last_idx;
}

unsigned int powerbuf_num_sessions(powerbuf_t pb)
{
	/* (head-tail) modulo MAX_SESSIONS */
	return (pb->session_head + POWERBUF_MAX_SESSIONS - pb->session_tail) %
		POWERBUF_MAX_SESSIONS;
}

const struct powerbuf_session *powerbuf_session_info(powerbuf_t pb,
	unsigned int rev_idx, unsigned int *length)
{
	/* (head-1-rev_idx) modulo MAX_SESSIONS */
	const unsigned int idx_map = rev_index(pb, rev_idx);

	if (length)
		*length = session_length(pb, idx_map);

	return &pb->sessions[idx_map];
}

static void ensure_room(powerbuf_t pb, unsigned int required)
{
	unsigned int room =
		(pb->current_tail + pb->max_samples - pb->current_head - 1) %
			pb->max_samples;

	/* Drop old sessions if they're smaller than what we need to
	 * reclaim.
	 */
	while (room < required && powerbuf_num_sessions(pb) > 1) {
		const unsigned int len = session_length(pb, pb->session_tail);

		if (room + len > required)
			break;

		pop_oldest_session(pb);
		room += len;
	}

	/* If we still lack space, it must be because the oldest session
	 * is larger than what we still need to reclaim (we'll never be
	 * asked to reclaim more than the buffer can hold).
	 *
	 * We also know at this point that (required-room) is <= the
	 * length of the oldest session.
	 */
	while (room < required) {
		struct powerbuf_session *old =
			&pb->sessions[pb->session_tail];
		unsigned int cont_len = pb->max_samples - old->start_index;
		int i;

		if (cont_len + room > required)
			cont_len = required - room;

		/* Un-integrate current */
		for (i = 0; i < cont_len; i++)
			old->total_ua -=
				pb->current_ua[old->start_index + i];

		/* Advance the start index and buffer tail */
		old->start_index = (old->start_index + cont_len) %
			pb->max_samples;
		pb->current_tail = (pb->current_tail + cont_len) %
			pb->max_samples;

		room += cont_len;
	}
}

void powerbuf_add_samples(powerbuf_t pb, unsigned int count,
			  const unsigned int *current_ua,
			  const address_t *mab)
{
	struct powerbuf_session *cur = &pb->sessions[rev_index(pb, 0)];
	int i;

	/* If no session is active, do nothing */
	if (pb->session_head == pb->session_tail)
		return;

	/* Make sure that we can't overflow the buffer in a single
	 * chunk.
	 */
	if (count > pb->max_samples - 1) {
		int extra = pb->max_samples - 1 - count;

		current_ua += extra;
		mab += extra;
		count -= extra;
	}

	/* Push old samples/sessions out of the buffer if we need to. */
	ensure_room(pb, count);

	/* Add current integral to the session's running count */
	for (i = 0; i < count; i++)
		cur->total_ua += current_ua[i];

	/* Add samples in contiguous chunks */
	while (count) {
		unsigned int cont_len = pb->max_samples - pb->current_head;

		/* Don't copy more than we have */
		if (cont_len > count)
			cont_len = count;

		/* Copy samples */
		memcpy(pb->current_ua + pb->current_head, current_ua,
			sizeof(pb->current_ua[0]) * cont_len);
		memcpy(pb->mab + pb->current_head, mab,
			sizeof(pb->mab[0]) * cont_len);
		pb->current_head = (pb->current_head +
					cont_len) % pb->max_samples;

		/* Advance source pointers and count */
		current_ua += cont_len;
		mab += cont_len;
		count -= cont_len;
	}

	pb->sort_valid = 0;
}

address_t powerbuf_last_mab(powerbuf_t pb)
{
	const struct powerbuf_session *s = &pb->sessions[rev_index(pb, 0)];
	const unsigned int last = (pb->current_head + pb->max_samples - 1) %
		pb->max_samples;

	if (s->start_index == pb->current_head)
		return 0;

	return pb->mab[last];
}

static void sift_down(powerbuf_t pb, int start, int end)
{
	int root = start;

	while (root * 2 + 1 <= end) {
		int left_child = root * 2 + 1;
		int biggest = root;
		unsigned int temp;

		/* Find the largest of
		 *   (root, left child, right child)
		 */
		if (pb->mab[pb->sorted[biggest]] <
		    pb->mab[pb->sorted[left_child]])
			biggest = left_child;
		if (left_child + 1 <= end &&
		    (pb->mab[pb->sorted[biggest]] <
		     pb->mab[pb->sorted[left_child + 1]]))
			biggest = left_child + 1;

		/* If no changes are needed, the heap property is ok and
		 * we can stop.
		 */
		if (biggest == root)
			break;

		/* Swap the root with its largest child */
		temp = pb->sorted[biggest];
		pb->sorted[biggest] = pb->sorted[root];
		pb->sorted[root] = temp;

		/* Continue to push down the old root (now a child) */
		root = biggest;
	}
}

static void heapify(powerbuf_t pb, int num_samples)
{
	int start = (num_samples - 2) / 2;

	while (start >= 0) {
		sift_down(pb, start, num_samples - 1);
		start--;
	}
}

static void heap_extract(powerbuf_t pb, int num_samples)
{
	int end = num_samples - 1;

	while (end > 0) {
		unsigned int temp;

		/* Swap the top of the heap with the end of the array,
		 * and shrink the heap.
		 */
		temp = pb->sorted[0];
		pb->sorted[0] = pb->sorted[end];
		pb->sorted[end] = temp;
		end--;

		/* Fix up the heap (push down the new root) */
		sift_down(pb, 0, end);
	}
}

void powerbuf_sort(powerbuf_t pb)
{
	const unsigned int num_samples =
		(pb->current_head + pb->max_samples - pb->current_tail) %
			pb->max_samples;
	unsigned int i;

	if (pb->sort_valid)
		return;

	/* Prepare an index list */
	for (i = 0; i < num_samples; i++)
		pb->sorted[i] = (pb->current_tail + i) % pb->max_samples;

	if (num_samples < 2) {
		pb->sort_valid = 1;
		return;
	}

	heapify(pb, num_samples);
	heap_extract(pb, num_samples);
	pb->sort_valid = 1;
}

/* Find the index within the sorted index of the first sample with an
 * MAB >= the given mab parameter.
 */
static int find_mab_ge(powerbuf_t pb, address_t mab)
{
	const int num_samples =
		(pb->current_head + pb->max_samples - pb->current_tail) %
			pb->max_samples;
	int low = 0;
	int high = num_samples - 1;

	while (low <= high) {
		int mid = (low + high) / 2;

		if (pb->mab[pb->sorted[mid]] < mab)
			low = mid + 1;
		else if ((mid <= 0) || (pb->mab[pb->sorted[mid - 1]] < mab))
			return mid;
		else
			high = mid - 1;
	}

	return -1;
}

int powerbuf_get_by_mab(powerbuf_t pb, address_t mab,
			unsigned long long *sum_ua)
{
	const unsigned int num_samples =
		(pb->current_head + pb->max_samples - pb->current_tail) %
			pb->max_samples;
	int i;
	int count = 0;

	if (!pb->sort_valid)
		powerbuf_sort(pb);

	i = find_mab_ge(pb, mab);
	if (i < 0)
		return 0;

	*sum_ua = 0;

	while ((i < num_samples) && (pb->mab[pb->sorted[i]] == mab)) {
		*sum_ua += pb->current_ua[pb->sorted[i]];
		count++;
		i++;
	}

	return count;
}
