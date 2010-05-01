/* MSPDebug - debugging tool for the eZ430
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <regex.h>
#include <errno.h>
#include "stab.h"
#include "expr.h"
#include "binfile.h"
#include "util.h"
#include "vector.h"
#include "sym.h"

static int cmd_eval(cproc_t cp, char **arg)
{
	int addr;
	u_int16_t offset;
	char name[64];

	if (expr_eval(*arg, &addr) < 0) {
		fprintf(stderr, "=: can't parse: %s\n", *arg);
		return -1;
	}

	printf("0x%04x", addr);
	if (!stab_nearest(addr, name, sizeof(name), &offset)) {
		printf(" = %s", name);
		if (offset)
			printf("+0x%x", offset);
	}
	printf("\n");

	return 0;
}

static int cmd_sym_load_add(cproc_t cp, int clear, char **arg)
{
	FILE *in;
	int result = 0;

	if (clear && cproc_prompt_abort(cp, CPROC_MODIFY_SYMS))
		return 0;

	in = fopen(*arg, "r");
	if (!in) {
		fprintf(stderr, "sym: %s: %s\n", *arg, strerror(errno));
		return -1;
	}

	if (clear)
		stab_clear();

	if (elf32_check(in))
		result = elf32_syms(in, stab_set);
	else if (symmap_check(in))
		result = symmap_syms(in, stab_set);
	else
		fprintf(stderr, "sym: %s: unknown file type\n", *arg);

	fclose(in);

	if (clear)
		cproc_unmodify(cp, CPROC_MODIFY_SYMS);
	else
		cproc_modify(cp, CPROC_MODIFY_SYMS);

	return result;
}

static int savemap_cb(void *user_data, const char *name, u_int16_t value)
{
	FILE *savemap_out = (FILE *)user_data;

	if (fprintf(savemap_out, "%04x t %s\n", value, name) < 0) {
		perror("sym: can't write to file");
		return -1;
	}

	return 0;
}

static int cmd_sym_savemap(cproc_t cp, char **arg)
{
	FILE *savemap_out;
	char *fname = get_arg(arg);

	if (!fname) {
		fprintf(stderr, "sym: filename required to save map\n");
		return -1;
	}

	savemap_out = fopen(fname, "w");
	if (!savemap_out) {
		fprintf(stderr, "sym: couldn't write to %s: %s\n", fname,
			strerror(errno));
		return -1;
	}

	if (stab_enum(savemap_cb, savemap_out) < 0) {
		fclose(savemap_out);
		return -1;
	}

	if (fclose(savemap_out) < 0) {
		fprintf(stderr, "sym: error on close: %s\n", strerror(errno));
		return -1;
	}

	cproc_unmodify(cp, CPROC_MODIFY_SYMS);
	return 0;
}

static int print_sym(void *user_data, const char *name, u_int16_t value)
{
	printf("0x%04x: %s\n", value, name);
	return 0;
}

static int find_sym(void *user_data, const char *name, u_int16_t value)
{
	regex_t *find_preg = (regex_t *)user_data;

	if (!regexec(find_preg, name, 0, NULL, 0))
		printf("0x%04x: %s\n", value, name);

	return 0;
}

static int cmd_sym_find(cproc_t cp, char **arg)
{
	regex_t find_preg;
	char *expr = get_arg(arg);

	if (!expr) {
		stab_enum(print_sym, NULL);
		return 0;
	}

	if (regcomp(&find_preg, expr, REG_EXTENDED | REG_NOSUB)) {
		fprintf(stderr, "sym: failed to compile: %s\n", expr);
		return -1;
	}

	stab_enum(find_sym, &find_preg);
	regfree(&find_preg);
	return 0;
}

struct rename_record {
	char    old_name[64];
	int     start, end;
};

struct rename_data {
	struct vector   list;
	regex_t         preg;
};

static int renames_do(struct rename_data *rename, const char *replace)
{
	int i;
	int count = 0;

	for (i = 0; i < rename->list.size; i++) {
		struct rename_record *r =
			VECTOR_PTR(rename->list, i, struct rename_record);
		char new_name[128];
		int len = r->start;
		int value;

		if (len + 1 > sizeof(new_name))
			len = sizeof(new_name) - 1;

		memcpy(new_name, r->old_name, len);
		snprintf(new_name + len,
			 sizeof(new_name) - len,
			 "%s%s", replace, r->old_name + r->end);

		printf("%s -> %s\n", r->old_name, new_name);

		if (stab_get(r->old_name, &value) < 0) {
			fprintf(stderr, "sym: warning: "
				"symbol missing: %s\n",
				r->old_name);
		} else {
			stab_del(r->old_name);
			if (stab_set(new_name, value) < 0) {
				fprintf(stderr, "sym: warning: "
					"failed to set new name: %s\n",
					new_name);
			}
		}

		count++;
	}

	printf("%d symbols renamed\n", count);
	return 0;
}

static int find_renames(void *user_data, const char *name, u_int16_t value)
{
	struct rename_data *rename = (struct rename_data *)user_data;
	regmatch_t pmatch;

	if (!regexec(&rename->preg, name, 1, &pmatch, 0) &&
	    pmatch.rm_so >= 0 && pmatch.rm_eo > pmatch.rm_so) {
		struct rename_record r;

		strncpy(r.old_name, name, sizeof(r.old_name));
		r.old_name[sizeof(r.old_name) - 1] = 0;
		r.start = pmatch.rm_so;
		r.end = pmatch.rm_eo;

		return vector_push(&rename->list, &r, 1);
	}

	return 0;
}

static int cmd_sym_rename(cproc_t cp, char **arg)
{
	const char *expr = get_arg(arg);
	const char *replace = get_arg(arg);
	int ret;
	struct rename_data rename;

	if (!(expr && replace)) {
		fprintf(stderr, "sym: expected pattern and replacement\n");
		return -1;
	}

	if (regcomp(&rename.preg, expr, REG_EXTENDED)) {
		fprintf(stderr, "sym: failed to compile: %s\n", expr);
		return -1;
	}

	vector_init(&rename.list, sizeof(struct rename_record));

	if (stab_enum(find_renames, &rename) < 0) {
		fprintf(stderr, "sym: rename failed\n");
		regfree(&rename.preg);
		vector_destroy(&rename.list);
		return -1;
	}

	regfree(&rename.preg);
	ret = renames_do(&rename, replace);
	vector_destroy(&rename.list);

	if (ret > 0)
		cproc_modify(cp, CPROC_MODIFY_SYMS);

	return ret >= 0 ? 0 : -1;
}

static int cmd_sym_del(cproc_t cp, char **arg)
{
	char *name = get_arg(arg);

	if (!name) {
		fprintf(stderr, "sym: need a name to delete "
			"symbol table entries\n");
		return -1;
	}

	if (stab_del(name) < 0) {
		fprintf(stderr, "sym: can't delete nonexistent symbol: %s\n",
			name);
		return -1;
	}

	cproc_modify(cp, CPROC_MODIFY_SYMS);
	return 0;
}

static int cmd_sym(cproc_t cp, char **arg)
{
	char *subcmd = get_arg(arg);

	if (!subcmd) {
		fprintf(stderr, "sym: need to specify a subcommand "
			"(try \"help sym\")\n");
		return -1;
	}

	if (!strcasecmp(subcmd, "clear")) {
		if (cproc_prompt_abort(cp, CPROC_MODIFY_SYMS))
			return 0;
		stab_clear();
		cproc_unmodify(cp, CPROC_MODIFY_SYMS);
		return 0;
	}

	if (!strcasecmp(subcmd, "set")) {
		char *name = get_arg(arg);
		char *val_text = get_arg(arg);
		int value;

		if (!(name && val_text)) {
			fprintf(stderr, "sym: need a name and value to set "
				"symbol table entries\n");
			return -1;
		}

		if (expr_eval(val_text, &value) < 0) {
			fprintf(stderr, "sym: can't parse value: %s\n",
				val_text);
			return -1;
		}

		if (stab_set(name, value) < 0)
			return -1;

		cproc_modify(cp, CPROC_MODIFY_SYMS);
		return 0;
	}

	if (!strcasecmp(subcmd, "del"))
		return cmd_sym_del(cp, arg);
	if (!strcasecmp(subcmd, "import"))
		return cmd_sym_load_add(cp, 1, arg);
	if (!strcasecmp(subcmd, "import+"))
		return cmd_sym_load_add(cp, 0, arg);
	if (!strcasecmp(subcmd, "export"))
		return cmd_sym_savemap(cp, arg);
	if (!strcasecmp(subcmd, "rename"))
		return cmd_sym_rename(cp, arg);
	if (!strcasecmp(subcmd, "find"))
		return cmd_sym_find(cp, arg);

	fprintf(stderr, "sym: unknown subcommand: %s\n", subcmd);
	return -1;
}

static const struct cproc_command commands[] = {
	{
		.name = "=",
		.func = cmd_eval,
		.help =
"= <expression>\n"
"    Evaluate an expression using the symbol table.\n"
	},
	{
		.name = "sym",
		.func = cmd_sym,
		.help =
"sym clear\n"
"    Clear the symbol table.\n"
"sym set <name> <value>\n"
"    Set or overwrite the value of a symbol.\n"
"sym del <name>\n"
"    Delete a symbol from the symbol table.\n"
"sym import <filename>\n"
"    Load symbols from the given file.\n"
"sym import+ <filename>\n"
"    Load additional symbols from the given file.\n"
"sym export <filename>\n"
"    Save the current symbols to a BSD-style symbol file.\n"
"sym find <regex>\n"
"    Search for symbols by regular expression.\n"
"sym rename <regex> <string>\n"
"    Replace every occurance of a pattern in symbol names.\n"
	}
};

int sym_register(cproc_t cp)
{
	return cproc_register_commands(cp, commands, ARRAY_LEN(commands));
}
