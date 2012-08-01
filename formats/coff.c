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
#include <string.h>

#include "coff.h"
#include "util.h"
#include "output.h"

struct coff_header {
	uint16_t        version;
	int             sec_count;
	uint32_t        timestamp;
	int             stab_start;
	int             stab_count;
	int             opt_bytes;
	uint16_t        flags;
	uint16_t        target_id;
};

/* Header sizes */
#define FILE_HEADER_SIZE        22
#define OPT_HEADER_SIZE         28
#define SHDR_SIZE               48
#define STAB_ENTRY_SIZE         18

/* Bits in the flags field */
#define F_RELFLG        0x0001
#define F_EXEC          0x0002
#define F_LSYMS         0x0008
#define F_LITTLE        0x0100
#define F_BIG           0x0200
#define F_SYMMERGE      0x1000

/* Section header flags */
#define STYP_REG        0x00000000
#define STYP_DSECT      0x00000001
#define STYP_NOLOAD     0x00000002
#define STYP_GROUP      0x00000004
#define STYP_PAD        0x00000008
#define STYP_COPY       0x00000010
#define STYP_TEXT       0x00000020
#define STYP_DATA       0x00000040
#define STYP_BSS        0x00000080
#define STYP_BLOCK      0x00001000
#define STYP_PASS       0x00002000
#define STYP_CLINK      0x00004000
#define STYP_VECTOR     0x00008000
#define STYP_PADDED     0x00010000

/* Symbol storage classes */
#define C_NULL          0
#define C_AUTO          1
#define C_EXT           2
#define C_STAT          3
#define C_REG           4
#define C_EXTREF        5
#define C_LABEL         6
#define C_ULABEL        7
#define C_MOS           8
#define C_ARG           9
#define C_STRTAG        10
#define C_MOU           11
#define C_UNTAG         12
#define C_TPDEF         13
#define C_USTATIC       14
#define C_ENTAG         15
#define C_MOE           16
#define C_REGPARM       17
#define C_FIELD         18
#define C_UEXT          19
#define C_STATLAB       20
#define C_EXTLAB        21
#define C_VARARG        22
#define C_BLOCK         100
#define C_FCN           101
#define C_EOS           102
#define C_FILE          103
#define C_LINE          104

/* MSP430 magic number */
#define MSP430_MAGIC    0x00a0

static int read_block(FILE *in, int offset, int size, void *buf)
{
	int len;

	if (size < 0) {
		printc_err("coff: invalid size: %d\n", size);
		return -1;
	}

	if (fseek(in, offset, SEEK_SET) < 0) {
		printc_err("coff: can't seek to offset %d: %s\n",
			offset, last_error());
		return -1;
	}

	len = fread(buf, 1, size, in);
	if (len < 0) {
		printc_err("coff: can't read %d bytes from "
			"offset %d: %s\n",
			size, offset, last_error());
		return -1;
	}

	if (len < size) {
		printc_err("coff: can't read %d bytes from "
			"offset %d: short read\n",
			size, offset);
		return -1;
	}

	return 0;
}

static void parse_header(const uint8_t *data, struct coff_header *hdr)
{
	hdr->version = LE_WORD(data, 0);
	hdr->sec_count = LE_WORD(data, 2);
	hdr->timestamp = LE_LONG(data, 4);
	hdr->stab_start = LE_LONG(data, 8);
	hdr->stab_count = LE_LONG(data, 12);
	hdr->opt_bytes = LE_WORD(data, 16);
	hdr->flags = LE_WORD(data, 18);
	hdr->target_id = LE_WORD(data, 20);
}

static int read_header(FILE *in, struct coff_header *hdr)
{
	uint8_t hdr_data[FILE_HEADER_SIZE];

	if (read_block(in, 0, FILE_HEADER_SIZE, hdr_data) < 0) {
		printc_err("coff: failed to extract COFF header\n");
		return -1;
	}

	parse_header(hdr_data, hdr);
	return 0;
}

int coff_check(FILE *in)
{
	uint8_t data[FILE_HEADER_SIZE];

	rewind(in);
	if (fread(data, 1, FILE_HEADER_SIZE, in) != FILE_HEADER_SIZE)
		return 0;

	return data[20] == 0xa0 && !data[21];
}

static int read_sechdrs(FILE *in, const struct coff_header *hdr,
			uint8_t **ret_tab)
{
	uint8_t *table;
	int alloc_size = SHDR_SIZE * hdr->sec_count;

	if (!hdr->sec_count) {
		*ret_tab = NULL;
		return 0;
	}

	table = malloc(alloc_size);
	if (!table) {
		pr_error("coff: can't allocate memory for section headers");
		return -1;
	}

	if (read_block(in, hdr->opt_bytes + FILE_HEADER_SIZE,
		       SHDR_SIZE * hdr->sec_count, table) < 0) {
		printc_err("coff: can't read section headers\n");
		free(table);
		return -1;
	}

	*ret_tab = table;
	return hdr->sec_count;
}

static int load_section(FILE *in, uint32_t addr, uint32_t offset,
			uint32_t size,
			binfile_imgcb_t cb, void *user_data)
{
	struct binfile_chunk ch = {0};
	uint8_t *section;

	if (!size)
		return 0;

	section = malloc(size);
	if (!section) {
		printc_err("coff: couldn't allocate memory for "
			"section at 0x%x: %s\n", offset, last_error());
		return -1;
	}

	if (read_block(in, offset, size, section) < 0) {
		printc_err("coff: couldn't read section at 0x%x\n",
			offset);
		free(section);
		return -1;
	}

	ch.addr = addr;
	ch.len = size;
	ch.data = section;

	if (cb(user_data, &ch) < 0) {
		free(section);
		return -1;
	}

	free(section);
	return 0;
}

int coff_extract(FILE *in, binfile_imgcb_t cb, void *user_data)
{
	struct coff_header hdr;
	uint8_t *shdrs;
	int ret = 0;
	int i;

	if (read_header(in, &hdr) < 0)
		return -1;

	if (read_sechdrs(in, &hdr, &shdrs) < 0)
		return -1;

	for (i = 0; i < hdr.sec_count; i++) {
		uint8_t *header = shdrs + SHDR_SIZE * i;
		uint32_t flags = LE_LONG(header, 40);

		if (((flags & STYP_TEXT) || (flags & STYP_DATA)) &&
		    !(flags & STYP_NOLOAD)) {
			uint32_t addr = LE_LONG(header, 8);
			uint32_t offset = LE_LONG(header, 20);
			uint32_t size = LE_LONG(header, 16);

			if (load_section(in, addr, offset, size,
					 cb, user_data) < 0) {
				printc_err("coff: error while loading "
					"section %d\n", i);
				ret = -1;
				break;
			}
		}
	}

	if (shdrs)
		free(shdrs);

	return ret;
}

static int read_strtab(FILE *in, const struct coff_header *hdr,
		       char **ret_tab)
{
	char *strtab;
	int file_size;
	int alloc_size;
	int strtab_len;
	int strtab_start = hdr->stab_count * STAB_ENTRY_SIZE + hdr->stab_start;

	if (fseek(in, 0, SEEK_END) < 0) {
		printc_err("coff: can't seek to end\n");
		return -1;
	}

	file_size = ftell(in);
	strtab_len = file_size - strtab_start;

	if (strtab_len < 0) {
		printc_err("coff: invalid string table size\n");
		return -1;
	}

	if (!strtab_len) {
		*ret_tab = NULL;
		return 0;
	}

	alloc_size = strtab_len + 1;
	strtab = malloc(alloc_size);
	if (!strtab) {
		pr_error("coff: can't allocate memory for string table");
		return -1;
	}

	if (read_block(in, strtab_start, strtab_len, strtab) < 0) {
		printc_err("coff: failed to read string table\n");
		free(strtab);
		return -1;
	}

	strtab[strtab_len] = 0;
	*ret_tab = strtab;
	return strtab_len;
}

static int read_symtab(FILE *in, const struct coff_header *hdr,
		       uint8_t **ret_tab)
{
	uint8_t *table;
	int alloc_size = STAB_ENTRY_SIZE * hdr->stab_count;

	if (!hdr->stab_count) {
		*ret_tab = NULL;
		return 0;
	}

	table = malloc(alloc_size);
	if (!table) {
		pr_error("coff: failed to allocate memory for symbol table");
		return -1;
	}

	if (read_block(in, hdr->stab_start,
		       STAB_ENTRY_SIZE * hdr->stab_count, table) < 0) {
		printc_err("coff: failed to read symbol table\n");
		free(table);
		return -1;
	}

	*ret_tab = table;
	return hdr->stab_count;
}

int coff_syms(FILE *in)
{
	struct coff_header hdr;
	char *strtab;
	uint8_t *symtab;
	int strtab_len;
	int i;
	int ret = 0;

	if (read_header(in, &hdr) < 0)
		return -1;

	strtab_len = read_strtab(in, &hdr, &strtab);
	if (strtab_len < 0)
		return -1;

	if (read_symtab(in, &hdr, &symtab) < 0) {
		if (strtab)
			free(strtab);
		return -1;
	}

	for (i = 0; i < hdr.stab_count; i++) {
		uint8_t *entry = symtab + i * STAB_ENTRY_SIZE;
		uint32_t value = LE_LONG(entry, 8);
		int storage_class = entry[16];
		char namebuf[9];
		const char *name = NULL;

		if (LE_LONG(entry, 0)) {
			memcpy(namebuf, entry, 8);
			namebuf[8] = 0;
			name = namebuf;
		} else {
			uint32_t offset = LE_LONG(entry, 4);

			if (offset >= 4 && offset < strtab_len)
				name = strtab + offset;
		}

		if (name &&
		    (storage_class == C_EXT || storage_class == C_LABEL) &&
		    stab_set(name, value) < 0) {
			printc_err("coff: failed to insert symbol\n");
			ret = -1;
			break;
		}

		/* Skip auxiliary entries */
		i += entry[17];
	}

	if (symtab)
		free(symtab);
	if (strtab)
		free(strtab);

	return ret;
}
