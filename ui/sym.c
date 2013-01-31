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
#include <stdint.h>
#include <regex.h>

#include "stab.h"
#include "expr.h"
#include "binfile.h"
#include "util.h"
#include "output.h"
#include "output_util.h"
#include "vector.h"
#include "sym.h"
#include "reader.h"
#include "demangle.h"

int cmd_eval(char **arg)
{
	address_t addr;
	char name[MAX_SYMBOL_LENGTH];

	if (expr_eval(*arg, &addr) < 0) {
		printc_err("=: can't parse: %s\n", *arg);
		return -1;
	}

	print_address(addr, name, sizeof(name), 0);
	printc("0x%05x = %s\n", addr, name);

	return 0;
}

static int cmd_sym_load_add(int clear, char **arg)
{
	FILE *in;
	char * path;

	if (clear && prompt_abort(MODIFY_SYMS))
		return 0;

	path = expand_tilde(*arg);
	if (!path)
		return -1;

	in = fopen(path, "rb");
	free(path);
	if (!in) {
		printc_err("sym: %s: %s\n", *arg, last_error());
		return -1;
	}

	if (clear) {
		stab_clear();
		unmark_modified(MODIFY_SYMS);
	} else {
		mark_modified(MODIFY_SYMS);
	}

	if (binfile_syms(in) < 0) {
		fclose(in);
		return -1;
	}

	fclose(in);

	return 0;
}

static int savemap_cb(void *user_data, const char *name, address_t value)
{
	FILE *savemap_out = (FILE *)user_data;

	if (fprintf(savemap_out, "%04x t %s\n", value, name) < 0) {
		pr_error("sym: can't write to file");
		return -1;
	}

	return 0;
}

static int cmd_sym_savemap(char **arg)
{
	FILE *savemap_out;
	char *fname = get_arg(arg);

	if (!fname) {
		printc_err("sym: filename required to save map\n");
		return -1;
	}

	savemap_out = fopen(fname, "w");
	if (!savemap_out) {
		printc_err("sym: couldn't write to %s: %s\n", fname,
			last_error());
		return -1;
	}

	if (stab_enum(savemap_cb, savemap_out) < 0) {
		fclose(savemap_out);
		return -1;
	}

	if (fclose(savemap_out) < 0) {
		printc_err("sym: error on close: %s\n", last_error());
		return -1;
	}

	unmark_modified(MODIFY_SYMS);
	return 0;
}

static int print_sym(void *user_data, const char *name, address_t value)
{
	(void)user_data;

	char demangled[MAX_SYMBOL_LENGTH];
	if (demangle(name, demangled, sizeof(demangled)) > 0)
		printc("0x%04x: %s (%s)\n", value, name, demangled);
	else
		printc("0x%04x: %s\n", value, name);

	return 0;
}

static int find_sym(void *user_data, const char *name, address_t value)
{
	regex_t *find_preg = (regex_t *)user_data;

	char demangled[MAX_SYMBOL_LENGTH];
	int len = demangle(name, demangled, sizeof(demangled));

	if (!regexec(find_preg, name, 0, NULL, 0) ||
	    (len > 0 && !regexec(find_preg, demangled, 0, NULL, 0))) {
		if (len > 0)
			printc("0x%04x: %s (%s)\n", value, name, demangled);
		else
			printc("0x%04x: %s\n", value, name);
	}

	return 0;
}

static int cmd_sym_find(char **arg)
{
	regex_t find_preg;
	char *expr = get_arg(arg);

	if (!expr) {
		stab_enum(print_sym, NULL);
		return 0;
	}

	if (regcomp(&find_preg, expr, REG_EXTENDED | REG_NOSUB)) {
		printc_err("sym: failed to compile: %s\n", expr);
		return -1;
	}

	stab_enum(find_sym, &find_preg);
	regfree(&find_preg);
	return 0;
}

struct rename_record {
	char    old_name[MAX_SYMBOL_LENGTH];
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
		char new_name[MAX_SYMBOL_LENGTH];
		int len = r->start;
		address_t value;

		if (len + 1 > sizeof(new_name))
			len = sizeof(new_name) - 1;

		memcpy(new_name, r->old_name, len);
		snprintf(new_name + len,
			 sizeof(new_name) - len,
			 "%s%s", replace, r->old_name + r->end);

		printc("%s -> %s\n", r->old_name, new_name);

		if (stab_get(r->old_name, &value) < 0) {
			printc_err("sym: warning: "
				"symbol missing: %s\n",
				r->old_name);
		} else {
			stab_del(r->old_name);
			if (stab_set(new_name, value) < 0) {
				printc_err("sym: warning: "
					"failed to set new name: %s\n",
					new_name);
			}
		}

		count++;
	}

	printc("%d symbols renamed\n", count);
	return 0;
}

static int find_renames(void *user_data, const char *name, address_t value)
{
	struct rename_data *rename = (struct rename_data *)user_data;
	regmatch_t pmatch;

	(void)value;

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

static int cmd_sym_rename(char **arg)
{
	const char *expr = get_arg(arg);
	const char *replace = get_arg(arg);
	int ret;
	struct rename_data rename;

	if (!(expr && replace)) {
		printc_err("sym: expected pattern and replacement\n");
		return -1;
	}

	if (regcomp(&rename.preg, expr, REG_EXTENDED)) {
		printc_err("sym: failed to compile: %s\n", expr);
		return -1;
	}

	vector_init(&rename.list, sizeof(struct rename_record));

	if (stab_enum(find_renames, &rename) < 0) {
		printc_err("sym: rename failed\n");
		regfree(&rename.preg);
		vector_destroy(&rename.list);
		return -1;
	}

	regfree(&rename.preg);
	ret = renames_do(&rename, replace);
	vector_destroy(&rename.list);

	if (ret > 0)
		mark_modified(MODIFY_SYMS);

	return ret >= 0 ? 0 : -1;
}

static int cmd_sym_del(char **arg)
{
	char *name = get_arg(arg);

	if (!name) {
		printc_err("sym: need a name to delete "
			"symbol table entries\n");
		return -1;
	}

	if (stab_del(name) < 0) {
		printc_err("sym: can't delete nonexistent symbol: %s\n",
			name);
		return -1;
	}

	mark_modified(MODIFY_SYMS);
	return 0;
}

int cmd_sym(char **arg)
{
	char *subcmd = get_arg(arg);

	if (!subcmd) {
		printc_err("sym: need to specify a subcommand "
			"(try \"help sym\")\n");
		return -1;
	}

	if (!strcasecmp(subcmd, "clear")) {
		if (prompt_abort(MODIFY_SYMS))
			return 0;
		stab_clear();
		unmark_modified(MODIFY_SYMS);
		return 0;
	}

	if (!strcasecmp(subcmd, "set")) {
		char *name = get_arg(arg);
		char *val_text = get_arg(arg);
		address_t value;

		if (!(name && val_text)) {
			printc_err("sym: need a name and value to set "
				"symbol table entries\n");
			return -1;
		}

		if (expr_eval(val_text, &value) < 0) {
			printc_err("sym: can't parse value: %s\n",
				val_text);
			return -1;
		}

		if (stab_set(name, value) < 0)
			return -1;

		mark_modified(MODIFY_SYMS);
		return 0;
	}

	if (!strcasecmp(subcmd, "del"))
		return cmd_sym_del(arg);
	if (!strcasecmp(subcmd, "import"))
		return cmd_sym_load_add(1, arg);
	if (!strcasecmp(subcmd, "import+"))
		return cmd_sym_load_add(0, arg);
	if (!strcasecmp(subcmd, "export"))
		return cmd_sym_savemap(arg);
	if (!strcasecmp(subcmd, "rename"))
		return cmd_sym_rename(arg);
	if (!strcasecmp(subcmd, "find"))
		return cmd_sym_find(arg);

	printc_err("sym: unknown subcommand: %s\n", subcmd);
	return -1;
}
