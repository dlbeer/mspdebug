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

#include <errno.h>
#include <string.h>
#include <elf.h>
#include "binfile.h"
#include "stab.h"

#define EM_MSP430	0x69

static const u_int8_t elf32_id[] = {
	ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3, ELFCLASS32
};

#define MAX_PHDRS	32
#define MAX_SHDRS	32

static Elf32_Ehdr file_ehdr;
static Elf32_Phdr file_phdrs[MAX_PHDRS];
static Elf32_Shdr file_shdrs[MAX_SHDRS];

static int read_ehdr(FILE *in)
{
	int i;

	/* Read and check the ELF header */
	rewind(in);
	if (fread(&file_ehdr, sizeof(file_ehdr), 1, in) < 0) {
		perror("elf32: couldn't read ELF header");
		return -1;
	}

	for (i = 0; i < sizeof(elf32_id); i++)
		if (file_ehdr.e_ident[i] != elf32_id[i]) {
			fprintf(stderr, "elf32: not an ELF32 file\n");
			return -1;
		}

	return 0;
}

static int read_phdr(FILE *in)
{
	int i;

	if (file_ehdr.e_phnum > MAX_PHDRS) {
		fprintf(stderr, "elf32: too many program headers: %d\n",
			file_ehdr.e_phnum);
		return -1;
	}

	for (i = 0; i < file_ehdr.e_phnum; i++) {
		if (fseek(in, i * file_ehdr.e_phentsize + file_ehdr.e_phoff,
			  SEEK_SET) < 0) {
			fprintf(stderr, "elf32: can't seek to phdr %d\n", i);
			return -1;
		}

		if (fread(&file_phdrs[i], sizeof(file_phdrs[0]), 1, in) < 0) {
			fprintf(stderr, "elf32: can't read phdr %d: %s\n",
				i, strerror(errno));
			return -1;
		}
	}

	return 0;
}

static int read_shdr(FILE *in)
{
	int i;

	if (file_ehdr.e_shnum > MAX_SHDRS) {
		fprintf(stderr, "elf32: too many section headers: %d\n",
			file_ehdr.e_shnum);
		return -1;
	}

	for (i = 0; i < file_ehdr.e_shnum; i++) {
		if (fseek(in, i * file_ehdr.e_shentsize + file_ehdr.e_shoff,
			  SEEK_SET) < 0) {
			fprintf(stderr, "elf32: can't seek to shdr %d\n", i);
			return -1;
		}

		if (fread(&file_shdrs[i], sizeof(file_shdrs[0]), 1, in) < 0) {
			fprintf(stderr, "elf32: can't read shdr %d: %s\n",
				i, strerror(errno));
			return -1;
		}
	}

	return 0;
}

static u_int32_t file_to_phys(u_int32_t v)
{
	int i;

	for (i = 0; i < file_ehdr.e_phnum; i++) {
		Elf32_Phdr *p = &file_phdrs[i];

		if (v >= p->p_offset && v - p->p_offset < p->p_filesz)
			return v - p->p_offset + p->p_paddr;
	}

	return v;
}

static int feed_section(FILE *in, int offset, int size, imgfunc_t cb)
{
	u_int8_t buf[1024];
	u_int16_t addr = file_to_phys(offset);

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

		if (cb(addr, buf, len) < 0)
			return -1;

		size -= len;
		offset += len;
		addr += len;
	}

	return 0;
}

static int read_all(FILE *in)
{
	if (read_ehdr(in) < 0)
		return -1;
	if (file_ehdr.e_machine != EM_MSP430) {
		fprintf(stderr, "elf32: this is not an MSP430 ELF32\n");
		return -1;
	}

	if (read_phdr(in) < 0)
		return -1;
	if (read_shdr(in) < 0)
		return -1;

	return 0;
}

int elf32_extract(FILE *in, imgfunc_t cb)
{
	int i;

	if (read_all(in) < 0)
		return -1;

	for (i = 0; i < file_ehdr.e_shnum; i++) {
		Elf32_Shdr *s = &file_shdrs[i];

		if (s->sh_type == SHT_PROGBITS && s->sh_flags & SHF_ALLOC &&
			feed_section(in, s->sh_offset, s->sh_size, cb) < 0)
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

static Elf32_Shdr *find_shdr(Elf32_Word type)
{
	int i;

	for (i = 0; i < file_ehdr.e_shnum; i++) {
		Elf32_Shdr *s = &file_shdrs[i];

		if (s->sh_type == type)
			return s;
	}

	return NULL;
}

static int syms_load_strings(FILE *in, Elf32_Shdr *s)
{
	int len = s->sh_size;
	char buf[1024];

	if (fseek(in, s->sh_offset, SEEK_SET) < 0) {
		perror("elf32: can't seek to strings");
		return -1;
	}

	while (len) {
		int req = sizeof(buf) > len ? len : sizeof(buf);
		int count = fread(buf, 1, req, in);

		if (!count) {
			fprintf(stderr, "elf32: eof reading strings\n");
			return -1;
		}

		if (count < 0) {
			perror("elf32: error reading strings");
			return -1;
		}

		if (stab_add_string(buf, count) < 0)
			return -1;
		len -= count;
	}

	return 0;
}

#define N_SYMS	128

static int syms_load_syms(FILE *in, Elf32_Shdr *s, int soff)
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

			if (stab_add_symbol(y->st_name + soff,
					    y->st_value) < 0)
				return -1;
		}
		len -= count;
	}

	return 0;
}

int elf32_syms(FILE *in)
{
	Elf32_Shdr *s;
	int soff;

	if (read_all(in) < 0)
		return -1;

	s = find_shdr(SHT_SYMTAB);
	if (!s) {
		fprintf(stderr, "elf32: no symbol table\n");
		return -1;
	}

	if (s->sh_link <= 0 || s->sh_link >= file_ehdr.e_shnum) {
		fprintf(stderr, "elf32: no string table\n");
		return -1;
	}

	soff = stab_add_string(NULL, 0);
	if (syms_load_strings(in, &file_shdrs[s->sh_link]) < 0)
		return -1;
	if (syms_load_syms(in, s, soff) < 0)
		return -1;

	return 0;
}
