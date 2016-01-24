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

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "vector.h"

void vector_init(struct vector *v, int elemsize)
{
	memset(v, 0, sizeof(*v));
	v->elemsize = elemsize;
}

void vector_destroy(struct vector *v)
{
	if (v->ptr)
		free(v->ptr);

	memset(v, 0, sizeof(*v));
}

int vector_realloc(struct vector *v, int capacity)
{
	assert (capacity >= 0);

	if (capacity) {
		void *new_ptr = realloc(v->ptr, capacity * v->elemsize);

		if (!new_ptr)
			return -1;

		v->ptr = new_ptr;
	} else {
		free(v->ptr);
		v->ptr = NULL;
	}

	v->capacity = capacity;
	if (v->size > capacity)
		v->size = capacity;

	return 0;
}

static int size_for(struct vector *v, int needed)
{
	int cap = needed;

	/* Find the smallest power of 2 which is greater than the
	 * necessary capacity.
	 */
	while (cap & (cap - 1))
		cap &= (cap - 1);
	if (cap < needed)
		cap <<= 1;

	/* Don't allocate fewer than 8 elements */
	if (cap < 8)
		cap = 8;

	if (v->capacity >= cap && v->capacity <= cap * 2)
		return 0;

	if (vector_realloc(v, cap) < 0)
		return -1;

	return 0;
}

int vector_push(struct vector *v, const void *data, int count)
{
	int needed = v->size + count;

	assert (count >= 0);

	if (size_for(v, needed) < 0)
		return -1;

	memcpy((char *)v->ptr + v->size * v->elemsize,
	       data,
	       count * v->elemsize);
	v->size += count;

	return 0;
}

void vector_pop(struct vector *v)
{
	if (v->size <= 0)
		return;

	size_for(v, v->size - 1);
	v->size--;
}
