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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btree.h"
#include "output.h"
#include "util.h"

#define MAX_HEIGHT 16

/* Btree pages consist of the following: a page header (struct btree_page),
 * followed by a block of memory consisting of:
 *
 * For a leaf node:
 *    An array of N keys, then an array of N data.
 *
 * For a non-leaf node:
 *    An array of N keys, then an array of N struct btree_page *.
 *
 * Where N is the branch factor.
 */
struct btree_page {
	int                     height;
	int                     num_children;
	struct btree            *owner;
	const struct btree_def  *def;
};

#define PAGE_KEY(p, i)					\
	(((char *)(p)) + sizeof(struct btree_page) +	\
	 (i) * (p)->def->key_size)
#define PAGE_DATA(p, i)					\
	(((char *)(p)) + sizeof(struct btree_page) +	\
	 (p)->def->branches * (p)->def->key_size +	\
	 (i) * (p)->def->data_size)
#define PAGE_PTR(p, i)					\
	((struct btree_page **)				\
	(((char *)(p)) + sizeof(struct btree_page) +	\
	 (p)->def->branches * (p)->def->key_size +	\
	 (i) * sizeof(struct btree_page *)))		\

struct btree {
	const struct btree_def  *def;
	struct btree_page       *root;

	struct btree_page       *path[MAX_HEIGHT];
	int                     slot[MAX_HEIGHT];
};

/************************************************************************
 * Debugging
 */

#ifdef DEBUG_BTREE

static void check_page(struct btree_page *p,
		       const void *lbound, const void *ubound,
		       int height)
{
	const struct btree_def *def = p->def;
	int i;

	assert (p);
	assert (p->height == height);

	if (p != p->owner->root) {
		assert (p->num_children >= def->branches / 2);
		assert (p->num_children <= def->branches);
	}

	for (i = 0; i < p->num_children; i++) {
		const void *key = PAGE_KEY(p, i);
		const void *next_key = ubound;

		if (i + 1 < p->num_children)
			next_key = PAGE_KEY(p, i + 1);

		assert (def->compare(key, lbound) >= 0);
		if (next_key) {
			assert (def->compare(key, next_key) < 0);
		}
		if (ubound) {
			assert (def->compare(key, ubound) < 0);
		}

		if (p->height)
			check_page(*PAGE_PTR(p, i), key, next_key, height - 1);
	}
}

static void check_btree(btree_t bt)
{
	assert (bt->def);

	if (bt->root->height) {
		assert (bt->root->num_children >= 2);
	}

	check_page(bt->root, bt->def->zero, NULL, bt->root->height);
}

#else
#define check_btree(bt)
#endif

/************************************************************************
 * B+Tree auxiliary functions
 */

static void destroy_page(struct btree_page *p)
{
	if (!p)
		return;

	if (p->height) {
		int i;

		for (i = 0; i < p->num_children; i++)
			destroy_page(*PAGE_PTR(p, i));
	}

	free(p);
}

static struct btree_page *allocate_page(btree_t bt, int height)
{
	const struct btree_def *def = bt->def;
	struct btree_page *p;
	int size = sizeof(*p) + def->key_size * def->branches;

	if (height)
		size += sizeof(struct btree_page *) * def->branches;
	else
		size += sizeof(def->data_size) * def->branches;

	p = malloc(size);
	if (!p) {
		printc_err("btree: couldn't allocate page: %s\n",
			   last_error());
		return NULL;
	}

	memset(p, 0, size);
	p->def = bt->def;
	p->owner = bt;
	p->height = height;

	return p;
}

static void split_page(struct btree_page *op, struct btree_page *np)
{
	const struct btree_def *def = op->def;
	btree_t bt = op->owner;
	const int halfsize = def->branches / 2;

	assert (op->num_children == def->branches);

	memcpy(PAGE_KEY(np, 0), PAGE_KEY(op, halfsize),
	       halfsize * def->key_size);

	if (op->height)
		memcpy(PAGE_PTR(np, 0), PAGE_PTR(op, halfsize),
		       halfsize * sizeof(struct btree_page *));
	else
		memcpy(PAGE_DATA(np, 0), PAGE_DATA(op, halfsize),
		       halfsize * def->data_size);

	op->num_children = halfsize;
	np->num_children = halfsize;

	/* Fix up the cursor if we split an active page */
	if (bt->slot[0] >= 0 && bt->path[op->height] == op &&
	    bt->slot[op->height] > op->num_children) {
		bt->slot[op->height] -= op->num_children;
		bt->path[op->height] = np;
	}
}

static void insert_data(struct btree_page *p, int s,
			const void *key, const void *data)
{
	const struct btree_def *def = p->def;
	btree_t bt = p->owner;
	int r = p->num_children - s;

	assert (!p->height);
	assert (p->num_children < def->branches);
	assert (s >= 0 && s <= p->num_children);

	memmove(PAGE_KEY(p, s + 1), PAGE_KEY(p, s),
		r * def->key_size);
	memmove(PAGE_DATA(p, s + 1), PAGE_DATA(p, s),
		r * def->data_size);

	memcpy(PAGE_KEY(p, s), key, def->key_size);
	memcpy(PAGE_DATA(p, s), data, def->data_size);
	p->num_children++;

	/* Fix up the cursor if we inserted before it, or if we're inserting
	 * a pointer to the cursor data itself (as in a borrow).
	 */
	if (bt->slot[0] >= 0) {
		if (data == PAGE_DATA(bt->path[0], bt->slot[0])) {
			bt->path[0] = p;
			bt->slot[0] = s;
		} else if (bt->path[0] == p && s <= bt->slot[0]) {
			bt->slot[0]++;
		}
	}
}

static void insert_ptr(struct btree_page *p, int s,
		       const void *key, struct btree_page *ptr)
{
	const struct btree_def *def = p->def;
	btree_t bt = p->owner;
	int r = p->num_children - s;

	assert (p->height);
	assert (p->num_children < def->branches);
	assert (s >= 0 && s <= p->num_children);

	memmove(PAGE_KEY(p, s + 1), PAGE_KEY(p, s),
		r * def->key_size);
	memmove(PAGE_PTR(p, s + 1), PAGE_PTR(p, s),
		r * sizeof(struct btree_page *));

	memcpy(PAGE_KEY(p, s), key, def->key_size);
	*PAGE_PTR(p, s) = ptr;
	p->num_children++;

	/* Fix up the cursor if we inserted before it, or if we just inserted
	 * the pointer for the active path (as in a split or borrow).
	 */
	if (bt->slot[0] >= 0) {
		if (ptr == bt->path[p->height - 1]) {
			bt->path[p->height] = p;
			bt->slot[p->height] = s;
		} else if (bt->path[p->height] == p &&
			   s <= bt->slot[p->height]) {
			bt->slot[p->height]++;
		}
	}
}

static void delete_item(struct btree_page *p, int s)
{
	const struct btree_def *def = p->def;
	btree_t bt = p->owner;
	int r = p->num_children - s - 1;

	assert (s >= 0 && s < p->num_children);

	memmove(PAGE_KEY(p, s), PAGE_KEY(p, s + 1),
		r * def->key_size);

	if (p->height)
		memmove(PAGE_PTR(p, s), PAGE_PTR(p, s + 1),
			r * sizeof(struct btree_page *));
	else
		memmove(PAGE_DATA(p, s), PAGE_DATA(p, s + 1),
			r * def->data_size);

	p->num_children--;

	/* Fix up the cursor if we deleted before it */
	if (bt->slot[0] >= 0 && bt->path[p->height] == p &&
	    s <= bt->slot[p->height])
		bt->slot[p->height]--;
}

static void move_item(struct btree_page *from, int from_pos,
		      struct btree_page *to, int to_pos)
{
	if (from->height)
		insert_ptr(to, to_pos, PAGE_KEY(from, from_pos),
			   *PAGE_PTR(from, from_pos));
	else
		insert_data(to, to_pos, PAGE_KEY(from, from_pos),
			    PAGE_DATA(from, from_pos));

	delete_item(from, from_pos);
}

static void merge_pages(struct btree_page *lower,
			struct btree_page *higher)
{
	const struct btree_def *def = lower->def;
	btree_t bt = lower->owner;

	assert (lower->num_children + higher->num_children < def->branches);

	memcpy(PAGE_KEY(lower, lower->num_children),
	       PAGE_KEY(higher, 0),
	       higher->num_children * def->key_size);

	if (lower->height)
		memcpy(PAGE_PTR(lower, lower->num_children),
		       PAGE_PTR(higher, 0),
		       higher->num_children * sizeof(struct btree_page *));
	else
		memcpy(PAGE_DATA(lower, lower->num_children),
		       PAGE_DATA(higher, 0),
		       higher->num_children * def->data_size);

	lower->num_children += higher->num_children;

	/* Fix up the cursor if we subsumed an active page */
	if (bt->slot[0] >= 0) {
		if (bt->path[higher->height] == higher) {
			bt->path[higher->height] = lower;
			bt->slot[higher->height] += lower->num_children;
		}
	}
}

static int find_key_le(const struct btree_page *p, const void *key)
{
	const struct btree_def *def = p->def;
	int i;

	for (i = 0; i < p->num_children; i++)
		if (def->compare(key, PAGE_KEY(p, i)) < 0)
			return i - 1;

	return p->num_children - 1;
}

static int trace_path(btree_t bt, const void *key,
		      struct btree_page **path, int *slot)
{
	const struct btree_def *def = bt->def;
	struct btree_page *p = bt->root;
	int h;

	for (h = p->height; h >= 0; h--) {
		int s = find_key_le(p, key);

		path[h] = p;
		slot[h] = s;

		if (h) {
			assert (s >= 0);
			p = *PAGE_PTR(p, s);
		} else if (s >= 0 && !def->compare(key, PAGE_KEY(p, s))) {
			return 1;
		}
	}

	return 0;
}

static void cursor_first(btree_t bt)
{
	int h;
	struct btree_page *p = bt->root;

	if (!bt->root->num_children) {
		bt->slot[0] = -1;
		return;
	}

	for (h = bt->root->height; h >= 0; h--) {
		assert (p->num_children > 0);

		bt->path[h] = p;
		bt->slot[h] = 0;

		if (h)
			p = *PAGE_PTR(p, 0);
	}
}

static void cursor_next(btree_t bt)
{
	int h;

	if (bt->slot[0] < 0)
		return;

	/* Ascend until we find a suitable sibling */
	for (h = 0; h <= bt->root->height; h++) {
		struct btree_page *p = bt->path[h];

		if (bt->slot[h] + 1 < p->num_children) {
			bt->slot[h]++;

			while (h > 0) {
				p = *PAGE_PTR(p, bt->slot[h]);
				h--;
				bt->slot[h] = 0;
				bt->path[h] = p;
			}

			return;
		}
	}

	/* Exhausted all levels */
	bt->slot[0] = -1;
}

/************************************************************************
 * Public interface
 */

btree_t btree_alloc(const struct btree_def *def)
{
	btree_t bt;

	if (def->branches < 2 || (def->branches & 1)) {
		printc_err("btree: invalid branch count: %d\n",
			def->branches);
		return NULL;
	}

	bt = malloc(sizeof(*bt));
	if (!bt) {
		printc_err("btree: couldn't allocate tree: %s\n",
			last_error());
		return NULL;
	}

	memset(bt, 0, sizeof(*bt));
	bt->def = def;
	bt->slot[0] = -1;

	bt->root = allocate_page(bt, 0);
	if (!bt->root) {
		printc_err("btree: couldn't allocate root node: %s\n",
			   last_error());
		free(bt);
		return NULL;
	}

	return bt;
}

void btree_free(btree_t bt)
{
	check_btree(bt);
	destroy_page(bt->root);
	free(bt);
}

void btree_clear(btree_t bt)
{
	struct btree_page *p;
	struct btree_page *path_up = 0;

	check_btree(bt);

	/* The cursor will have nothing to point to after this. */
	bt->slot[0] = -1;

	/* First, find the last leaf node, which we can re-use as an
	 * empty root.
	 */
	p = bt->root;
	while (p->height) {
		path_up = p;
		p = *PAGE_PTR(p, p->num_children - 1);
	}

	/* Unlink it from the tree and then destroy everything else. */
	if (path_up) {
		path_up->num_children--;
		destroy_page(bt->root);
	}

	/* Clear it out and make it the new root */
	p->num_children = 0;
	bt->root = p;
}

int btree_put(btree_t bt, const void *key, const void *data)
{
	const struct btree_def *def = bt->def;
	struct btree_page *new_root = NULL;
	struct btree_page *path_new[MAX_HEIGHT] = {0};
	struct btree_page *path_old[MAX_HEIGHT] = {0};
	int slot_old[MAX_HEIGHT] = {0};
	int h;

	check_btree(bt);

	/* Special case: cursor overwrite */
	if (!key) {
		if (bt->slot[0] < 0) {
			printc_err("btree: put at invalid cursor\n");
			return -1;
		}

		memcpy(PAGE_DATA(bt->path[0], bt->slot[0]), data,
		       def->data_size);
		return 1;
	}

	/* Find a path down the tree that leads to the page which should
	 * contain this datum (though the page might be too big to hold it).
	 */
	if (trace_path(bt, key, path_old, slot_old)) {
		/* Special case: overwrite existing item */
		memcpy(PAGE_DATA(path_old[0], slot_old[0]), data,
		       def->data_size);
		return 1;
	}

	/* Trace from the leaf up. If the leaf is at its maximum size, it will
	 * need to split, and cause a pointer to be added in the parent page
	 * of the same node (which may in turn cause it to split).
	 */
	for (h = 0; h <= bt->root->height; h++) {
		if (path_old[h]->num_children < def->branches)
			break;

		path_new[h] = allocate_page(bt, h);
		if (!path_new[h])
			goto fail;
	}

	/* If the split reaches the top (i.e. the root splits), then we need
	 * to allocate a new root node.
	 */
	if (h > bt->root->height) {
		if (h >= MAX_HEIGHT) {
			printc_err("btree: maximum height exceeded\n");
			goto fail;
		}

		new_root = allocate_page(bt, h);
		if (!new_root)
			goto fail;
	}

	/* Trace up to one page above the split. At each page that needs
	 * splitting, copy the top half of keys into the new page. Also,
	 * insert a key into one of the pages at all pages from the leaf
	 * to the page above the top of the split.
	 */
	for (h = 0; h <= bt->root->height; h++) {
		int s = slot_old[h] + 1;
		struct btree_page *p = path_old[h];

		/* If there's a split at this level, copy the top half of
		 * the keys from the old page to the new one. Check to see
		 * if the position we were going to insert into is in the
		 * old page or the new one.
		 */
		if (path_new[h]) {
			split_page(path_old[h], path_new[h]);

			if (s > p->num_children) {
				s -= p->num_children;
				p = path_new[h];
			}
		}

		/* Insert the key in the appropriate page */
		if (h)
			insert_ptr(p, s, PAGE_KEY(path_new[h - 1], 0),
				   path_new[h - 1]);
		else
			insert_data(p, s, key, data);

		/* If there was no split at this level, there's nothing to
		 * insert higher up, and we're all done.
		 */
		if (!path_new[h])
			return 0;
	}

	/* If we made it this far, the split reached the top of the tree, and
	 * we need to grow it using the extra page we allocated.
	 */
	assert (new_root);

	if (bt->slot[0] >= 0) {
		/* Fix up the cursor, if active */
		bt->slot[new_root->height] =
			bt->path[bt->root->height] == new_root ? 1 : 0;
		bt->path[new_root->height] = new_root;
	}

	memcpy(PAGE_KEY(new_root, 0), def->zero, def->key_size);
	*PAGE_PTR(new_root, 0) = path_old[h - 1];
	memcpy(PAGE_KEY(new_root, 1), PAGE_KEY(path_new[h - 1], 0),
	       def->key_size);
	*PAGE_PTR(new_root, 1) = path_new[h - 1];
	new_root->num_children = 2;
	bt->root = new_root;

	return 0;

 fail:
	for (h = 0; h <= bt->root->height; h++)
		if (path_new[h])
			free(path_new[h]);
	return -1;
}

int btree_delete(btree_t bt, const void *key)
{
	const struct btree_def *def = bt->def;
	const int halfsize = def->branches / 2;
	struct btree_page *path[MAX_HEIGHT] = {0};
	int slot[MAX_HEIGHT] = {0};
	int h;

	check_btree(bt);

	/* Trace a path to the item to be deleted */
	if (!key) {
		if (bt->slot[0] < 0)
			return 1;

		memcpy(path, bt->path, sizeof(path));
		memcpy(slot, bt->slot, sizeof(slot));
	} else if (!trace_path(bt, key, path, slot)) {
		return 1;
	}

	/* Select the next item if we're deleting at the cursor */
	if (bt->slot[0] == slot[0] && bt->path[0] == path[0])
		cursor_next(bt);

	/* Delete from the leaf node. If it's still full enough, then we don't
	 * need to do anything else.
	 */
	delete_item(path[0], slot[0]);
	if (path[0]->num_children >= halfsize)
		return 0;

	/* Trace back up the tree, fixing underfull nodes. If we can fix by
	 * borrowing, do it and we're done. Otherwise, we need to fix by
	 * merging, which may result in another underfull node, and we need
	 * to continue.
	 */
	for (h = 1; h <= bt->root->height; h++) {
		struct btree_page *p = path[h];
		struct btree_page *c = path[h - 1];
		int s = slot[h];

		if (s > 0) {
			/* Borrow/merge from lower page */
			struct btree_page *d = *PAGE_PTR(p, s - 1);

			if (d->num_children > halfsize) {
				move_item(d, d->num_children - 1, c, 0);
				memcpy(PAGE_KEY(p, s), PAGE_KEY(c, 0),
				       def->key_size);
				return 0;
			}

			merge_pages(d, c);
			delete_item(p, s);
			free(c);
		} else {
			/* Borrow/merge from higher page */
			struct btree_page *d = *PAGE_PTR(p, s + 1);

			if (d->num_children > halfsize) {
				move_item(d, 0, c, c->num_children);
				memcpy(PAGE_KEY(p, s + 1),
				       PAGE_KEY(d, 0),
				       def->key_size);
				return 0;
			}

			merge_pages(c, d);
			delete_item(p, s + 1);
			free(d);
		}

		if (p->num_children >= halfsize)
			return 0;
	}

	/* If the root contains only a single pointer to another page,
	 * shrink the tree. This does not affect the cursor.
	 */
	if (bt->root->height && bt->root->num_children == 1) {
		struct btree_page *old = bt->root;

		bt->root = *PAGE_PTR(old, 0);
		free(old);
	}

	return 0;
}

int btree_get(btree_t bt, const void *key, void *data)
{
	const struct btree_def *def = bt->def;
	struct btree_page *p = bt->root;
	int h;

	check_btree(bt);

	if (!key)
		return btree_select(bt, NULL, BTREE_READ, NULL, data);

	for (h = bt->root->height; h >= 0; h--) {
		int s = find_key_le(p, key);

		if (h) {
			assert (s >= 0 && s < p->num_children);
			p = *PAGE_PTR(p, s);
		} else if (s >= 0 && !def->compare(key, PAGE_KEY(p, s))) {
			memcpy(data, PAGE_DATA(p, s), def->data_size);
			return 0;
		}
	}

	return 1;
}

int btree_select(btree_t bt, const void *key, btree_selmode_t mode,
		 void *key_ret, void *data_ret)
{
	const struct btree_def *def = bt->def;

	check_btree(bt);

	switch (mode) {
	case BTREE_CLEAR:
		bt->slot[0] = -1;
		break;

	case BTREE_READ:
		break;

	case BTREE_EXACT:
	case BTREE_LE:
		if (!trace_path(bt, key, bt->path, bt->slot) &&
		    mode == BTREE_EXACT)
			bt->slot[0] = -1;
		break;

	case BTREE_FIRST:
		cursor_first(bt);
		break;

	case BTREE_NEXT:
		cursor_next(bt);
		break;
	}

	/* Return the data at the cursor */
	if (bt->slot[0] >= 0) {
		if (key_ret)
			memcpy(key_ret,
			       PAGE_KEY(bt->path[0], bt->slot[0]),
			       def->key_size);
		if (data_ret)
			memcpy(data_ret,
			       PAGE_DATA(bt->path[0], bt->slot[0]),
			       def->data_size);
		return 0;
	}

	return 1;
}
