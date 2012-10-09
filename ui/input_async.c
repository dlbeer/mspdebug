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

/* Mailbox state machine works as follows:
 *
 *   BLOCKED: no data is requested by the main thread. The only
 *     transition from this state is to RECEIVING at the request of the
 *     main thread.
 *
 *   RECEIVING: main thread is waiting for data. Reader thread may
 *     change state to either READY or EOF.
 *
 *   READY: a line of text is ready to be picked up from the mailbox.
 *     The only transitions are to either BLOCKED or RECEIVING, at the
 *     request of the main thread.
 *
 *   EOF: end of input. No transitions from this state.
 */

typedef enum {
	STATE_BLOCKED,
	STATE_RECEIVING,
	STATE_READY,
	STATE_EOF
} mailbox_state_t;

struct mailbox {
	thread_lock_t		lock;
	thread_cond_t		cond;
	mailbox_state_t		state;
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
	/* Deliver the command to the mailbox if it's receiving. */
	thread_lock_acquire(&linebox.lock);

	if (linebox.state == STATE_RECEIVING) {
		strncpy(linebox.text, text, sizeof(linebox.text));
		linebox.text[sizeof(linebox.text) - 1] = 0;
		linebox.state = STATE_READY;
	}

	thread_lock_release(&linebox.lock);
	thread_cond_notify(&linebox.cond);
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

	/* Wait for the next read request and deliver EOF */
	thread_lock_acquire(&linebox.lock);
	while (linebox.state != STATE_RECEIVING)
		thread_cond_wait(&linebox.cond, &linebox.lock);
	linebox.state = STATE_EOF;
	thread_lock_release(&linebox.lock);
	thread_cond_notify(&linebox.cond);
}

static int async_init(void)
{
	thread_t thr;

	thread_lock_init(&linebox.lock);
	thread_cond_init(&linebox.cond);
	linebox.state = STATE_BLOCKED;

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
	/* Upon entry to this function, the mailbox is either in the
	 * BLOCKED or EOF state. First, put us into the receiving
	 * state.
	 */
	thread_lock_acquire(&linebox.lock);
	if (linebox.state == STATE_EOF) {
		thread_lock_release(&linebox.lock);
		return 1;
	}

	linebox.state = STATE_RECEIVING;
	thread_lock_release(&linebox.lock);
	thread_cond_notify(&linebox.cond);

	/* Now wait for the reader thread to change the mailbox state to
	 * either READY or EOF.
	 */
	thread_lock_acquire(&linebox.lock);
	while (linebox.state == STATE_RECEIVING)
		thread_cond_wait(&linebox.cond, &linebox.lock);

	if (linebox.state == STATE_EOF) {
		thread_lock_release(&linebox.lock);
		return 1;
	}

	strncpy(buf, linebox.text, max_len);
	buf[max_len - 1] = 0;

	linebox.state = STATE_BLOCKED;
	thread_lock_release(&linebox.lock);
	thread_cond_notify(&linebox.cond);

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
