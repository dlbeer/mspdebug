/* MSPDebug - debugging tool for the eZ430
 * Copyright (C) 2009 Daniel Beer
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

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "stab.h"

static char *strtab;
static int strtab_len;
static int strtab_cap;

struct symbol {
	int		name;
	u_int16_t	addr;
};

struct symvec {
	struct symbol	*syms;
	int		len;
	int		cap;
};

static struct symvec by_name;
static struct symvec by_addr;
static int need_sort;

static void vec_clear(struct symvec *v)
{
	if (v->syms)
		free(v->syms);

	v->syms = NULL;
	v->len = 0;
	v->cap = 0;
}

void stab_clear(void)
{
	if (strtab)
		free(strtab);
	strtab = NULL;
	strtab_len = 0;
	strtab_cap = 0;

	need_sort = 0;
	vec_clear(&by_name);
	vec_clear(&by_addr);
}

int stab_add_string(const char *text, int len)
{
	int cap = strtab_cap;

	if (!text || !len)
		return strtab_len;

	/* Figure out how big the table needs to be after we add this
	 * string.
	 */
	if (!cap)
		cap = 1024;
	while (strtab_len + len + 1 > cap)
		cap *= 2;

	/* Reallocate if necessary */
	if (cap != strtab_cap) {
		char *n = realloc(strtab, cap);

		if (!n) {
			perror("stab: can't allocate memory for string");
			return -1;
		}

		strtab = n;
		strtab_cap = cap;
	}

	/* Copy it in */
	memcpy(strtab + strtab_len, text, len);
	strtab_len += len;
	strtab[strtab_len] = 0;

	return strtab_len;
}

static int vec_push(struct symvec *v, int name, u_int16_t addr)
{
	int cap = v->cap;
	struct symbol *s;

	if (!cap)
		cap = 64;
	while (v->len + 1 > cap)
		cap *= 2;

	if (cap != v->cap) {
		struct symbol *n = realloc(v->syms, cap * sizeof(v->syms[0]));

		if (!n) {
			perror("stab: can't allocate memory for symbol");
			return -1;
		}

		v->syms = n;
		v->cap = cap;
	}

	s = &v->syms[v->len++];
	s->name = name;
	s->addr = addr;

	return 0;
}

int stab_add_symbol(int name, u_int16_t addr)
{
	if (name < 0 || name > strtab_len) {
		fprintf(stderr, "stab: symbol name out of bounds: %d\n",
			name);
		return -1;
	}

	need_sort = 1;
	if (vec_push(&by_name, name, addr) < 0)
		return -1;
	if (vec_push(&by_addr, name, addr) < 0)
		return -1;

	return 0;
}

static int cmp_by_name(const void *a, const void *b)
{
	const struct symbol *sa = (const struct symbol *)a;
	const struct symbol *sb = (const struct symbol *)b;

	return strcmp(strtab + sa->name, strtab + sb->name);
}

static int cmp_by_addr(const void *a, const void *b)
{
	const struct symbol *sa = (const struct symbol *)a;
	const struct symbol *sb = (const struct symbol *)b;

	if (sa->addr < sb->addr)
		return -1;
	if (sa->addr > sb->addr)
		return 1;
	return 0;
}

static void sort_tables(void)
{
	if (!need_sort)
		return;
	need_sort = 0;

	qsort(by_name.syms, by_name.len, sizeof(by_name.syms[0]),
	      cmp_by_name);
	qsort(by_addr.syms, by_addr.len, sizeof(by_addr.syms[0]),
	      cmp_by_addr);
}

static char token_buf[64];
static int token_len;
static int token_mult;
static int token_sum;

static void token_add(void)
{
	int low = 0;
	int high = by_name.len - 1;

	if (!token_len)
		return;

	token_buf[token_len] = 0;
	token_len = 0;

	/* Look up the name in the symbol table */
	while (low <= high) {
		int mid = (low + high) / 2;
		struct symbol *sym = &by_name.syms[mid];
		int cmp = strcmp(strtab + sym->name, token_buf);

		if (!cmp) {
			token_sum += token_mult * (int)sym->addr;
			return;
		}

		if (cmp < 0)
			low = mid + 1;
		else
			high = mid - 1;
	}

	/* Not found? Try to parse it the old way */
	token_sum += token_mult * strtol(token_buf, NULL, 16);
}

int stab_parse(const char *text, int *addr)
{
	token_len = 0;
	token_mult = 1;
	token_sum = 0;

	sort_tables();

	while (*text) {
		if (isalnum(*text) || *text == '_' || *text == '$') {
			if (token_len + 1 < sizeof(token_buf))
				token_buf[token_len++] = *text;
		} else {
			token_add();
			if (*text == '+')
				token_mult = 1;
			if (*text == '-')
				token_mult = -1;
		}

		text++;
	}

	token_add();
	*addr = token_sum;
	return 0;
}

int stab_find(u_int16_t *addr, const char **name)
{
	int low = 0;
	int high = by_addr.len - 1;

	sort_tables();

	while (low <= high) {
		int mid = (low + high) / 2;
		struct symbol *sym = &by_addr.syms[mid];

		if (sym->addr > *addr) {
			high = mid - 1;
		} else if (mid + 1 > by_addr.len ||
			   by_addr.syms[mid + 1].addr > *addr) {
			*addr -= sym->addr;
			*name = strtab + sym->name;
			return 0;
		} else {
			low = mid + 1;
		}
	}

	return -1;
}
