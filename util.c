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
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include "util.h"

static struct option *option_list;
static struct command *command_list;

void register_option(struct option *o)
{
	o->next = option_list;
	option_list = o;
}

static struct option *find_option(const char *name)
{
	struct option *o;

	for (o = option_list; o; o = o->next)
		if (!strcasecmp(o->name, name))
			return o;

	return NULL;
}

static struct command *find_command(const char *name)
{
	struct command *c;

	for (c = command_list; c; c = c->next)
		if (!strcasecmp(c->name, name))
			return c;

	return NULL;
}

void register_command(struct command *c)
{
	c->next = command_list;
	command_list = c;
}

static int interactive_call = 1;

int is_interactive(void)
{
	return interactive_call;
}

char *get_arg(char **text)
{
	char *start;
	char *end;

	if (!text)
		return NULL;

	start = *text;
	while (*start && isspace(*start))
		start++;

	if (!*start)
		return NULL;

	end = start;
	while (*end && !isspace(*end))
		end++;

	if (*end)
	    while (*end && isspace(*end))
		    *(end++) = 0;

	*text = end;
	return start;
}

int process_command(char *arg, int interactive)
{
	const char *cmd_text;
	int len = strlen(arg);

	while (len && isspace(arg[len - 1]))
		len--;
	arg[len] = 0;

	cmd_text = get_arg(&arg);
	if (cmd_text) {
		const struct command *cmd = find_command(cmd_text);

		if (cmd) {
			int old = interactive_call;
			int ret;

			interactive_call = interactive;
			ret = cmd->func(&arg);
			interactive_call = old;
			return 0;
		}

		fprintf(stderr, "unknown command: %s (try \"help\")\n",
			cmd_text);
		return -1;
	}

	return 0;
}

const char *type_text(option_type_t type)
{
	switch (type) {
	case OPTION_BOOLEAN:
		return "boolean";

	case OPTION_NUMERIC:
		return "numeric";

	case OPTION_TEXT:
		return "text";
	}

	return "unknown";
}

static const char *name_list[128];
static int num_names;
static int name_max_len;

static void name_start(void)
{
	num_names = 0;
	name_max_len = 0;
}

static void name_push(const char *text)
{
	if (num_names < ARRAY_LEN(name_list)) {
		int len = strlen(text);

		name_list[num_names++] = text;
		if (len > name_max_len)
			name_max_len = len;
	}
}

static int compare_name(const void *left, const void *right)
{
	return strcasecmp(*(const char *const *)left,
			  *(const char *const *)right);
}

static void name_list_show(void)
{
	int i;
	int max_len = name_max_len + 2;
	int rows, cols;

	qsort(name_list, num_names, sizeof(name_list[0]),
	      compare_name);

	cols = 72 / max_len;
	rows = (num_names + cols - 1) / cols;

	for (i = 0; i < rows; i++) {
		int j;

		printf("    ");
		for (j = 0; j < cols; j++) {
			int k = j * rows + i;

			if (k >= num_names)
				break;

			printf("%s", name_list[k]);
			for (k = strlen(name_list[k]); k < max_len; k++)
				printf(" ");
		}

		printf("\n");
	}
}

static int cmd_help(char **arg)
{
	const char *topic = get_arg(arg);

	if (topic) {
		const struct command *cmd = find_command(topic);
		const struct option *opt = find_option(topic);

		if (cmd) {
			printf("COMMAND: %s\n", cmd->name);
			fputs(cmd->help, stdout);
			if (opt)
				printf("\n");
		}

		if (opt) {
			printf("OPTION: %s (%s)\n", opt->name,
			       type_text(opt->type));
			fputs(opt->help, stdout);
		}

		if (!(cmd || opt)) {
			fprintf(stderr, "help: unknown command: %s\n", topic);
			return -1;
		}
	} else {
		const struct command *cmd;
		const struct option *opt;

		name_start();
		for (cmd = command_list; cmd; cmd = cmd->next)
			name_push(cmd->name);

		printf("Available commands:\n");
		name_list_show();
		printf("\n");

		name_start();
		for (opt = option_list; opt; opt = opt->next)
			name_push(opt->name);

		printf("Available options:\n");
		name_list_show();
		printf("\n");

		printf("Type \"help <topic>\" for more information.\n");
		printf("Press Ctrl+D to quit.\n");
	}

	return 0;
}

static struct command command_help = {
	.func = cmd_help,
	.name = "help",
	.help =
	"help [command]\n"
	"    Without arguments, displays a list of commands. With a command\n"
	"    name as an argument, displays help for that command.\n"
};

#ifndef USE_READLINE
#define LINE_BUF_SIZE 128

static char *readline(const char *prompt)
{
	char *buf = malloc(LINE_BUF_SIZE);

	if (!buf) {
		perror("readline: can't allocate memory");
		return NULL;
	}

	for (;;) {
		printf("(mspdebug) ");
		fflush(stdout);

		if (fgets(buf, LINE_BUF_SIZE, stdin))
			return buf;

		if (feof(stdin))
			break;

		printf("\n");
	}

	free(buf);
	return NULL;
}

#define add_history(x)
#endif

void reader_loop(void)
{
	printf("\n");
	cmd_help(NULL);
	printf("\n");

	do {
		for (;;) {
			char *buf = readline("(mspdebug) ");

			if (!buf)
				break;

			add_history(buf);
			process_command(buf, 1);
			free(buf);
		}
	} while (modify_prompt(MODIFY_ALL));

	printf("\n");
}

static void display_option(const struct option *o)
{
	printf("%32s = ", o->name);

	switch (o->type) {
	case OPTION_BOOLEAN:
		printf("%s", o->data.numeric ? "true" : "false");
		break;

	case OPTION_NUMERIC:
		printf("0x%x (%d)", o->data.numeric,
		       o->data.numeric);
		break;

	case OPTION_TEXT:
		printf("%s", o->data.text);
		break;
	}

	printf("\n");
}

static int parse_option(struct option *o, const char *word)
{
	switch (o->type) {
	case OPTION_BOOLEAN:
		o->data.numeric = (isdigit(word[0]) && word[0] > '0') ||
			word[0] == 't' || word[0] == 'y' ||
			(word[0] == 'o' && word[1] == 'n');
		break;

	case OPTION_NUMERIC:
		return addr_exp(word, &o->data.numeric);

	case OPTION_TEXT:
		strncpy(o->data.text, word, sizeof(o->data.text));
		o->data.text[sizeof(o->data.text) - 1] = 0;
		break;
	}

	return 0;
}

static int cmd_opt(char **arg)
{
	const char *opt_text = get_arg(arg);
	struct option *opt = NULL;

	if (opt_text) {
		opt = find_option(opt_text);
		if (!opt) {
			fprintf(stderr, "opt: no such option: %s\n",
				opt_text);
			return -1;
		}
	}

	if (**arg) {
		if (parse_option(opt, *arg) < 0) {
			fprintf(stderr, "opt: can't parse option: %s\n",
				*arg);
			return -1;
		}
	} else if (opt_text) {
		display_option(opt);
	} else {
		struct option *o;

		for (o = option_list; o; o = o->next)
			display_option(o);
	}

	return 0;
}

static struct command command_opt = {
	.name = "opt",
	.func = cmd_opt,
	.help =
	"opt [name] [value]\n"
	"    Query or set option variables. With no arguments, displays all\n"
	"    available options.\n"
};

static int cmd_read(char **arg)
{
	char *filename = get_arg(arg);
	FILE *in;
	char buf[1024];

	if (!filename) {
		fprintf(stderr, "read: filename must be specified\n");
		return -1;
	}

	in = fopen(filename, "r");
	if (!in) {
		fprintf(stderr, "read: can't open %s: %s\n",
			filename, strerror(errno));
		return -1;
	}

	while (fgets(buf, sizeof(buf), in)) {
		char *cmd = buf;

		while (*cmd && isspace(*cmd))
			cmd++;

		if (*cmd == '#')
			continue;

		if (process_command(cmd, 0) < 0) {
			fprintf(stderr, "read: error processing %s\n",
				filename);
			fclose(in);
			return -1;
		}
	}

	fclose(in);
	return 0;
}

static struct command command_read = {
	.name = "read",
	.func = cmd_read,
	.help =
	"read <filename>\n"
	"    Read commands from a file and evaluate them.\n"
};

static struct option option_color = {
	.name = "color",
	.type = OPTION_BOOLEAN,
	.help = "Colorize disassembly output.\n"
};

int colorize(const char *text)
{
	if (!option_color.data.numeric)
		return 0;

	return printf("\x1b[%s", text);
}

static volatile int ctrlc_flag;

static void sigint_handler(int signum)
{
	ctrlc_flag = 1;
}

void parse_init(void)
{
	const static struct sigaction siga = {
		.sa_handler = sigint_handler,
		.sa_flags = 0
	};

	sigaction(SIGINT, &siga, NULL);

	register_option(&option_color);

	register_command(&command_help);
	register_command(&command_opt);
	register_command(&command_read);
}

void hexdump(int addr, const u_int8_t *data, int len)
{
	int offset = 0;

	while (offset < len) {
		int i, j;

		/* Address label */
		printf("    %04x:", offset + addr);

		/* Hex portion */
		for (i = 0; i < 16 && offset + i < len; i++)
			printf(" %02x", data[offset + i]);
		for (j = i; j < 16; j++)
			printf("   ");

		/* Printable characters */
		printf(" |");
		for (j = 0; j < i; j++) {
			int c = data[offset + j];

			printf("%c", (c >= 32 && c <= 126) ? c : '.');
		}
		for (; j < 16; j++)
			printf(" ");
		printf("|\n");

		offset += i;
	}
}

/* This table of device IDs is sourced mainly from the MSP430 Memory
 * Programming User's Guide (SLAU265).
 *
 * The table should be kept sorted by device ID.
 */

static struct {
	u_int16_t	id;
	const char	*id_text;
} id_table[] = {
	{0x1132, "F1122"},
	{0x1132, "F1132"},
	{0x1232, "F1222"},
	{0x1232, "F1232"},
	{0xF112, "F11x"},   /* obsolete */
	{0xF112, "F11x1"},  /* obsolete */
	{0xF112, "F11x1A"}, /* obsolete */
	{0xF123, "F122"},
	{0xF123, "F123x"},
	{0xF143, "F14x"},
	{0xF149, "F13x"},
	{0xF149, "F14x1"},
	{0xF149, "F149"},
	{0xF169, "F16x"},
	{0xF16C, "F161x"},
	{0xF201, "F20x3"},
	{0xF213, "F21x1"},
	{0xF227, "F22xx"},
	{0xF249, "F24x"},
	{0xF26F, "F261x"},
	{0xF413, "F41x"},
	{0xF427, "FE42x"},
	{0xF427, "FW42x"},
	{0xF427, "F415"},
	{0xF427, "F417"},
	{0xF427, "F42x0"},
	{0xF439, "FG43x"},
	{0xF449, "F43x"},
	{0xF449, "F44x"},
	{0xF46F, "FG46xx"},
	{0xF46F, "F471xx"}
};

void print_devid(u_int16_t id)
{
	int i = 0;

	while (i < ARRAY_LEN(id_table) && id_table[i].id != id)
		i++;

	if (i < ARRAY_LEN(id_table)) {
		printf("Device: MSP430%s", id_table[i++].id_text);
		while (id_table[i].id == id)
			printf("/MSP430%s", id_table[i++].id_text);
		printf("\n");
	} else {
		printf("Unknown device ID: 0x%04x\n", id);
	}
}

int read_with_timeout(int fd, u_int8_t *data, int max_len)
{
	int r;

	do {
		struct timeval tv = {
			.tv_sec = 5,
			.tv_usec = 0
		};

		fd_set set;

		FD_ZERO(&set);
		FD_SET(fd, &set);

		r = select(fd + 1, &set, NULL, NULL, &tv);
		if (r > 0)
			r = read(fd, data, max_len);

		if (!r)
			errno = ETIMEDOUT;
		if (r <= 0 && errno != EINTR)
			return -1;
	} while (r <= 0);

	return r;
}

int write_all(int fd, const u_int8_t *data, int len)
{
	while (len) {
		int result = write(fd, data, len);

		if (result < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}

		data += result;
		len -= result;
	}

	return 0;
}

int open_serial(const char *device, int rate)
{
	int fd = open(device, O_RDWR | O_NOCTTY);
	struct termios attr;

	tcgetattr(fd, &attr);
	cfmakeraw(&attr);
	cfsetspeed(&attr, rate);

	if (tcsetattr(fd, TCSAFLUSH, &attr) < 0)
		return -1;

	return fd;
}

void ctrlc_reset(void)
{
	ctrlc_flag = 0;
}

int ctrlc_check(void)
{
	return ctrlc_flag;
}

static char token_buf[64];
static int token_len;
static int token_mult;
static int token_sum;

static token_func_t token_func;

static int token_add(void)
{
	int i;
	int value;

	if (!token_len)
		return 0;

	token_buf[token_len] = 0;
	token_len = 0;

	/* Is it a decimal? */
	i = 0;
	while (token_buf[i] && isdigit(token_buf[i]))
		i++;
	if (!token_buf[i]) {
		token_sum += token_mult * atoi(token_buf);
		return 0;
	}

	/* Is it hex? */
	if (token_buf[0] == '0' && tolower(token_buf[1]) == 'x') {
		token_sum += token_mult * strtol(token_buf + 2, NULL, 16);
		return 0;
	}

	/* Look up the name in the symbol table */
	if (token_func && !token_func(token_buf, &value)) {
		token_sum += token_mult * value;
		return 0;
	}

	fprintf(stderr, "unknown token: %s\n", token_buf);
	return -1;
}

void set_token_func(token_func_t func)
{
	token_func = func;
}

int addr_exp(const char *text, int *addr)
{
	token_len = 0;
	token_mult = 1;
	token_sum = 0;

	while (*text) {
		if (isalnum(*text) || *text == '_' || *text == '$' ||
		    *text == '.' || *text == ':') {
			if (token_len + 1 < sizeof(token_buf))
				token_buf[token_len++] = *text;
		} else {
			if (token_add() < 0)
				return -1;
			if (*text == '+')
				token_mult = 1;
			if (*text == '-')
				token_mult = -1;
		}

		text++;
	}

	if (token_add() < 0)
		return -1;

	*addr = token_sum & 0xffff;
	return 0;
}

static int modify_flags;

void modify_set(int flags)
{
	modify_flags |= flags;
}

void modify_clear(int flags)
{
	modify_flags &= ~flags;
}

int modify_prompt(int flags)
{
	char buf[32];

	if (!(interactive_call && (modify_flags & flags)))
		return 0;

	for (;;) {
		printf("Symbols have not been saved since modification. "
		       "Continue (y/n)? ");
		fflush(stdout);

		if (!fgets(buf, sizeof(buf), stdin)) {
			printf("\n");
			return 1;
		}

		if (toupper(buf[0]) == 'Y')
			return 0;
		if (toupper(buf[0]) == 'N')
			return 1;

		printf("Please answer \"y\" or \"n\".\n");
	}

	return 0;
}
