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

#include <stdlib.h>
#include <string.h>

#include "elf32.h"
#include "elf_format.h"
#include "output.h"
#include "util.h"

#ifndef EM_MSP430
#define EM_MSP430	0x69
#endif

static const uint8_t elf32_id[] = {
	ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3, ELFCLASS32
};

#define MAX_PHDRS	32
#define MAX_SHDRS	128

struct elf32_info {
	Elf32_Ehdr              file_ehdr;
	Elf32_Phdr              file_phdrs[MAX_PHDRS];
	Elf32_Shdr              file_shdrs[MAX_SHDRS];

	char                    *string_tab;
	int                     string_len;
};

static int parse_ehdr(Elf32_Ehdr *e, FILE *in)
{
	uint8_t data[52];

	if (fread(data, sizeof(data), 1, in) != 1)
		return -1;

	memcpy(e->e_ident, data, EI_NIDENT);
	e->e_type = LE_WORD(data, 16);
	e->e_machine = LE_WORD(data, 18);
	e->e_version = LE_LONG(data, 20);
	e->e_entry = LE_LONG(data, 24);
	e->e_phoff = LE_LONG(data, 28);
	e->e_shoff = LE_LONG(data, 32);
	e->e_flags = LE_LONG(data, 36);
	e->e_ehsize = LE_WORD(data, 40);
	e->e_phentsize = LE_WORD(data, 42);
	e->e_phnum = LE_WORD(data, 44);
	e->e_shentsize = LE_WORD(data, 46);
	e->e_shnum = LE_WORD(data, 48);
	e->e_shstrndx = LE_WORD(data, 50);

	return 0;
}

static int parse_phdr(Elf32_Phdr *p, FILE *in)
{
	uint8_t data[32];

	if (fread(data, sizeof(data), 1, in) != 1)
		return -1;

	p->p_type = LE_LONG(data, 0);
	p->p_offset = LE_LONG(data, 4);
	p->p_vaddr = LE_LONG(data, 8);
	p->p_paddr = LE_LONG(data, 12);
	p->p_filesz = LE_LONG(data, 16);
	p->p_memsz = LE_LONG(data, 20);
	p->p_flags = LE_LONG(data, 24);
	p->p_align = LE_LONG(data, 28);

	return 0;
}

static int parse_shdr(Elf32_Shdr *s, FILE *in)
{
	uint8_t data[40];

	if (fread(data, sizeof(data), 1, in) != 1)
		return -1;

	s->sh_name = LE_LONG(data, 0);
	s->sh_type = LE_LONG(data, 4);
	s->sh_flags = LE_LONG(data, 8);
	s->sh_addr = LE_LONG(data, 12);
	s->sh_offset = LE_LONG(data, 16);
	s->sh_size = LE_LONG(data, 20);
	s->sh_link = LE_LONG(data, 24);
	s->sh_info = LE_LONG(data, 28);
	s->sh_addralign = LE_LONG(data, 32);
	s->sh_entsize = LE_LONG(data, 36);

	return 0;
}

static int parse_sym(Elf32_Sym *s, FILE *in)
{
	uint8_t data[16];

	if (fread(data, sizeof(data), 1, in) != 1)
		return -1;

	s->st_name = LE_LONG(data, 0);
	s->st_value = LE_LONG(data, 4);
	s->st_size = LE_LONG(data, 8);
	s->st_info = data[12];
	s->st_other = data[13];
	s->st_shndx = LE_WORD(data, 14);

	return 0;
}

static int read_ehdr(struct elf32_info *info, FILE *in)
{
	/* Read and check the ELF header */
	rewind(in);
	if (parse_ehdr(&info->file_ehdr, in) < 0) {
		pr_error("elf32: couldn't read ELF header");
		return -1;
	}

	if (memcmp(info->file_ehdr.e_ident, elf32_id, sizeof(elf32_id))) {
		printc_err("elf32: not an ELF32 file\n");
		return -1;
	}

	return 0;
}

static int read_phdr(struct elf32_info *info, FILE *in)
{
	int i;

	if (info->file_ehdr.e_phnum > MAX_PHDRS) {
		printc_err("elf32: too many program headers: %d\n",
			info->file_ehdr.e_phnum);
		return -1;
	}

	for (i = 0; i < info->file_ehdr.e_phnum; i++) {
		if (fseek(in, i * info->file_ehdr.e_phentsize +
			  info->file_ehdr.e_phoff,
			  SEEK_SET) < 0) {
			printc_err("elf32: can't seek to phdr %d\n", i);
			return -1;
		}

		if (parse_phdr(&info->file_phdrs[i], in) < 0) {
			printc_err("elf32: can't read phdr %d: %s\n",
				i, last_error());
			return -1;
		}
	}

	return 0;
}

static int read_shdr(struct elf32_info *info, FILE *in)
{
	int i;

	if (info->file_ehdr.e_shnum > MAX_SHDRS) {
		printc_err("elf32: too many section headers: %d\n",
			info->file_ehdr.e_shnum);
		return -1;
	}

	for (i = 0; i < info->file_ehdr.e_shnum; i++) {
		if (fseek(in, i * info->file_ehdr.e_shentsize +
			  info->file_ehdr.e_shoff,
			  SEEK_SET) < 0) {
			printc_err("elf32: can't seek to shdr %d\n", i);
			return -1;
		}

		if (parse_shdr(&info->file_shdrs[i], in) < 0) {
			printc_err("elf32: can't read shdr %d: %s\n",
				i, last_error());
			return -1;
		}
	}

	return 0;
}

static uint32_t file_to_phys(struct elf32_info *info, uint32_t v)
{
	int i;

	for (i = 0; i < info->file_ehdr.e_phnum; i++) {
		Elf32_Phdr *p = &info->file_phdrs[i];

		if (v >= p->p_offset && v - p->p_offset < p->p_filesz)
			return v - p->p_offset + p->p_paddr;
	}

	return v;
}

static int feed_section(struct elf32_info *info,
			FILE *in, const Elf32_Shdr *sh,
			binfile_imgcb_t cb, void *user_data)
{
	uint32_t offset = sh->sh_offset;
	uint32_t size = sh->sh_size;
	uint8_t buf[1024];
	uint32_t addr = file_to_phys(info, offset);
	const char *name = NULL;

	if (fseek(in, offset, SEEK_SET) < 0) {
		pr_error("elf32: can't seek to section");
		return -1;
	}

	if (info->string_tab &&
	    sh->sh_name < info->string_len)
		name = info->string_tab + sh->sh_name;

	while (size) {
		int ask = size > sizeof(buf) ? sizeof(buf) : size;
		int len = fread(buf, 1, ask, in);
		struct binfile_chunk ch = {0};

		if (len < 0) {
			pr_error("elf32: can't read section");
			return -1;
		}

		ch.name = name;
		ch.addr = addr;
		ch.data = buf;
		ch.len = len;

		if (cb(user_data, &ch) < 0)
			return -1;

		size -= len;
		offset += len;
		addr += len;
	}

	return 0;
}

static int read_all(struct elf32_info *info, FILE *in)
{
	memset(info, 0, sizeof(*info));

	if (read_ehdr(info, in) < 0)
		return -1;

	if (info->file_ehdr.e_machine != EM_MSP430)
		printc_err("elf32: warning: unknown machine type: 0x%x\n",
			info->file_ehdr.e_machine);

	if (read_phdr(info, in) < 0)
		return -1;
	if (read_shdr(info, in) < 0)
		return -1;

	return 0;
}

static int load_strings(struct elf32_info *info, FILE *in, Elf32_Shdr *s)
{
	int len = s->sh_size;

	if (!len)
		return 0;

	if (fseek(in, s->sh_offset, SEEK_SET) < 0) {
		pr_error("elf32: can't seek to strings");
		return -1;
	}

	info->string_len = len;
	info->string_tab = malloc(len + 1);

	if (!info->string_tab) {
		pr_error("elf32: can't allocate string table memory");
		return -1;
	}

	if (!fread(info->string_tab, info->string_len, 1, in)) {
		if (ferror(in)) {
			pr_error("elf32: error reading strings");
			return -1;
		}

		printc_err("elf32: eof reading strings\n");
		return -1;
	}

	info->string_tab[info->string_len] = 0;
	return 0;
}

int elf32_extract(FILE *in, binfile_imgcb_t cb, void *user_data)
{
	struct elf32_info info;
	int i;
	int ret = 0;

	if (read_all(&info, in) < 0)
		return -1;

	if (load_strings(&info, in,
			 &info.file_shdrs[info.file_ehdr.e_shstrndx]) < 0) {
		printc_err("elf32: warning: can't load section string "
			   "table\n");
		info.string_tab = NULL;
	}

	for (i = 0; i < info.file_ehdr.e_shnum; i++) {
		Elf32_Shdr *s = &info.file_shdrs[i];

		if (s->sh_type == SHT_PROGBITS && s->sh_flags & SHF_ALLOC &&
		    feed_section(&info, in, s, cb, user_data) < 0) {
			ret = -1;
			break;
		}
	}

	if (info.string_tab)
		free(info.string_tab);

	return ret;
}

int elf32_check(FILE *in)
{
	int i;

	rewind(in);
	for (i = 0; i < sizeof(elf32_id); i++)
		if (fgetc(in) != elf32_id[i])
			return 0;

	return 1;
}

static Elf32_Shdr *find_shdr(struct elf32_info *info, Elf32_Word type)
{
	int i;

	for (i = 0; i < info->file_ehdr.e_shnum; i++) {
		Elf32_Shdr *s = &info->file_shdrs[i];

		if (s->sh_type == type)
			return s;
	}

	return NULL;
}

#define N_SYMS	128

#ifndef STT_COMMON
#define STT_COMMON 5
#endif

static int syms_load_syms(struct elf32_info *info, FILE *in,
			  Elf32_Shdr *s)
{
	int len = s->sh_size / 16;
	int i;

	if (fseek(in, s->sh_offset, SEEK_SET) < 0) {
		pr_error("elf32: can't seek to symbols");
		return -1;
	}

	for (i = 0; i < len; i++) {
		Elf32_Sym y;
		int st;
		const char *name;

		if (parse_sym(&y, in) < 0) {
			pr_error("elf32: error reading symbols");
			return -1;
		}

		st = ELF32_ST_TYPE(y.st_info);
		name = info->string_tab + y.st_name;

		if (y.st_name > info->string_len) {
			printc_err("elf32: symbol out of bounds\n");
			return -1;
		}

		if (name[0] &&
		    (st == STT_OBJECT || st == STT_FUNC ||
		     st == STT_SECTION || st == STT_COMMON ||
		     st == STT_TLS) &&
		    stab_set(info->string_tab + y.st_name,
			     y.st_value) < 0)
			return -1;
	}

	return 0;
}

int elf32_syms(FILE *in)
{
	struct elf32_info info;
	Elf32_Shdr *s;
	int ret = 0;

	if (read_all(&info, in) < 0)
		return -1;

	s = find_shdr(&info, SHT_SYMTAB);
	if (!s) {
		printc_err("elf32: no symbol table\n");
		return -1;
	}

	if (s->sh_link <= 0 || s->sh_link >= info.file_ehdr.e_shnum) {
		printc_err("elf32: no string table\n");
		return -1;
	}

	if (load_strings(&info, in, &info.file_shdrs[s->sh_link]) < 0 ||
	    syms_load_syms(&info, in, s) < 0)
		ret = -1;

	if (info.string_tab)
		free(info.string_tab);

	return ret;
}
