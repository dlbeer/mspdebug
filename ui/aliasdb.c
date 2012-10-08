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
#include <stdio.h>
#include <string.h>

#include "aliasdb.h"
#include "vector.h"
#include "output.h"
#include "util.h"

struct alias {
	char	src[256];
	char	dst[256];
};

static struct vector alias_list = {
	.ptr = NULL,
	.capacity = 0,
	.size = 0,
	.elemsize = sizeof(struct alias)
};

static int list_is_sorted;

static struct alias *find_alias(const char *name)
{
	int i;

	for (i = 0; i < alias_list.size; i++) {
		struct alias *a =
			VECTOR_PTR(alias_list, i, struct alias);

		if (!strcasecmp(name, a->src))
			return a;
	}

	return NULL;
}

struct recurse_list {
	const char		*cmd;
	struct recurse_list	*next;
};

static int translate_rec(struct recurse_list *l,
			 const char *command, const char *args,
			 char *out_cmd, int max_len)
{
	struct recurse_list *c;
	const struct alias *a;

	if (*command == '\\') {
		snprintf(out_cmd, max_len, "%s %s", command + 1, args);
		return 0;
	}

	for (c = l; c; c = c->next)
		if (!strcasecmp(c->cmd, command)) {
			printc_err("recursive alias: %s\n", command);
			return -1;
		}

	a = find_alias(command);
	if (a) {
		struct recurse_list r;
		char tmp_buf[1024];
		char *new_args = tmp_buf;
		char *cmd;

		snprintf(tmp_buf, sizeof(tmp_buf), "%s %s", a->dst, args);
		r.next = l;
		r.cmd = command;

		cmd = get_arg(&new_args);
		return translate_rec(&r, cmd, new_args, out_cmd, max_len);
	}

	snprintf(out_cmd, max_len, "%s %s", command, args);
	return 0;
}

int translate_alias(const char *command, const char *args,
		    char *out_cmd, int max_len)
{
	return translate_rec(NULL, command, args, out_cmd, max_len);
}

static int cmp_alias(const void *a, const void *b)
{
	const struct alias *aa = (const struct alias *)a;
	const struct alias *ab = (const struct alias *)b;

	return strcasecmp(aa->src, ab->src);
}

int cmd_alias(char **arg)
{
	const char *src = get_arg(arg);
	const char *dst = get_arg(arg);
	struct alias *a;
	struct alias na;

	if (!src) { /* List aliases */
		int i;

		if (!list_is_sorted) {
			qsort(alias_list.ptr, alias_list.size,
			      alias_list.elemsize, cmp_alias);
			list_is_sorted = 1;
		}

		printc("%d aliases defined:\n", alias_list.size);
		for (i = 0; i < alias_list.size; i++) {
			struct alias *a =
				VECTOR_PTR(alias_list, i, struct alias);

			printc("    %20s = %s\n", a->src, a->dst);
		}

		return 0;
	}

	a = find_alias(src);

	if (!dst) { /* Delete alias */
		struct alias *end = VECTOR_PTR(alias_list,
			alias_list.size - 1, struct alias);

		if (!a) {
			printc_err("alias: no such alias defined: %s\n", src);
			return -1;
		}

		if (end != a)
			memcpy(a, end, sizeof(*a));

		vector_pop(&alias_list);
		list_is_sorted = 0;
		return 0;
	}

	if (a) { /* Overwrite old alias */
		strncpy(a->dst, dst, sizeof(a->dst));
		a->dst[sizeof(a->dst) - 1] = 0;
		return 0;
	}

	/* New alias */
	strncpy(na.src, src, sizeof(na.src));
	na.src[sizeof(na.src) - 1] = 0;
	strncpy(na.dst, dst, sizeof(na.dst));
	na.dst[sizeof(na.dst) - 1] = 0;

	if (vector_push(&alias_list, &na, 1) < 0) {
		printc_err("alias: can't allocate memory: %s\n",
			   last_error());
		return -1;
	}

	list_is_sorted = 0;
	return 0;
}
