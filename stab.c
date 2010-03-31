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
#include "util.h"
#include "binfile.h"
#include "btree.h"

struct sym_key {
	char name[64];
};

static const struct sym_key sym_key_zero;

static int sym_key_compare(const void *left, const void *right)
{
	return strcmp(((const struct sym_key *)left)->name,
		      ((const struct sym_key *)right)->name);
}

static void sym_key_init(struct sym_key *key, const char *text)
{
	int len = strlen(text);

	if (len >= sizeof(key->name))
		len = sizeof(key->name) - 1;

	memcpy(key->name, text, len);
	key->name[len] = 0;
}

struct addr_key {
	u_int16_t       addr;
	char            name[64];
};

static const struct addr_key addr_key_zero;

static int addr_key_compare(const void *left, const void *right)
{
	const struct addr_key *kl = (const struct addr_key *)left;
	const struct addr_key *kr = (const struct addr_key *)right;

	if (kl->addr < kr->addr)
		return -1;
	if (kl->addr > kr->addr)
		return 1;

	return strcmp(kl->name, kr->name);
}

static void addr_key_init(struct addr_key *key, u_int16_t addr,
			  const char *text)
{
	int len = strlen(text);

	if (len >= sizeof(key->name))
		len = sizeof(key->name) - 1;

	key->addr = addr;
	memcpy(key->name, text, len);
	key->name[len] = 0;
}

static const struct btree_def sym_table_def = {
	.compare = sym_key_compare,
	.zero = &sym_key_zero,
	.branches = 32,
	.key_size = sizeof(struct sym_key),
	.data_size = sizeof(u_int16_t)
};

static const struct btree_def addr_table_def = {
	.compare = addr_key_compare,
	.zero = &addr_key_zero,
	.branches = 32,
	.key_size = sizeof(struct addr_key),
	.data_size = 0
};

static btree_t sym_table;
static btree_t addr_table;

static int cmd_eval(char **arg)
{
	int addr;
	u_int16_t offset;
	char name[64];

	if (addr_exp(*arg, &addr) < 0) {
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

static struct command command_eval = {
	.name = "=",
	.func = cmd_eval,
	.help =
	"= <expression>\n"
	"    Evaluate an expression using the symbol table.\n"
};

typedef int (*stab_callback_t)(const char *name, u_int16_t value);

void stab_clear(void)
{
	btree_clear(sym_table);
	btree_clear(addr_table);
}

int stab_set(const char *name, int value)
{
	struct sym_key skey;
	struct addr_key akey;
	u_int16_t addr = value;
	u_int16_t old_addr;

	sym_key_init(&skey, name);

	/* Look for an old address first, and delete the reverse mapping
	 * if it's there.
	 */
	if (!btree_get(sym_table, &skey, &old_addr)) {
		addr_key_init(&akey, old_addr, skey.name);
		btree_delete(addr_table, &akey);
	}

	/* Put the new mapping into both tables */
	addr_key_init(&akey, addr, name);
	if (btree_put(addr_table, &akey, NULL) < 0 ||
	    btree_put(sym_table, &skey, &addr) < 0) {
		fprintf(stderr, "stab: can't set %s = 0x%04x\n", name, addr);
		return -1;
	}

	return 0;
}

int stab_nearest(u_int16_t addr, char *ret_name, int max_len,
		 u_int16_t *ret_offset)
{
	struct addr_key akey;
	int i;

	akey.addr = addr;
	for (i = 0; i < sizeof(akey.name); i++)
		akey.name[i] = 0xff;
	akey.name[sizeof(akey.name) - 1] = 0xff;

	if (!btree_select(addr_table, &akey, BTREE_LE, &akey, NULL)) {
		strncpy(ret_name, akey.name, max_len);
		ret_name[max_len - 1] = 0;
		*ret_offset = addr - akey.addr;
		return 0;
	}

	return -1;
}

static int cmd_sym_load_add(int clear, char **arg)
{
	FILE *in;
	int result = 0;

	if (clear && modify_prompt(MODIFY_SYMS))
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
		modify_clear(MODIFY_SYMS);
	else
		modify_set(MODIFY_SYMS);

	return result;
}

static int cmd_sym_savemap(char **arg)
{
	char *fname = get_arg(arg);
	int ret;
	struct addr_key akey;
	FILE *savemap_out;

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

	ret = btree_select(addr_table, NULL, BTREE_FIRST,
			   &akey, NULL);
	while (!ret) {
		fprintf(savemap_out, "0x%04x: %s\n", akey.addr, akey.name);
		ret = btree_select(addr_table, NULL, BTREE_NEXT,
				   &akey, NULL);
	}

	if (fclose(savemap_out) < 0) {
		fprintf(stderr, "sym: error closing %s: %s\n", fname,
			strerror(errno));
		return -1;
	}

	modify_clear(MODIFY_SYMS);
	return 0;
}

static int cmd_sym_find(char **arg)
{
	char *expr = get_arg(arg);
	int ret;
	struct sym_key skey;
	u_int16_t value;
	regex_t preg;

	if (!expr) {
		struct addr_key akey;

		ret = btree_select(addr_table, NULL, BTREE_FIRST,
				   &akey, NULL);
		while (!ret) {
			printf("0x%04x: %s\n", akey.addr, akey.name);
			ret = btree_select(addr_table, NULL, BTREE_NEXT,
					   &akey, NULL);
		}

		return 0;
	}

	if (regcomp(&preg, expr, REG_EXTENDED | REG_NOSUB)) {
		fprintf(stderr, "stab: failed to compile: %s\n", expr);
		return -1;
	}

	ret = btree_select(sym_table, NULL, BTREE_FIRST, &skey, &value);
	while (!ret) {
		if (!regexec(&preg, skey.name, 0, NULL, 0))
			printf("0x%04x: %s\n", value, skey.name);
		ret = btree_select(sym_table, NULL, BTREE_NEXT, &skey, &value);
	}

	regfree(&preg);
	return 0;
}

struct rename_record {
	char    old_name[64];
	int     start, end;
};

struct rename_record *renames;
static int renames_cap;
static int renames_len;

static void renames_free(void)
{
	if (!renames)
		return;

	free(renames);
	renames = NULL;
	renames_cap = 0;
	renames_len = 0;
}

static int renames_push(const char *old_name,
			int start, int end)
{
	struct rename_record *r;

	if (renames_len + 1 > renames_cap) {
		int new_cap = renames_cap * 2;
		struct rename_record *new_array;

		if (!new_cap)
			new_cap = 64;

		new_array = realloc(renames, new_cap * sizeof(renames[0]));
		if (!new_array) {
			perror("sym: can't allocate memory for renaming");
			return -1;
		}

		renames = new_array;
		renames_cap = new_cap;
	}

	r = &renames[renames_len++];
	strncpy(r->old_name, old_name, sizeof(r->old_name));
	r->old_name[sizeof(r->old_name) - 1] = 0;
	r->start = start;
	r->end = end;

	return 0;
}

static int renames_do(const char *replace)
{
	int i;
	int count = 0;

	for (i = 0; i < renames_len; i++) {
		struct rename_record *r = &renames[i];
		char new_name[128];
		int len = r->start;
		struct sym_key skey;
		struct addr_key akey;
		u_int16_t addr;
		u_int16_t new_addr;

		if (len + 1 > sizeof(new_name))
			len = sizeof(new_name) - 1;

		memcpy(new_name, r->old_name, len);
		snprintf(new_name + len,
			 sizeof(new_name) - len,
			 "%s%s", replace, r->old_name + r->end);

		printf("%s -> %s\n", r->old_name, new_name);

		/* Step 1: delete old symbol, record address */
		sym_key_init(&skey, r->old_name);
		if (!btree_get(sym_table, &skey, &new_addr)) {
			addr_key_init(&akey, new_addr, r->old_name);
			if (btree_delete(addr_table, &akey))
				fprintf(stderr, "sym: warning: "
					"no reverse symbol: %s\n",
					r->old_name);
		} else {
			fprintf(stderr, "sym: warning: "
				"no forward symbol: %s\n",
				r->old_name);
		}
		btree_delete(sym_table, &skey);

		/* Step 2: overwrite new name */
		sym_key_init(&skey, new_name);
		if (!btree_get(sym_table, &skey, &addr)) {
			addr_key_init(&akey, addr, new_name);
			btree_delete(addr_table, &akey);
		}

		if (btree_put(sym_table, &skey, &new_addr) < 0)
			return -1;

		addr_key_init(&akey, new_addr, new_name);
		if (btree_put(addr_table, &akey, NULL) < 0)
			return -1;

		count++;
	}

	if (count)
		modify_set(MODIFY_SYMS);

	printf("%d symbols renamed\n", count);
	return 0;
}

static int cmd_sym_rename(char **arg)
{
	const char *expr = get_arg(arg);
	const char *replace = get_arg(arg);
	regex_t preg;
	struct addr_key akey;
	int ret;

	if (!(expr && replace)) {
		fprintf(stderr, "sym: expected pattern and replacement\n");
		return -1;
	}

	if (regcomp(&preg, expr, REG_EXTENDED)) {
		fprintf(stderr, "sym: failed to compile: %s\n", expr);
		return -1;
	}

	ret = btree_select(addr_table, NULL, BTREE_FIRST, &akey, NULL);
	while (!ret) {
		regmatch_t pmatch;

		if (!regexec(&preg, akey.name, 1, &pmatch, 0) &&
		    pmatch.rm_so >= 0 && pmatch.rm_eo > pmatch.rm_so &&
		    renames_push(akey.name, pmatch.rm_so,
				 pmatch.rm_eo) < 0)
			goto fail;

		ret = btree_select(addr_table, NULL, BTREE_NEXT,
				   &akey, NULL);
	}

	regfree(&preg);
	ret = renames_do(replace);
	renames_free();
	return ret;

 fail:
	regfree(&preg);
	renames_free();
	return -1;
}

static int cmd_sym_del(char **arg)
{
	char *name = get_arg(arg);
	struct sym_key skey;
	u_int16_t value;
	struct addr_key akey;

	if (!name) {
		fprintf(stderr, "sym: need a name to delete "
			"symbol table entries\n");
		return -1;
	}

	sym_key_init(&skey, name);
	if (btree_get(sym_table, &skey, &value)) {
		fprintf(stderr, "sym: can't delete nonexistent symbol: %s\n",
			name);
		return -1;
	}

	addr_key_init(&akey, value, name);
	btree_delete(sym_table, &skey);
	btree_delete(addr_table, &akey);

	modify_set(MODIFY_SYMS);
	return 0;
}

static int cmd_sym(char **arg)
{
	char *subcmd = get_arg(arg);

	if (!subcmd) {
		fprintf(stderr, "sym: need to specify a subcommand "
			"(try \"help sym\")\n");
		return -1;
	}

	if (!strcasecmp(subcmd, "clear")) {
		if (modify_prompt(MODIFY_SYMS))
			return 0;
		stab_clear();
		modify_clear(MODIFY_SYMS);
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

		if (addr_exp(val_text, &value) < 0) {
			fprintf(stderr, "sym: can't parse value: %s\n",
				val_text);
			return -1;
		}

		if (stab_set(name, value) < 0)
			return -1;

		modify_set(MODIFY_SYMS);
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

	fprintf(stderr, "sym: unknown subcommand: %s\n", subcmd);
	return -1;
}

static struct command command_sym = {
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
};

static int lookup_token(const char *name, int *value)
{
	struct sym_key skey;
	u_int16_t addr;

	sym_key_init(&skey, name);
	if (btree_get(sym_table, &skey, &addr))
		return -1;

	*value = addr;
	return 0;
}

int stab_init(void)
{
	sym_table = btree_alloc(&sym_table_def);
	if (!sym_table) {
		fprintf(stderr, "stab: failed to allocate symbol table\n");
		return -1;
	}

	addr_table = btree_alloc(&addr_table_def);
	if (!addr_table) {
		fprintf(stderr, "stab: failed to allocate address table\n");
		btree_free(sym_table);
		return -1;
	}

	set_token_func(lookup_token);
	register_command(&command_eval);
	register_command(&command_sym);

	return 0;
}

void stab_exit(void)
{
	btree_free(sym_table);
	btree_free(addr_table);
}
