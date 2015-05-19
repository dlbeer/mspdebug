/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2012 Daniel Beer
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
#include <string.h>
#include <ctype.h>

#include "input_async.h"
#include "util.h"
#include "ctrlc.h"
#include "thread.h"

#define MAX_LINE_LENGTH		1024

typedef enum {
	STATE_BLOCKED,
	STATE_RECEIVING,
	STATE_READY,
	STATE_EOF
} mailbox_state_t;

struct mailbox {
	thread_lock_t		lock_ack;
	thread_cond_t		cond_ack;
	int			ack;

	thread_lock_t		lock_text;
	thread_cond_t		cond_text;
	int			text_len;
	int			text_eof;
	char			text[MAX_LINE_LENGTH];
};

static struct mailbox linebox;

static void handle_special(const char *text)
{
	if (!strcmp(text, "break"))
		ctrlc_raise();
}

static void handle_command(const char *text)
{
	int len = strlen(text);

	if (len >= sizeof(linebox.text))
		len = sizeof(linebox.text) - 1;

	/* Deliver the command to the mailbox */
	thread_lock_acquire(&linebox.lock_text);
	memcpy(linebox.text, text, len);
	linebox.text[len] = 0;
	linebox.text_len = len;
	thread_lock_release(&linebox.lock_text);
	thread_cond_notify(&linebox.cond_text);

	/* Wait for ACK */
	thread_lock_acquire(&linebox.lock_ack);
	while (!linebox.ack)
		thread_cond_wait(&linebox.cond_ack, &linebox.lock_ack);
	linebox.ack = 0;
	thread_lock_release(&linebox.lock_ack);
}

static void io_worker(void *thread_arg)
{
	char buf[MAX_LINE_LENGTH];
	(void)thread_arg;

	/* Read commands from stdin and dispatch them */
	while (fgets(buf, sizeof(buf), stdin)) {
		int len = strlen(buf);

		while (len > 0 && isspace(buf[len - 1]))
			len--;
		buf[len] = 0;

		if (buf[0] == '\\')
			handle_special(buf + 1);
		else if (buf[0] == ':')
			handle_command(buf + 1);
		else
			handle_command(buf);
	}

	/* Deliver EOF */
	thread_lock_acquire(&linebox.lock_text);
	linebox.text_eof = 1;
	thread_lock_release(&linebox.lock_text);
	thread_cond_notify(&linebox.cond_text);
}

static int async_init(void)
{
	thread_t thr;

	thread_lock_init(&linebox.lock_text);
	thread_lock_init(&linebox.lock_ack);
	thread_cond_init(&linebox.cond_text);
	thread_cond_init(&linebox.cond_ack);
	linebox.text_len = -1;
	linebox.text_eof = 0;
	linebox.ack = 0;

	if (thread_create(&thr, io_worker, NULL) < 0) {
		fprintf(stderr, "async_init: failed to "
			"start reader thread: %s\n", last_error());
		return -1;
	}

	return 0;
}

static void async_exit(void) { }

static int async_read_command(char *buf, int max_len)
{
	/* Wait for text or EOF */
	thread_lock_acquire(&linebox.lock_text);
	while (!linebox.text_eof && (linebox.text_len < 0))
		thread_cond_wait(&linebox.cond_text, &linebox.lock_text);

	if (linebox.text_eof) {
		thread_lock_release(&linebox.lock_text);
		return 1;
	}

	strncpy(buf, linebox.text, max_len);
	buf[max_len - 1] = 0;
	linebox.text_len = -1;
	thread_lock_release(&linebox.lock_text);

	/* Send ACK */
	thread_lock_acquire(&linebox.lock_ack);
	linebox.ack = 1;
	thread_lock_release(&linebox.lock_ack);
	thread_cond_notify(&linebox.cond_ack);

	return 0;
}

static int async_prompt_abort(const char *message)
{
	(void)message;

	return 0;
}

const struct input_interface input_async = {
	.init		= async_init,
	.exit		= async_exit,
	.read_command	= async_read_command,
	.prompt_abort	= async_prompt_abort
};
