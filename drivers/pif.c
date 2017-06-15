/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2012 Daniel Beer
 * Copyright (C) 2012-2015 Peter Bägel
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

/* Driver for parallel port interface like the Olimex MSP430-JTAG
 * Starting point was the goodfet driver
 *
 * 2012-10-03 initial release		Peter Bägel (DF5EQ)
 * 2014-12-26 single step implemented   Peter Bägel (DF5EQ)
 * 2015-02-21 breakpoints implemented   Peter Bägel (DF5EQ)
 */

#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "output.h"
#include "pif.h"
#include "jtaglib.h"
#include "ctrlc.h"

struct pif_device {
  struct device		base;
  struct jtdev		jtag;
};

/*============================================================================*/
/* pif MSP430 JTAG operations */

/*----------------------------------------------------------------------------*/
/* Read a word-aligned block from any kind of memory
 * returns the number of bytes read or -1 on failure
 */
static int read_words(device_t dev, const struct chipinfo_memory *m,
		address_t addr, address_t len, uint8_t *data)
{
  struct pif_device *pif = (struct pif_device *)dev;
  struct jtdev *p = &pif->jtag;
  unsigned int index;
  unsigned int word;

  for ( index = 0; index < len; index += 2 ) {
    word = jtag_read_mem( p, 16, addr+index );
    data[index]   =  word       & 0x00ff;
    data[index+1] = (word >> 8) & 0x00ff;
  }

  return p->failed ? -1 : len;
}

/*----------------------------------------------------------------------------*/
/* Write a word to RAM */
static int write_ram_word( struct jtdev *p, address_t addr,
		    uint16_t  value )
{
  jtag_write_mem( p, 16, addr, value );

  return p->failed ? -1 : 0;
}

/*----------------------------------------------------------------------------*/
/* Write a word-aligned flash block. */
/* The starting address must be within the flash memory range. */

static int write_flash_block( struct jtdev *p, address_t addr,
			      address_t len,
			      const uint8_t *data)
{
  unsigned int i;
  uint16_t *word;

  word = malloc( len / 2 * sizeof(*word) );
  if (!word) {
	pr_error("pif: failed to allocate memory");
	return -1;
  }

  for(i = 0; i < len/2; i++) {
    word[i]=data[2*i] + (((uint16_t)data[2*i+1]) << 8);
  }
  jtag_write_flash( p, addr, len/2, word );

  free(word);

  return p->failed ? -1 : 0;
}

/* Write a word-aligned block to any kind of memory.
 * returns the number of bytes written or -1 on failure
 */
static int write_words(device_t dev, const struct chipinfo_memory *m,
		address_t addr, address_t len, const uint8_t *data)
{
  struct pif_device *pif = (struct pif_device *)dev;
  struct jtdev *p = &pif->jtag;
  int r;

  if (m->type != CHIPINFO_MEMTYPE_FLASH) {
    len = 2;
    r = write_ram_word(p, addr, r16le(data));
  } else {
    r = write_flash_block(p, addr, len, data);
  }

  if (r < 0) {
    printc_err("pif: write_words at address 0x%x failed\n", addr);
    return -1;
  }

  return len;
}

/*----------------------------------------------------------------------------*/
static int init_device(struct jtdev *p)
{
  unsigned int jtag_id;

  printc_dbg("Starting JTAG\n");
  jtag_id = jtag_init(p);
  printc("JTAG ID: 0x%02x\n", jtag_id);
  if (jtag_id != 0x89 && jtag_id != 0x91) {
    printc_err("pif: unexpected JTAG ID: 0x%02x\n", jtag_id);
    jtag_release_device(p, 0xfffe);
    return -1;
  }

  return 0;
}

/*===== MSPDebug Device interface ============================================*/

/*----------------------------------------------------------------------------*/
static int refresh_bps(struct pif_device *dev)
{
	int i;
	int ret;
	struct device_breakpoint *bp;
	address_t addr;
	ret = 0;

	for (i = 0; i < dev->base.max_breakpoints; i++) {
		bp = &dev->base.breakpoints[i];

		printc_dbg("refresh breakpoint %d: type=%d "
			   "addr=%04x flags=%04x\n",
			   i, bp->type, bp->addr, bp->flags);

		if ( (bp->flags &  DEVICE_BP_DIRTY) &&
		     (bp->type  == DEVICE_BPTYPE_BREAK) ) {
			addr = bp->addr;

			if ( !(bp->flags & DEVICE_BP_ENABLED) ) {
				addr = 0;
			}

			if ( jtag_set_breakpoint (&dev->jtag, i, addr) == 0) {
				printc_err("pif: failed to refresh "
					   "breakpoint #%d\n", i);
				ret = -1;
			} else {
				bp->flags &= ~DEVICE_BP_DIRTY;
			}
		}
	}

	return ret;
}

/*----------------------------------------------------------------------------*/
static int pif_readmem( device_t  dev_base,
			address_t addr,
			uint8_t*  mem,
			address_t len )
{
  struct pif_device *dev = (struct pif_device *)dev_base;
  dev->jtag.failed = 0;
  return readmem(dev_base, addr, mem, len, read_words);
}

/*----------------------------------------------------------------------------*/
static int pif_writemem( device_t       dev_base,
			 address_t      addr,
			 const uint8_t* mem,
			 address_t      len )
{
  struct pif_device *dev = (struct pif_device *)dev_base;
  dev->jtag.failed = 0;
  return writemem(dev_base, addr, mem, len, write_words, read_words);
}

/*----------------------------------------------------------------------------*/
static int pif_getregs(device_t dev_base, address_t *regs)
{
  struct pif_device *dev = (struct pif_device *)dev_base;
  int i;

  dev->jtag.failed = 0;

  for (i = 0; i < DEVICE_NUM_REGS; i++)
    regs[i] = jtag_read_reg(&dev->jtag, i);

  return dev->jtag.failed ? -1 : 0;
}

/*----------------------------------------------------------------------------*/
static int pif_setregs( device_t dev_base, const address_t* regs )
{
  struct pif_device *dev = (struct pif_device *)dev_base;
  int i;

  dev->jtag.failed = 0;

  for (i = 0; i < DEVICE_NUM_REGS; i++) {
    jtag_write_reg( &dev->jtag, i, regs[i] );
  }
  return dev->jtag.failed ? -1 : 0;
}

/*----------------------------------------------------------------------------*/
static int pif_ctl(device_t dev_base, device_ctl_t type)
{
  struct pif_device *dev = (struct pif_device *)dev_base;

  dev->jtag.failed = 0;

  switch (type) {
    case DEVICE_CTL_RESET:
      /* perform soft reset */
      jtag_execute_puc(&dev->jtag);
      break;

    case DEVICE_CTL_RUN:
      /* transfer changed breakpoints to device */
      if (refresh_bps(dev) < 0) {
	return -1;
      }
      /* start program execution at current PC */
      jtag_release_device(&dev->jtag, 0xffff);
      break;

    case DEVICE_CTL_HALT:
      /* take device under JTAG control */
      jtag_get_device(&dev->jtag);
      break;

    case DEVICE_CTL_STEP:
      /* execute next instruction at current PC */
      jtag_single_step(&dev->jtag);
      break;

    default:
      printc_err("pif: unsupported operation\n");
      return -1;
  }

  return dev->jtag.failed ? -1 : 0;
}

/*----------------------------------------------------------------------------*/
static device_status_t pif_poll(device_t dev_base)
{
  struct pif_device *dev = (struct pif_device *)dev_base;

  if (delay_ms(100) < 0 || ctrlc_check())
    return DEVICE_STATUS_INTR;

  if (jtag_cpu_state(&dev->jtag) == 1) {
    return DEVICE_STATUS_HALTED;
  }

  return DEVICE_STATUS_RUNNING;
}

/*----------------------------------------------------------------------------*/
static int pif_erase( device_t dev_base,
		      device_erase_type_t type,
		      address_t addr )
{
  struct pif_device *dev = (struct pif_device *)dev_base;

  dev->jtag.failed = 0;

  switch(type) {
    case DEVICE_ERASE_MAIN:
      jtag_erase_flash ( &dev->jtag, JTAG_ERASE_MAIN, addr );
      break;
    case DEVICE_ERASE_ALL:
      jtag_erase_flash ( &dev->jtag, JTAG_ERASE_MASS, addr );
      break;
    case DEVICE_ERASE_SEGMENT:
      jtag_erase_flash ( &dev->jtag, JTAG_ERASE_SGMT, addr );
      break;
    default:
      return -1;
  }

  return dev->jtag.failed ? -1 : 0;
}

/*----------------------------------------------------------------------------*/
static int pif_getconfigfuses(device_t dev_base)
{
  struct pif_device *dev = (struct pif_device *)dev_base;

  return jtag_get_config_fuses(&dev->jtag);
}

/*----------------------------------------------------------------------------*/


static device_t pif_open(const struct device_args *args)
{
  struct pif_device *dev;

  if (!(args->flags & DEVICE_FLAG_TTY)) {
    printc_err("pif: this driver does not support raw USB access\n");
    return NULL;
  }

  if (!(args->flags & DEVICE_FLAG_JTAG)) {
    printc_err("pif: this driver does not support Spy-Bi-Wire\n");
    return NULL;
  }

  dev = malloc(sizeof(*dev));
  if (!dev) {
    printc_err("pif: malloc: %s\n", last_error());
    return NULL;
  }

  memset(dev, 0, sizeof(*dev));
  dev->base.type = &device_pif;
  dev->base.max_breakpoints = 2; //supported by all devices
  dev->base.need_probe = 1;
  (&dev->jtag)->f = &jtdev_func_pif;

  if ((&dev->jtag)->f->jtdev_open(&dev->jtag, args->path) < 0) {
    printc_err("pif: can't open port\n");
    free(dev);
    return NULL;
  }

  if (init_device(&dev->jtag) < 0) {
    printc_err("pif: initialization failed\n");
    free(dev);
    return NULL;
  }

  return &dev->base;
}

/*----------------------------------------------------------------------------*/


static device_t gpio_open(const struct device_args *args)
{
  struct pif_device *dev;

  if (!(args->flags & DEVICE_FLAG_TTY)) {
    printc_err("gpio: this driver does not support raw USB access\n");
    return NULL;
  }

  if (!(args->flags & DEVICE_FLAG_JTAG)) {
    printc_err("gpio: this driver does not support Spy-Bi-Wire\n");
    return NULL;
  }

  dev = malloc(sizeof(*dev));
  if (!dev) {
    printc_err("gpio: malloc: %s\n", last_error());
    return NULL;
  }

  memset(dev, 0, sizeof(*dev));
  dev->base.type = &device_pif;
  dev->base.max_breakpoints = 0;
  (&dev->jtag)->f = &jtdev_func_gpio;

  if ((&dev->jtag)->f->jtdev_open(&dev->jtag, args->path) < 0) {
    printc_err("gpio: can't open port\n");
    free(dev);
    return NULL;
  }

  if (init_device(&dev->jtag) < 0) {
    printc_err("gpio: initialization failed\n");
    free(dev);
    return NULL;
  }

  return &dev->base;
}

/*----------------------------------------------------------------------------*/


static device_t bp_open(const struct device_args *args)
{
  struct pif_device *dev;

  if (!(args->flags & DEVICE_FLAG_TTY)) {
    printc_err("bp: this driver does not support raw USB access\n");
    return NULL;
  }

  if (!(args->flags & DEVICE_FLAG_JTAG)) {
    printc_err("bp: this driver does not support Spy-Bi-Wire\n");
    return NULL;
  }

  dev = malloc(sizeof(*dev));
  if (!dev) {
    printc_err("bp: malloc: %s\n", last_error());
    return NULL;
  }

  memset(dev, 0, sizeof(*dev));
  dev->base.type = &device_pif;
  dev->base.max_breakpoints = 2; //supported by all devices
  dev->base.need_probe = 1;
  (&dev->jtag)->f = &jtdev_func_bp;

  if ((&dev->jtag)->f->jtdev_open(&dev->jtag, args->path) < 0) {
    printc_err("bp: can't open port\n");
    free(dev);
    return NULL;
  }

  if (init_device(&dev->jtag) < 0) {
    printc_err("bp: initialization failed\n");
    free(dev);
    return NULL;
  }

  return &dev->base;
}

/*----------------------------------------------------------------------------*/
static void pif_destroy(device_t dev_base)
{
  struct pif_device *dev = (struct pif_device *)dev_base;

  dev->jtag.failed = 0;

  jtag_release_device(&dev->jtag, 0xfffe);
  (&dev->jtag)->f->jtdev_close(&dev->jtag);
  free(dev);
}

/*----------------------------------------------------------------------------*/
const struct device_class device_pif = {
  .name     = "pif",
  .help     = "Parallel Port JTAG",
  .open     = pif_open,
  .destroy  = pif_destroy,
  .readmem  = pif_readmem,
  .writemem = pif_writemem,
  .getregs  = pif_getregs,
  .setregs  = pif_setregs,
  .ctl      = pif_ctl,
  .poll     = pif_poll,
  .erase    = pif_erase,
  .getconfigfuses = pif_getconfigfuses
};

const struct device_class device_gpio = {
  .name     = "gpio",
  .help     = "/sys/class/gpio direct connect",
  .open     = gpio_open,
  .destroy  = pif_destroy,
  .readmem  = pif_readmem,
  .writemem = pif_writemem,
  .getregs  = pif_getregs,
  .setregs  = pif_setregs,
  .ctl      = pif_ctl,
  .poll     = pif_poll,
  .erase    = pif_erase,
  .getconfigfuses = pif_getconfigfuses
};

const struct device_class device_bp = {
  .name     = "bus-pirate",
  .help     = "Bus Pirate JTAG, MISO-TDO, MOSI-TDI, CS-TMS, AUX-RESET, CLK-TCK",
  .open     = bp_open,
  .destroy  = pif_destroy,
  .readmem  = pif_readmem,
  .writemem = pif_writemem,
  .getregs  = pif_getregs,
  .setregs  = pif_setregs,
  .ctl      = pif_ctl,
  .poll     = pif_poll,
  .erase    = pif_erase,
  .getconfigfuses = pif_getconfigfuses
};
