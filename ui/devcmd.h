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

#ifndef DEVCMD_H_
#define DEVCMD_H_

int cmd_regs(char **arg);
int cmd_md(char **arg);
int cmd_mw(char **arg);
int cmd_reset(char **arg);
int cmd_erase(char **arg);
int cmd_step(char **arg);
int cmd_run(char **arg);
int cmd_set(char **arg);
int cmd_dis(char **arg);
int cmd_hexout(char **arg);
int cmd_prog(char **arg);
int cmd_load(char **arg);
int cmd_verify(char **arg);
int cmd_setbreak(char **arg);
int cmd_setwatch(char **arg);
int cmd_setwatch_r(char **arg);
int cmd_setwatch_w(char **arg);
int cmd_delbreak(char **arg);
int cmd_break(char **arg);
int cmd_fill(char **arg);
int cmd_blow_jtag_fuse(char **arg);

#endif
