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

#ifndef VECTOR_H_
#define VECTOR_H_

/* A vector is a flexible array type. It can be used to hold elements of
 * any type.
 *
 * elemsize: size in bytes of each element
 * capacity: maximum number of elements that ptr can hold
 * size:     number of elements currently held
 */
struct vector {
	void            *ptr;
	int             elemsize;
	int             capacity;
	int             size;
};

/* Create and destroy vectors */
void vector_init(struct vector *v, int elemsize);
void vector_destroy(struct vector *v);

/* Reallocate a vector to the given size. Returns 0 if successful, or -1
 * if memory could not be allocated.
 */
int vector_realloc(struct vector *v, int capacity);

/* Append any number of elements to the end of a vector, reallocating if
 * necessary. Returns 0 on success or -1 if memory could not be allocated.
 */
int vector_push(struct vector *v, const void *data, int count);

/* Remove the last element from a vector. */
void vector_pop(struct vector *v);

/* Dereference a vector, giving an expression for the element of type t at
 * position i in vector v. Use as follows:
 *
 *    struct vector v;
 *
 *    VECTOR_AT(v, 3, int) = 57;
 *    *VECTOR_PTR(v, 3, int) = 57;
 */
#define VECTOR_AT(v, i, t) (*VECTOR_PTR(v, i, t))
#define VECTOR_PTR(v, i, t) \
	((t *)((char *)(v).ptr + (i) * (v).elemsize))

#endif
