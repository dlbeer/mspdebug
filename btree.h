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

#ifndef BTREE_H_
#define BTREE_H_

typedef int (*btree_compare_t)(const void *left, const void *right);

struct btree_def {
	int             key_size;
	int             data_size;
	int             branches;

	const void      *zero;
	btree_compare_t compare;
};

struct btree;
typedef struct btree *btree_t;

/* Instantiate a new B+Tree, given the definition. The definition must
 * remain valid for the duration of the B+Tree's existence.
 *
 * Returns NULL on error.
 */
btree_t btree_alloc(const struct btree_def *def);

/* Destroy a B+Tree */
void btree_free(btree_t bt);

/* Clear all data from a B+Tree */
void btree_clear(btree_t bt);

/* Add or update a record in a B+Tree. Any existing data for the key will
 * be overwritten.
 *
 * Specifying a NULL key causes the cursor value to be overwritten.
 *
 * Returns 1 if the key already existed, 0 if a new key was inserted,
 * or -1 if an error occurs (failed memory allocation).
 */
int btree_put(btree_t bt, const void *key, const void *data);

/* Delete a value from a B+Tree. If the key is NULL, the value at the cursor
 * is deleted, and the cursor is updated to point to the next item.
 *
 * Returns 0 if the key existed, 1 if it didn't.
 */
int btree_delete(btree_t bt, const void *key);

/* Retrieve an item from the B+Tree. If the key is NULL, the value at the
 * cursor is retrieved. Optionally, the actual key value can be retrieved
 * into key_ret (this may be useful if the keys are compared case-insensitive,
 * or if fetching the cursor item).
 *
 * Returns 0 if the key existed, 1 if it didn't.
 */
int btree_get(btree_t bt, const void *key, void *data);

/* Cursor manipulation. This function takes a cursor movement command,
 * some of which require a key argument.
 *
 * After the command is completed, the currently selected key and value
 * are returned via the supplied pointers key_ret and data_ret (each of
 * which may be NULL).
 *
 * Returns 0 if a record was selected, 1 if not.
 */
typedef enum {
	BTREE_EXACT,    /* find the exact item */
	BTREE_LE,       /* find the largest item <= the key */
	BTREE_NEXT,     /* find the next item after the cursor */
	BTREE_FIRST,    /* find the first item in the tree */
	BTREE_CLEAR,    /* clear the cursor */
	BTREE_READ      /* fetch the current record without moving */
} btree_selmode_t;

int btree_select(btree_t bt, const void *key, btree_selmode_t mode,
		 void *key_ret, void *data_ret);

#endif
