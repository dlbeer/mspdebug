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
#include "list.h"

void list_init(struct list_node *head)
{
	head->next = head;
	head->prev = head;
}

void list_insert(struct list_node *item, struct list_node *after)
{
	item->next = after;
	item->prev = after->prev;

	after->prev->next = item;
	after->prev = item;
}

void list_remove(struct list_node *item)
{
	item->next->prev = item->prev;
	item->prev->next = item->next;

	item->prev = NULL;
	item->next = NULL;
}
