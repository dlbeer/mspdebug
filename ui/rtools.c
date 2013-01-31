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
#include <ctype.h>

#include "util.h"
#include "device.h"
#include "dis.h"
#include "rtools.h"
#include "stab.h"
#include "expr.h"
#include "output_util.h"
#include "vector.h"

/************************************************************************
 * Instruction search ("isearch")
 */

#define ISEARCH_OPCODE          0x0001
#define ISEARCH_DSIZE           0x0002
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

static int isearch_opcode(const char *term, char **arg,
			  struct isearch_query *q)
{
	const char *opname = get_arg(arg);
	int opc;

	(void)term;

	if (q->flags & ISEARCH_OPCODE) {
		printc_err("isearch: opcode already specified\n");
		return -1;
	}

	if (!opname) {
		printc_err("isearch: opcode name expected\n");
		return -1;
	}

	opc = dis_opcode_from_name(opname);
	if (opc < 0) {
		printc_err("isearch: unknown opcode: %s\n", opname);
		return -1;
	}
	q->insn.op = opc;

	q->flags |= ISEARCH_OPCODE;
	return 0;
}

static int isearch_bw(const char *term, char **arg,
		      struct isearch_query *q)
{
	(void)arg;

	if (q->flags & ISEARCH_DSIZE) {
		printc_err("isearch: operand size already specified\n");
		return -1;
	}

	q->flags |= ISEARCH_DSIZE;
	switch (toupper(*term)) {
	case 'B':
		q->insn.dsize = MSP430_DSIZE_BYTE;
		break;

	case 'W':
		q->insn.dsize = MSP430_DSIZE_WORD;
		break;

	case 'A':
		q->insn.dsize = MSP430_DSIZE_AWORD;
		break;
	}

	return 0;
}

static int isearch_type(const char *term, char **arg,
			struct isearch_query *q)
{
	(void)arg;

	if (q->flags & ISEARCH_TYPE) {
		printc_err("isearch: instruction type already "
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

static int isearch_addr(const char *term, char **arg,
			struct isearch_query *q)
{
	int which = toupper(*term) == 'S' ?
		ISEARCH_SRC_ADDR : ISEARCH_DST_ADDR;
	const char *addr_text;
	address_t addr;

	if (q->flags & which) {
		printc_err("isearch: address already specified\n");
		return -1;
	}

	addr_text = get_arg(arg);
	if (!addr_text) {
		printc_err("isearch: address expected\n");
		return -1;
	}

	if (expr_eval(addr_text, &addr) < 0)
		return -1;

	q->flags |= which;
	if (which == ISEARCH_SRC_ADDR)
		q->insn.src_addr = addr;
	else
		q->insn.dst_addr = addr;

	return 0;
}

static int isearch_reg(const char *term, char **arg,
		       struct isearch_query *q)
{
	int which = toupper(*term) == 'S' ?
		ISEARCH_SRC_REG : ISEARCH_DST_REG;
	const char *reg_text;
	int reg;

	if (q->flags & which) {
		printc_err("isearch: register already specified\n");
		return -1;
	}

	reg_text = get_arg(arg);
	if (!reg_text) {
		printc_err("isearch: register expected\n");
		return -1;
	}

	reg = dis_reg_from_name(reg_text);
	if (reg < 0) {
		printc_err("isearch: unknown register: %s\n",
			reg_text);
		return -1;
	}

	q->flags |= which;
	if (which == ISEARCH_SRC_REG)
		q->insn.src_reg = reg;
	else
		q->insn.dst_reg = reg;

	return 0;
}

static int isearch_mode(const char *term, char **arg,
			struct isearch_query *q)
{
	int which = toupper(*term) == 'S' ?
		ISEARCH_SRC_MODE : ISEARCH_DST_MODE;
	const char *what_text;
	int what;

	if (q->flags & which) {
		printc_err("isearch: mode already specified\n");
		return -1;
	}

	what_text = get_arg(arg);
	if (!what_text) {
		printc_err("isearch: mode must be specified\n");
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
		printc_err("isearch: unknown address mode: %s\n",
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

	if ((q->flags & ISEARCH_DSIZE) &&
	    q->insn.dsize != insn->dsize)
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

static int do_isearch(address_t addr, address_t len,
		      const struct isearch_query *q)
{
	uint8_t *mbuf;
	address_t i;

	mbuf = malloc(len);
	if (!mbuf) {
		printc_err("isearch: couldn't allocate memory: %s\n",
			last_error());
		return -1;
	}

	if (device_readmem(addr, mbuf, len) < 0) {
		printc_err("isearch: couldn't read device memory\n");
		free(mbuf);
		return -1;
	}

	addr &= ~1;
	len &= ~1;
	for (i = 0; i < len; i += 2) {
		struct msp430_instruction insn;
		int count = dis_decode(mbuf + i, addr + i, len - i, &insn);

		if (count >= 0 && isearch_match(&insn, q))
			disassemble(addr + i, mbuf + i, count,
				    device_default->power_buf);
	}

	free(mbuf);
	return 0;
}

int cmd_isearch(char **arg)
{
	static const struct {
		const char      *name;
		int             (*func)(const char *term, char **arg,
					struct isearch_query *q);
	} term_handlers[] = {
		{"opcode",      isearch_opcode},
		{"byte",        isearch_bw},
		{"word",        isearch_bw},
		{"aword",       isearch_bw},
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

	struct isearch_query q;
	const char *addr_text;
	const char *len_text;
	address_t addr;
	address_t len;

	addr_text = get_arg(arg);
	len_text = get_arg(arg);

	if (!(addr_text && len_text)) {
		printc_err("isearch: address and length expected\n");
		return -1;
	}

	if (expr_eval(addr_text, &addr) < 0 ||
	    expr_eval(len_text, &len) < 0)
		return -1;

	q.flags = 0;
	for (;;) {
		const char *term = get_arg(arg);
		int i;

		if (!term)
			break;

		for (i = 0; i < ARRAY_LEN(term_handlers); i++)
			if (!strcasecmp(term_handlers[i].name, term)) {
				if (term_handlers[i].func(term, arg, &q) < 0)
					return -1;
				break;
			}
	}

	if (!q.flags) {
		printc_err("isearch: no query terms given "
			"(perhaps you mean \"dis\"?)\n");
		return -1;
	}

	return do_isearch(addr, len, &q);
}

/************************************************************************
 * Call graph ("cgraph")
 */

struct cg_edge {
	int             is_tail_call;

	address_t       src;
	address_t       dst;
};

static int cmp_branch_by_dst(const void *a, const void *b)
{
	const struct cg_edge *br_a = (const struct cg_edge *)a;
	const struct cg_edge *br_b = (const struct cg_edge *)b;

	if (br_a->dst < br_b->dst)
		return -1;
	if (br_a->dst > br_b->dst)
		return 1;

	if (br_a->src < br_b->src)
		return -1;
	if (br_a->src > br_b->src)
		return 1;

	if (!br_a->is_tail_call && br_b->is_tail_call)
		return -1;
	if (br_a->is_tail_call && !br_b->is_tail_call)
		return 1;

	return 0;
}

static int cmp_branch_by_src(const void *a, const void *b)
{
	const struct cg_edge *br_a = (const struct cg_edge *)a;
	const struct cg_edge *br_b = (const struct cg_edge *)b;

	if (br_a->src < br_b->src)
		return -1;
	if (br_a->src > br_b->src)
		return 1;

	if (br_a->dst < br_b->dst)
		return -1;
	if (br_a->dst > br_b->dst)
		return 1;

	if (!br_a->is_tail_call && br_b->is_tail_call)
		return -1;
	if (br_a->is_tail_call && !br_b->is_tail_call)
		return 1;

	return 0;
}

struct cg_node {
	address_t      offset;
};

static int cmp_node(const void *a, const void *b)
{
	const struct cg_node *na = (const struct cg_node *)a;
	const struct cg_node *nb = (const struct cg_node *)b;

	if (na->offset < nb->offset)
		return -1;
	if (na->offset > nb->offset)
		return 1;

	return 0;
}

struct call_graph {
	int             offset;
	int             len;

	struct vector   edge_to;
	struct vector   edge_from;
	struct vector   node_list;
};

#define CG_NODE(g, i) (VECTOR_PTR((g)->node_list, (i), struct cg_node))
#define CG_EDGE_FROM(g, i) (VECTOR_PTR((g)->edge_from, (i), struct cg_edge))
#define CG_EDGE_TO(g, i) (VECTOR_PTR((g)->edge_to, (i), struct cg_edge))

static void cgraph_destroy(struct call_graph *graph)
{
	vector_destroy(&graph->edge_to);
	vector_destroy(&graph->edge_from);
	vector_destroy(&graph->node_list);
}

static int find_possible_edges(int offset, int len, uint8_t *memory,
			       struct call_graph *graph)
{
	int i;

	for (i = 0; i < len; i += 2) {
		struct msp430_instruction insn;

		if (dis_decode(memory + i, offset + i, len - i, &insn) < 0)
			continue;

		if (insn.dst_mode == MSP430_AMODE_IMMEDIATE &&
		    (insn.op == MSP430_OP_CALL || insn.op == MSP430_OP_BR) &&
		    !(insn.dst_addr & 1)) {
			struct cg_edge br;

			br.src = offset + i;
			br.dst = insn.dst_addr;
			br.is_tail_call = insn.op != MSP430_OP_CALL;

			if (vector_push(&graph->edge_from, &br, 1) < 0)
				return -1;
		}
	}

	return 0;
}

static int add_nodes_from_edges(struct call_graph *graph)
{
	int i;
	address_t last_addr = 0;
	int have_last_addr = 0;

	qsort(graph->edge_from.ptr, graph->edge_from.size,
	      graph->edge_from.elemsize, cmp_branch_by_dst);

	/* Look for unique destination addresses */
	for (i = 0; i < graph->edge_from.size; i++) {
		const struct cg_edge *br = CG_EDGE_FROM(graph, i);

		if (!have_last_addr ||
		    br->dst != last_addr) {
			struct cg_node n;

			n.offset = br->dst;

			last_addr = br->dst;
			have_last_addr = 1;

			if (vector_push(&graph->node_list, &n, 1) < 0)
				return -1;
		}
	}

	return 0;
}

static void relabel_sources(struct call_graph *graph)
{
	int i = 0; /* Node index */
	int j = 0; /* Edge index */

	/* Identify the source nodes for each edge */
	qsort(graph->edge_from.ptr, graph->edge_from.size,
	      graph->edge_from.elemsize, cmp_branch_by_src);

	while (j < graph->edge_from.size) {
		struct cg_edge *br = CG_EDGE_FROM(graph, j);
		struct cg_node *n;

		/* Skip over nodes which are too early for this edge */
		while (i + 1 < graph->node_list.size &&
		       CG_NODE(graph, i + 1)->offset <= br->src)
			i++;

		n = CG_NODE(graph, i);
		if (n->offset <= br->src)
			br->src = n->offset;
		j++;
	}
}

static void remove_duplicate_nodes(struct call_graph *graph)
{
	int i = 0;
	int j = 0;

	qsort(graph->node_list.ptr, graph->node_list.size,
	      graph->node_list.elemsize, cmp_node);

	while (i < graph->node_list.size) {
		struct cg_node *n = CG_NODE(graph, i);
		struct cg_node *l = CG_NODE(graph, j - 1);

		if (!j || n->offset != l->offset) {
			if (i != j)
				memcpy(l + 1, n, sizeof(*l));
			j++;
		}

		i++;
	}

	graph->node_list.size = j;
}

static void remove_duplicate_edges(struct call_graph *graph)
{
	int i = 0; /* Source index */
	int j = 0; /* Destination index */

	qsort(graph->edge_from.ptr, graph->edge_from.size,
	      graph->edge_from.elemsize, cmp_branch_by_src);

	while (i < graph->edge_from.size) {
		struct cg_edge *e = CG_EDGE_FROM(graph, i);
		struct cg_edge *l = CG_EDGE_FROM(graph, j - 1);

		if (!j ||
		    l->src != e->src ||
		    l->dst != e->dst ||
		    l->is_tail_call != e->is_tail_call) {
			if (i != j)
				memcpy(l + 1, e, sizeof(*l));
			j++;
		}

		i++;
	}

	graph->edge_from.size = j;
}

static int build_inverse(struct call_graph *graph)
{
	graph->edge_to.size = 0;

	if (vector_push(&graph->edge_to, graph->edge_from.ptr,
			graph->edge_from.size) < 0)
		return -1;

	qsort(graph->edge_to.ptr, graph->edge_to.size,
	      graph->edge_to.elemsize, cmp_branch_by_dst);

	return 0;
}

static int add_irq_edges(address_t offset, address_t len, uint8_t *memory,
			 struct call_graph *graph)
{
	int i;

	if (offset > 0x10000 || offset + len <= 0xffe0)
		return 0;

	if (offset < 0xffe0) {
		len -= (0xffe0 - offset);
		memory += (0xffe0 - offset);
		offset = 0xffe0;
	}

	if (offset + len > 0x10000)
		len = 0x10000 - offset;

	if (offset & 1) {
		offset++;
		memory++;
		len--;
	}

	for (i = 0; i < len; i += 2) {
		struct cg_edge br;

		br.src = offset + i;
		br.dst = ((address_t)memory[i]) |
			(((address_t)memory[i + 1]) << 8);
		br.is_tail_call = 0;

		if (vector_push(&graph->edge_from, &br, 1) < 0)
			return -1;
	}

	return 0;
}

static int add_symbol_nodes(void *user_data, const char *name,
			    address_t offset)
{
	struct call_graph *graph = (struct call_graph *)user_data;

	while (*name) {
		if (*name == '.')
			return 0;
		name++;
	}

	if (offset > graph->offset &&
	    offset <= graph->offset + graph->len) {
		struct cg_node n;

		n.offset = offset;
		return vector_push(&graph->node_list, &n, 1);
	}

	return 0;
}

static int cgraph_init(address_t offset, address_t len, uint8_t *memory,
		       struct call_graph *graph)
{
	vector_init(&graph->edge_to, sizeof(struct cg_edge));
	vector_init(&graph->edge_from, sizeof(struct cg_edge));
	vector_init(&graph->node_list, sizeof(struct cg_node));

	graph->offset = offset;
	graph->len = len;

	if (find_possible_edges(offset, len, memory, graph) < 0)
		goto fail;
	if (add_irq_edges(offset, len, memory, graph) < 0)
		goto fail;

	if (stab_enum(add_symbol_nodes, graph) < 0)
		goto fail;
	if (add_nodes_from_edges(graph) < 0)
		goto fail;
	remove_duplicate_nodes(graph);

	relabel_sources(graph);
	remove_duplicate_edges(graph);

	if (build_inverse(graph) < 0)
		goto fail;

	return 0;

 fail:
	cgraph_destroy(graph);
	return -1;
}

static void cgraph_summary(struct call_graph *graph)
{
	int i;
	int j = 0; /* Edge from index */
	int k = 0; /* Edge to index */

	for (i = 0; i < graph->node_list.size; i++) {
		struct cg_node *n = CG_NODE(graph, i);
		int from_count = 0;
		int to_count = 0;
		char name[64];

		while (j < graph->edge_from.size &&
		       CG_EDGE_FROM(graph, j)->src < n->offset)
			j++;

		while (k < graph->edge_to.size &&
		       CG_EDGE_TO(graph, k)->dst < n->offset)
			k++;

		while (j < graph->edge_from.size &&
		       CG_EDGE_FROM(graph, j)->src == n->offset) {
			from_count++;
			j++;
		}

		while (k < graph->edge_to.size &&
		       CG_EDGE_TO(graph, k)->dst == n->offset) {
			to_count++;
			k++;
		}

		print_address(n->offset, name, sizeof(name), 0);
		printc("0x%04x [%3d ==> %3d] %s\n",
		       n->offset, to_count, from_count, name);
	}
}

static void cgraph_func_info(struct call_graph *graph, address_t addr)
{
	int i = 0;
	int j = 0;
	int k = 0;
	char name[64];
	struct cg_node *n;

	while (i + 1 < graph->node_list.size &&
	       CG_NODE(graph, i + 1)->offset <= addr)
		i++;
	if (i >= graph->node_list.size ||
	    CG_NODE(graph, i)->offset > addr) {
		printc("No information for address 0x%04x\n", addr);
		return;
	}

	n = CG_NODE(graph, i);

	while (j < graph->edge_from.size &&
	       CG_EDGE_FROM(graph, j)->src < n->offset)
		j++;

	while (k < graph->edge_to.size &&
	       CG_EDGE_TO(graph, k)->dst < n->offset)
		k++;

	print_address(n->offset, name, sizeof(name), 0);
	printc("0x%04x %s:\n", n->offset, name);

	if (j < graph->edge_from.size &&
	    CG_EDGE_FROM(graph, j)->src == n->offset) {
		printc("    Callees:\n");
		while (j < graph->edge_from.size) {
			struct cg_edge *e = CG_EDGE_FROM(graph, j);

			if (e->src != n->offset)
				break;

			print_address(e->dst, name, sizeof(name), 0);
			printc("        %s%s\n",
			       e->is_tail_call ? "*" : "", name);

			j++;
		}
		printc("\n");
	}

	if (k < graph->edge_to.size &&
	    CG_EDGE_TO(graph, k)->dst == n->offset) {
		printc("    Callers:\n");
		while (k < graph->edge_to.size) {
			struct cg_edge *e = CG_EDGE_TO(graph, k);

			if (e->dst != n->offset)
				break;

			print_address(e->src, name, sizeof(name), 0);
			printc("        %s%s\n",
			       e->is_tail_call ? "*" : "", name);

			k++;
		}
	}
}

int cmd_cgraph(char **arg)
{
	char *offset_text, *len_text, *addr_text;;
	address_t offset, len, addr;
	uint8_t *memory;
	struct call_graph graph;

	/* Figure out what the arguments are */
	offset_text = get_arg(arg);
	len_text = get_arg(arg);
	addr_text = get_arg(arg);

	if (!(offset_text && len_text)) {
		printc_err("cgraph: offset and length must be "
			"specified\n");
		return -1;
	}

	if (expr_eval(offset_text, &offset) < 0) {
		printc_err("cgraph: invalid offset: %s\n", offset_text);
		return -1;
	}
	offset &= ~1;

	if (expr_eval(len_text, &len) < 0) {
		printc_err("cgraph: invalid length: %s\n", len_text);
		return -1;
	}
	len &= ~1;

	if (addr_text && expr_eval(addr_text, &addr) < 0) {
		printc_err("cgraph: invalid address: %s\n", addr_text);
		return -1;
	}

	/* Grab the memory to be analysed */
	memory = malloc(len);
	if (!memory) {
		printc_err("cgraph: couldn't allocate memory: %s\n",
			last_error());
		return -1;
	}

	if (device_readmem(offset, memory, len) < 0) {
		printc_err("cgraph: couldn't fetch memory\n");
		free(memory);
		return -1;
	}

	/* Produce and display the call graph */
	if (cgraph_init(offset, len, memory, &graph) < 0) {
		printc_err("cgraph: couldn't build call graph\n");
		free(memory);
		return -1;
	}
	free(memory);

	if (addr_text)
		cgraph_func_info(&graph, addr);
	else
		cgraph_summary(&graph);

	cgraph_destroy(&graph);
	return 0;
}
