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
#include <assert.h>

#include "expr.h"
#include "stab.h"
#include "util.h"
#include "output.h"
#include "opdb.h"
#include "demangle.h"
#include "device.h"
#include "dis.h"

/************************************************************************
 * Address expression parsing.
 */

struct addr_exp_state {
	int            last_operator;
	address_t      data_stack[32];
	int            data_stack_size;
	int            op_stack[32];
	int            op_stack_size;
};

static int addr_exp_data(struct addr_exp_state *s, const char *text)
{
	address_t value;

	if (!s->last_operator || s->last_operator == ')') {
		printc_err("syntax error at token %s\n", text);
		return -1;
	}

	/* Hex value */
	if (*text == '0' && text[1] == 'x') {
		value = strtoul(text + 2, NULL, 16);
	} else if (*text == '0' && text[1] == 'd') {
		value = atoi(text + 2);
	} else if (*text == '@') {
		int reg = dis_reg_from_name(text + 1);
		if (reg < 0) {
			printc_err("invalid register: %s\n", text);
			return -1;
		}

		address_t regs[DEVICE_NUM_REGS];
		if (device_getregs(regs) < 0)
			return -1;

		value = regs[reg];
	} else if (stab_get(text, &value) < 0) {
		char *end;

		value = strtol(text, &end, opdb_get_numeric("iradix"));
		if (*end) {
			printc_err("can't parse token: %s\n", text);
			return -1;
		}
	}

	if (s->data_stack_size + 1 > ARRAY_LEN(s->data_stack)) {
		printc_err("data stack overflow at token %s\n", text);
		return -1;
	}

	s->data_stack[s->data_stack_size++] = value;
	s->last_operator = 0;
	return 0;
}

static int addr_exp_pop(struct addr_exp_state *s)
{
	char op = s->op_stack[--s->op_stack_size];
	address_t data1 = s->data_stack[--s->data_stack_size];
	address_t data2 = 0;
	address_t result = 0;

	if (op != 'N')
		data2 = s->data_stack[--s->data_stack_size];

	assert (s->op_stack_size >= 0);
	assert (s->data_stack_size >= 0);

	switch (op) {
	case '+':
		result = data2 + data1;
		break;

	case '-':
		result = data2 - data1;
		break;

	case '*':
		result = data2 * data1;
		break;

	case '/':
		if (!data1)
			goto divzero;
		result = data2 / data1;
		break;

	case '%':
		if (!data1)
			goto divzero;
		result = data2 % data1;
		break;

	case 'N':
		result = -data1;
		break;
	}

	s->data_stack[s->data_stack_size++] = result;
	return 0;

 divzero:
	printc_err("divide by zero\n");
	return -1;
}

static int can_push(struct addr_exp_state *s, char op)
{
	char top;

	if (!s->op_stack_size || op == '(')
		return 1;

	top = s->op_stack[s->op_stack_size - 1];

	if (top == '(')
		return 1;

	switch (op) {
	case 'N':
		return 1;

	case '*':
	case '%':
	case '/':
		return top == '+' || top == '-';

	default:
		break;
	}

	return 0;
}

static int addr_exp_op(struct addr_exp_state *s, char op)
{
	if (op == '(') {
		if (!s->last_operator || s->last_operator == ')')
			goto syntax_error;
	} else if (op == '-') {
		if (s->last_operator && s->last_operator != ')')
			op = 'N';
	} else {
		if (s->last_operator && s->last_operator != ')')
			goto syntax_error;
	}

	if (op == ')') {
		/* ) collapses the stack to the last matching ( */
		while (s->op_stack_size &&
		       s->op_stack[s->op_stack_size - 1] != '(')
			if (addr_exp_pop(s) < 0)
				return -1;

		if (!s->op_stack_size) {
			printc_err("parenthesis mismatch: )\n");
			return -1;
		}

		s->op_stack_size--;
	} else {
		while (!can_push(s, op))
			if (addr_exp_pop(s) < 0)
				return -1;

		if (s->op_stack_size + 1 > ARRAY_LEN(s->op_stack)) {
			printc_err("operator stack overflow: %c\n", op);
			return -1;
		}

		s->op_stack[s->op_stack_size++] = op;
	}

	s->last_operator = op;
	return 0;

 syntax_error:
	printc_err("syntax error at operator %c\n", op);
	return -1;
}

static int addr_exp_finish(struct addr_exp_state *s, address_t *ret)
{
	if (s->last_operator && s->last_operator != ')') {
		printc_err("syntax error at end of expression\n");
		return -1;
	}

	while (s->op_stack_size) {
		if (s->op_stack[s->op_stack_size - 1] == '(') {
			printc_err("parenthesis mismatch: (\n");
			return -1;
		}

		if (addr_exp_pop(s) < 0)
			return -1;
	}

	if (s->data_stack_size != 1) {
		printc_err("no data: stack size is %d\n",
			s->data_stack_size);
		return -1;
	}

	if (ret)
		*ret = s->data_stack[0];

	return 0;
}

int expr_eval(const char *text, address_t *addr)
{
	const char *text_save = text;
	char token_buf[MAX_SYMBOL_LENGTH];
	int token_len = 0;
	struct addr_exp_state s = {0};

	s.last_operator = '(';

	for (;;) {
		int cc;

		/* Figure out what class this character is */
		if (*text == '+' || *text == '-' ||
		    *text == '*' || *text == '/' ||
		    *text == '%' || *text == '(' ||
		    *text == ')')
			cc = 1;
		else if (!*text || isspace(*text))
			cc = 2;
		else if (isalnum(*text) || *text == '.' || *text == '_' ||
			 *text == '$' || *text == ':' || *text == '@')
			cc = 3;
		else {
			printc_err("illegal character in expression: %c\n",
				*text);
			return -1;
		}

		/* Accumulate and process token text */
		if (cc == 3) {
			if (token_len + 1 < sizeof(token_buf))
				token_buf[token_len++] = *text;
		} else if (token_len) {
			token_buf[token_len] = 0;
			token_len = 0;

			if (addr_exp_data(&s, token_buf) < 0)
				goto fail;
		}

		/* Process operators */
		if (cc == 1) {
			if (addr_exp_op(&s, *text) < 0)
				goto fail;
		}

		if (!*text)
			break;

		text++;
	}

	if (addr_exp_finish(&s, addr) < 0)
		goto fail;

	return 0;

 fail:
	printc_err("bad address expression: %s\n", text_save);
	return -1;
}
