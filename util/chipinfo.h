/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2013 Daniel Beer
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

#ifndef CHIPINFO_H_
#define CHIPINFO_H_

#include <stdint.h>

struct chipinfo_funclet {
	uint16_t	code_size;
	uint16_t	max_payload;
	uint16_t	entry_point;
	uint16_t	code[512];
};

typedef enum {
	CHIPINFO_PSA_REGULAR,
	CHIPINFO_PSA_ENHANCED
} chipinfo_psa_t;

struct chipinfo_id {
	uint16_t	ver_id;
	uint16_t	ver_sub_id;
	uint8_t		revision;
	uint8_t		fab;
	uint16_t	self;
	uint8_t		config;
	uint8_t		fuses;
	uint32_t	activation_key;
};

struct chipinfo_eem {
	uint8_t		state_storage;
	uint8_t		cycle_counter;
	uint8_t		cycle_counter_ops;
	uint8_t		trig_emulation_level;
	uint8_t		trig_mem;
	uint8_t		trig_reg;
	uint8_t		trig_combinations;
	uint8_t		trig_options;
	uint8_t		trig_dma;
	uint8_t		trig_read_write;
	uint8_t		trig_reg_ops;
	uint8_t		trig_comp_level;
	uint8_t		trig_mem_cond_level;
	uint8_t		trig_mem_umask_level;
	uint8_t		seq_states;
	uint8_t		seq_start;
	uint8_t		seq_end;
	uint8_t		seq_reset;
	uint8_t		seq_blocked;
};

struct chipinfo_voltage {
	uint16_t	vcc_min;
	uint16_t	vcc_max;
	uint16_t	vcc_flash_min;
	uint16_t	vcc_secure_min;
	uint16_t	vpp_secure_min;
	uint16_t	vpp_secure_max;
	uint8_t		has_test_vpp;
};

struct chipinfo_power {
	uint32_t	reg_mask;
	uint32_t	enable_lpm5;
	uint32_t	disable_lpm5;
	uint32_t	reg_mask_3v;
	uint32_t	enable_lpm5_3v;
	uint32_t	disable_lpm5_3v;
};

typedef enum {
	CHIPINFO_CLOCK_SYS_BC_1XX,
	CHIPINFO_CLOCK_SYS_BC_2XX,
	CHIPINFO_CLOCK_SYS_FLL_PLUS,
	CHIPINFO_CLOCK_SYS_MOD_OSC
} chipinfo_clock_sys_t;

typedef enum {
	CHIPINFO_FEATURE_I2C			= 0x0001,
	CHIPINFO_FEATURE_LCFE			= 0x0002,
	CHIPINFO_FEATURE_QUICK_MEM_READ		= 0x0004,
	CHIPINFO_FEATURE_SFLLDH			= 0x0008,
	CHIPINFO_FEATURE_FRAM			= 0x0010,
	CHIPINFO_FEATURE_NO_BSL			= 0x0020,
	CHIPINFO_FEATURE_TMR			= 0x0040,
	CHIPINFO_FEATURE_JTAG			= 0x0080,
	CHIPINFO_FEATURE_DTC			= 0x0100,
	CHIPINFO_FEATURE_SYNC			= 0x0200,
	CHIPINFO_FEATURE_INSTR			= 0x0400,
	CHIPINFO_FEATURE_1337			= 0x0800,
	CHIPINFO_FEATURE_PSACH			= 0x1000
} chipinfo_features_t;

typedef enum {
	CHIPINFO_MEMTYPE_ROM,
	CHIPINFO_MEMTYPE_RAM,
	CHIPINFO_MEMTYPE_FLASH,
	CHIPINFO_MEMTYPE_REGISTER
} chipinfo_memtype_t;

struct chipinfo_memory {
	const char		*name;
	chipinfo_memtype_t	type;
	unsigned int		bits;
	unsigned int		mapped;
	unsigned int		size;
	uint32_t		offset;
	unsigned int		seg_size;
	unsigned int		bank_size;
	unsigned int		banks;
};

struct chipinfo_clockmap {
	const char		*name;
	uint8_t			value;
};

struct chipinfo {
	const char			*name;

	unsigned int			bits;
	chipinfo_psa_t			psa;
	uint8_t				clock_control;
	uint16_t			mclk_control;
	chipinfo_clock_sys_t		clock_sys;
	chipinfo_features_t		features;

	struct chipinfo_id		id;
	struct chipinfo_id		id_mask;
	struct chipinfo_eem		eem;
	struct chipinfo_voltage		voltage;
	struct chipinfo_power		power;
	struct chipinfo_memory		memory[16];
	struct chipinfo_clockmap	clock_map[32];

	uint8_t				v3_functions[128];
	const struct chipinfo_funclet	*v3_erase;
	const struct chipinfo_funclet	*v3_write;
	const struct chipinfo_funclet	*v3_unlock;
};

extern const struct chipinfo chipinfo_db[];

const struct chipinfo *chipinfo_find_by_id(const struct chipinfo_id *id);
const struct chipinfo *chipinfo_find_by_name(const char *name);

const struct chipinfo_memory *chipinfo_find_mem_by_addr
	(const struct chipinfo *info, uint32_t offset);
const struct chipinfo_memory *chipinfo_find_mem_by_name
	(const struct chipinfo *info, const char *name);

const char *chipinfo_copyright(void);

#endif
