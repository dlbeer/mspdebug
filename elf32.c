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
#include <errno.h>
#include <string.h>
#ifdef __APPLE__
#include <libelf.h>
#else
#include <elf.h>
#endif
#include "binfile.h"

#ifndef EM_MSP430
#define EM_MSP430	0x69
#endif

static const u_int8_t elf32_id[] = {
	ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3, ELFCLASS32
};

#define MAX_PHDRS	32
#define MAX_SHDRS	32

struct elf32_info {
	Elf32_Ehdr              file_ehdr;
	Elf32_Phdr              file_phdrs[MAX_PHDRS];
	Elf32_Shdr              file_shdrs[MAX_SHDRS];

	char                    *string_tab;
	int                     string_len;
};

static int read_ehdr(struct elf32_info *info, FILE *in)
{
	/* Read and check the ELF header */
	rewind(in);
	if (fread(&info->file_ehdr, sizeof(info->file_ehdr), 1, in) < 0) {
		perror("elf32: couldn't read ELF header");
		return -1;
	}

	if (memcmp(info->file_ehdr.e_ident, elf32_id, sizeof(elf32_id))) {
		fprintf(stderr, "elf32: not an ELF32 file\n");
		return -1;
	}

	return 0;
}

static int read_phdr(struct elf32_info *info, FILE *in)
{
	int i;

	if (info->file_ehdr.e_phnum > MAX_PHDRS) {
		fprintf(stderr, "elf32: too many program headers: %d\n",
			info->file_ehdr.e_phnum);
		return -1;
	}

	for (i = 0; i < info->file_ehdr.e_phnum; i++) {
		if (fseek(in, i * info->file_ehdr.e_phentsize +
			  info->file_ehdr.e_phoff,
			  SEEK_SET) < 0) {
			fprintf(stderr, "elf32: can't seek to phdr %d\n", i);
			return -1;
		}

		if (fread(&info->file_phdrs[i],
			  sizeof(info->file_phdrs[0]), 1, in) < 0) {
			fprintf(stderr, "elf32: can't read phdr %d: %s\n",
				i, strerror(errno));
			return -1;
		}
	}

	return 0;
}

static int read_shdr(struct elf32_info *info, FILE *in)
{
	int i;

	if (info->file_ehdr.e_shnum > MAX_SHDRS) {
		fprintf(stderr, "elf32: too many section headers: %d\n",
			info->file_ehdr.e_shnum);
		return -1;
	}

	for (i = 0; i < info->file_ehdr.e_shnum; i++) {
		if (fseek(in, i * info->file_ehdr.e_shentsize +
			  info->file_ehdr.e_shoff,
			  SEEK_SET) < 0) {
			fprintf(stderr, "elf32: can't seek to shdr %d\n", i);
			return -1;
		}

		if (fread(&info->file_shdrs[i],
			  sizeof(info->file_shdrs[0]), 1, in) < 0) {
			fprintf(stderr, "elf32: can't read shdr %d: %s\n",
				i, strerror(errno));
			return -1;
		}
	}

	return 0;
}

static u_int32_t file_to_phys(struct elf32_info *info, u_int32_t v)
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
			FILE *in, int offset, int size, imgfunc_t cb,
			void *user_data)
{
	u_int8_t buf[1024];
	u_int16_t addr = file_to_phys(info, offset);

	if (fseek(in, offset, SEEK_SET) < 0) {
		perror("elf32: can't seek to section");
		return -1;
	}

	while (size) {
		int ask = size > sizeof(buf) ? sizeof(buf) : size;
		int len = fread(buf, 1, ask, in);

		if (len < 0) {
			perror("elf32: can't read section");
			return -1;
		}

		if (cb(user_data, addr, buf, len) < 0)
			return -1;

		size -= len;
		offset += len;
		addr += len;
	}

	return 0;
}

static int read_all(struct elf32_info *info, FILE *in)
{
	memset(info, 0, sizeof(info));

	if (read_ehdr(info, in) < 0)
		return -1;
	if (info->file_ehdr.e_machine != EM_MSP430) {
		fprintf(stderr, "elf32: this is not an MSP430 ELF32\n");
		return -1;
	}

	if (read_phdr(info, in) < 0)
		return -1;
	if (read_shdr(info, in) < 0)
		return -1;

	return 0;
}

int elf32_extract(FILE *in, imgfunc_t cb, void *user_data)
{
	struct elf32_info info;
	int i;

	if (read_all(&info, in) < 0)
		return -1;

	for (i = 0; i < info.file_ehdr.e_shnum; i++) {
		Elf32_Shdr *s = &info.file_shdrs[i];

		if (s->sh_type == SHT_PROGBITS && s->sh_flags & SHF_ALLOC &&
		    feed_section(&info, in, s->sh_offset, s->sh_size,
				 cb, user_data) < 0)
			return -1;
	}

	return 0;
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

static int syms_load_strings(struct elf32_info *info, FILE *in, Elf32_Shdr *s)
{
	int len = s->sh_size;

	if (!len)
		return 0;

	if (fseek(in, s->sh_offset, SEEK_SET) < 0) {
		perror("elf32: can't seek to strings");
		return -1;
	}

	info->string_len = len;
	info->string_tab = malloc(len + 1);

	if (!info->string_tab) {
		perror("elf32: can't allocate string table memory");
		return -1;
	}

	if (!fread(info->string_tab, 1, info->string_len, in)) {
		if (ferror(in)) {
			perror("elf32: error reading strings");
			return -1;
		}

		fprintf(stderr, "elf32: eof reading strings\n");
		return -1;
	}

	info->string_tab[info->string_len] = 0;
	return 0;
}

#define N_SYMS	128

static int syms_load_syms(struct elf32_info *info, FILE *in,
			  Elf32_Shdr *s, stab_t stab)
{
	Elf32_Sym syms[N_SYMS];
	int len = s->sh_size / sizeof(syms[0]);

	if (fseek(in, s->sh_offset, SEEK_SET) < 0) {
		perror("elf32: can't seek to symbols");
		return -1;
	}

	while (len) {
		int req = N_SYMS > len ? len : N_SYMS;
		int count = fread(syms, sizeof(syms[0]), req, in);
		int i;

		if (!count) {
			fprintf(stderr, "elf32: eof reading symbols\n");
			return -1;
		}

		if (count < 0) {
			perror("elf32: error reading symbols");
			return -1;
		}

		for (i = 0; i < count; i++) {
			Elf32_Sym *y = &syms[i];

			if (y->st_name > info->string_len) {
				fprintf(stderr, "elf32: symbol out of "
					"bounds\n");
				return -1;
			}

			if (stab_set(stab, info->string_tab + y->st_name,
				     y->st_value) < 0)
				return -1;
		}

		len -= count;
	}

	return 0;
}

int elf32_syms(FILE *in, stab_t stab)
{
	struct elf32_info info;
	Elf32_Shdr *s;
	int ret = 0;

	if (read_all(&info, in) < 0)
		return -1;

	s = find_shdr(&info, SHT_SYMTAB);
	if (!s) {
		fprintf(stderr, "elf32: no symbol table\n");
		return -1;
	}

	if (s->sh_link <= 0 || s->sh_link >= info.file_ehdr.e_shnum) {
		fprintf(stderr, "elf32: no string table\n");
		return -1;
	}

	if (syms_load_strings(&info, in, &info.file_shdrs[s->sh_link]) < 0 ||
	    syms_load_syms(&info, in, s, stab) < 0)
		ret = -1;

	if (info.string_tab)
		free(info.string_tab);

	return ret;
}
