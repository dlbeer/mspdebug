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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "util.h"
#include "device.h"
#include "dis.h"
#include "rtools.h"
#include "stab.h"
#include "expr.h"
#include "cproc_util.h"

#define ISEARCH_OPCODE          0x0001
#define ISEARCH_BW              0x0002
#define ISEARCH_SRC_ADDR        0x0004
#define ISEARCH_DST_ADDR        0x0008
#define ISEARCH_SRC_MODE        0x0010
#define ISEARCH_DST_MODE        0x0020
#define ISEARCH_SRC_REG         0x0040
#define ISEARCH_DST_REG         0x0080
#define ISEARCH_TYPE            0x0100

struct isearch_query {
	int                             flags;
	struct msp430_instruction       insn;
};

static int isearch_opcode(cproc_t cp, const char *term, char **arg,
			  struct isearch_query *q)
{
	const char *opname = get_arg(arg);

	if (q->flags & ISEARCH_OPCODE) {
		fprintf(stderr, "isearch: opcode already specified\n");
		return -1;
	}

	if (!opname) {
		fprintf(stderr, "isearch: opcode name expected\n");
		return -1;
	}

	q->insn.op = dis_opcode_from_name(opname);
	if (q->insn.op < 0) {
		fprintf(stderr, "isearch: unknown opcode: %s\n", opname);
		return -1;
	}

	q->flags |= ISEARCH_OPCODE;
	return 0;
}

static int isearch_bw(cproc_t cp, const char *term, char **arg,
		      struct isearch_query *q)
{
	if (q->flags & ISEARCH_BW) {
		fprintf(stderr, "isearch: operand size already specified\n");
		return -1;
	}

	q->flags |= ISEARCH_BW;
	q->insn.is_byte_op = (toupper(*term) == 'B');
	return 0;
}

static int isearch_type(cproc_t cp, const char *term, char **arg,
			struct isearch_query *q)
{
	if (q->flags & ISEARCH_TYPE) {
		fprintf(stderr, "isearch: instruction type already "
			"specified\n");
		return -1;
	}

	q->flags |= ISEARCH_TYPE;

	switch (toupper(*term)) {
	case 'J':
		q->insn.itype = MSP430_ITYPE_JUMP;
		break;

	case 'S':
		q->insn.itype = MSP430_ITYPE_SINGLE;
		break;

	case 'D':
		q->insn.itype = MSP430_ITYPE_DOUBLE;
		break;

	default:
		q->insn.itype = MSP430_ITYPE_NOARG;
		break;
	}

	return 0;
}

static int isearch_addr(cproc_t cp, const char *term, char **arg,
			struct isearch_query *q)
{
	int which = toupper(*term) == 'S' ?
		ISEARCH_SRC_ADDR : ISEARCH_DST_ADDR;
	const char *addr_text;
	int addr;

	if (q->flags & which) {
		fprintf(stderr, "isearch: address already specified\n");
		return -1;
	}

	addr_text = get_arg(arg);
	if (!addr_text) {
		fprintf(stderr, "isearch: address expected\n");
		return -1;
	}

	if (expr_eval(cproc_stab(cp), addr_text, &addr) < 0)
		return -1;

	q->flags |= which;
	if (which == ISEARCH_SRC_ADDR)
		q->insn.src_addr = addr;
	else
		q->insn.dst_addr = addr;

	return 0;
}

static int isearch_reg(cproc_t cp, const char *term, char **arg,
		       struct isearch_query *q)
{
	int which = toupper(*term) == 'S' ?
		ISEARCH_SRC_REG : ISEARCH_DST_REG;
	const char *reg_text;
	int reg;

	if (q->flags & which) {
		fprintf(stderr, "isearch: register already specified\n");
		return -1;
	}

	reg_text = get_arg(arg);
	if (!reg_text) {
		fprintf(stderr, "isearch: register expected\n");
		return -1;
	}

	while (*reg_text && !isdigit(*reg_text))
		reg_text++;
	reg = atoi(reg_text);

	q->flags |= which;
	if (which == ISEARCH_SRC_REG)
		q->insn.src_reg = reg;
	else
		q->insn.dst_reg = reg;

	return 0;
}

static int isearch_mode(cproc_t cp, const char *term, char **arg,
			struct isearch_query *q)
{
	int which = toupper(*term) == 'S' ?
		ISEARCH_SRC_MODE : ISEARCH_DST_MODE;
	const char *what_text;
	int what;

	if (q->flags & which) {
		fprintf(stderr, "isearch: mode already specified\n");
		return -1;
	}

	what_text = get_arg(arg);
	if (!what_text) {
		fprintf(stderr, "isearch: mode must be specified\n");
		return -1;
	}

	switch (toupper(*what_text)) {
	case 'R':
		what = MSP430_AMODE_REGISTER;
		break;

	case '@':
		what = MSP430_AMODE_INDIRECT;
		break;

	case '+':
		what = MSP430_AMODE_INDIRECT_INC;
		break;

	case '#':
		what = MSP430_AMODE_IMMEDIATE;
		break;

	case 'I':
		what = MSP430_AMODE_INDEXED;
		break;

	case '&':
		what = MSP430_AMODE_ABSOLUTE;
		break;

	case 'S':
		what = MSP430_AMODE_SYMBOLIC;
		break;

	default:
		fprintf(stderr, "isearch: unknown address mode: %s\n",
			what_text);
		return -1;
	}

	q->flags |= which;
	if (which == ISEARCH_SRC_MODE)
		q->insn.src_mode = what;
	else
		q->insn.dst_mode = what;

	return 0;
}

static int isearch_match(const struct msp430_instruction *insn,
			 const struct isearch_query *q)
{
	if ((q->flags & (ISEARCH_SRC_ADDR | ISEARCH_SRC_MODE |
			 ISEARCH_SRC_REG)) &&
	    insn->itype != MSP430_ITYPE_DOUBLE)
		return 0;

	if ((q->flags & (ISEARCH_DST_ADDR | ISEARCH_DST_MODE |
			 ISEARCH_DST_REG)) &&
	    insn->itype == MSP430_ITYPE_NOARG)
		return 0;

	if ((q->flags & ISEARCH_OPCODE) &&
	    insn->op != q->insn.op)
		return 0;

	if ((q->flags & ISEARCH_BW) &&
	    (q->insn.is_byte_op ? 1 : 0) != (insn->is_byte_op ? 1 : 0))
		return 0;

	if (q->flags & ISEARCH_SRC_ADDR) {
		if (insn->src_mode != MSP430_AMODE_INDEXED &&
		    insn->src_mode != MSP430_AMODE_SYMBOLIC &&
		    insn->src_mode != MSP430_AMODE_ABSOLUTE &&
		    insn->src_mode != MSP430_AMODE_IMMEDIATE)
			return 0;
		if (insn->src_addr != q->insn.src_addr)
			return 0;
	}

	if (q->flags & ISEARCH_DST_ADDR) {
		if (insn->dst_mode != MSP430_AMODE_INDEXED &&
		    insn->dst_mode != MSP430_AMODE_SYMBOLIC &&
		    insn->dst_mode != MSP430_AMODE_ABSOLUTE &&
		    insn->dst_mode != MSP430_AMODE_IMMEDIATE)
			return 0;
		if (insn->dst_addr != q->insn.dst_addr)
			return 0;
	}

	if ((q->flags & ISEARCH_SRC_MODE) &&
	    insn->src_mode != q->insn.src_mode)
		return 0;

	if ((q->flags & ISEARCH_DST_MODE) &&
	    insn->dst_mode != q->insn.dst_mode)
		return 0;

	if (q->flags & ISEARCH_SRC_REG) {
		if (insn->src_mode != MSP430_AMODE_REGISTER &&
		    insn->src_mode != MSP430_AMODE_INDIRECT &&
		    insn->src_mode != MSP430_AMODE_INDIRECT_INC &&
		    insn->src_mode != MSP430_AMODE_INDEXED)
			return 0;
		if (insn->src_reg != q->insn.src_reg)
			return 0;
	}

	if (q->flags & ISEARCH_DST_REG) {
		if (insn->dst_mode != MSP430_AMODE_REGISTER &&
		    insn->dst_mode != MSP430_AMODE_INDIRECT &&
		    insn->dst_mode != MSP430_AMODE_INDIRECT_INC &&
		    insn->dst_mode != MSP430_AMODE_INDEXED)
			return 0;
		if (insn->dst_reg != q->insn.dst_reg)
			return 0;
	}

	if ((q->flags & ISEARCH_TYPE) &&
	    insn->itype != q->insn.itype)
		return 0;

	return 1;
}

static int do_isearch(cproc_t cp,
		      int addr, int len, const struct isearch_query *q)
{
	u_int8_t *mbuf;
	device_t dev = cproc_device(cp);
	int i;

	if (len <= 0 || len > 0x10000 ||
	    addr <= 0 || addr >= 0x10000 ||
	    addr + len > 0x10000) {
		fprintf(stderr, "isearch: invalid memory range\n");
		return -1;
	}

	mbuf = malloc(len);
	if (!mbuf) {
		fprintf(stderr, "isearch: couldn't allocate memory: %s\n",
			strerror(errno));
		return -1;
	}

	if (dev->readmem(dev, addr, mbuf, len) < 0) {
		fprintf(stderr, "isearch: couldn't read device memory\n");
		free(mbuf);
		return -1;
	}

	for (i = 0; i < len; i += 2) {
		struct msp430_instruction insn;
		int count = dis_decode(mbuf + i, addr + i, len - i, &insn);

		if (count >= 0 && isearch_match(&insn, q))
			cproc_disassemble(cp, addr + i, mbuf + i, count);
	}

	free(mbuf);
	return 0;
}

static int cmd_isearch(cproc_t cp, char **arg)
{
	const static struct {
		const char      *name;
		int             (*func)(cproc_t cp,
					const char *term, char **arg,
					struct isearch_query *q);
	} term_handlers[] = {
		{"opcode",      isearch_opcode},
		{"byte",        isearch_bw},
		{"word",        isearch_bw},
		{"jump",        isearch_type},
		{"single",      isearch_type},
		{"double",      isearch_type},
		{"src",         isearch_addr},
		{"dst",         isearch_addr},
		{"srcreg",      isearch_reg},
		{"dstreg",      isearch_reg},
		{"srcmode",     isearch_mode},
		{"dstmode",     isearch_mode}
	};

	stab_t stab = cproc_stab(cp);
	struct isearch_query q;
	const char *addr_text;
	const char *len_text;
	int addr;
	int len;

	addr_text = get_arg(arg);
	len_text = get_arg(arg);

	if (!(addr_text && len_text)) {
		fprintf(stderr, "isearch: address and length expected\n");
		return -1;
	}

	if (expr_eval(stab, addr_text, &addr) < 0 ||
	    expr_eval(stab, len_text, &len) < 0)
		return -1;

	q.flags = 0;
	for (;;) {
		const char *term = get_arg(arg);
		int i;

		if (!term)
			break;

		for (i = 0; i < ARRAY_LEN(term_handlers); i++)
			if (!strcasecmp(term_handlers[i].name, term)) {
				if (term_handlers[i].func(cp, term, arg,
							  &q) < 0)
					return -1;
				break;
			}
	}

	if (!q.flags) {
		fprintf(stderr, "isearch: no query terms given "
			"(perhaps you mean \"dis\"?)\n");
		return -1;
	}

	return do_isearch(cp, addr, len, &q);
}

static const struct cproc_command isearch_command = {
	.name = "isearch",
	.func = cmd_isearch,
	.help =
	"isearch <address> <length> [options ...]\n"
	"    Search for an instruction matching certain search terms. These\n"
	"    terms may be any of the following:\n"
	"        opcode <opcode>\n"
	"        byte|word\n"
	"        jump|single|double|noarg\n"
	"        src <value>\n"
	"        dst <value>\n"
	"        srcreg <register>\n"
	"        dstreg <register>\n"
	"        srcmode R|I|S|&|@|+|#\n"
	"        dstmode R|I|S|&|@|+|#\n"
	"    For single-operand instructions, the operand is considered the\n"
	"    destination operand.\n"
};

int rtools_register(cproc_t cp)
{
	return cproc_register_commands(cp, &isearch_command, 1);
}
