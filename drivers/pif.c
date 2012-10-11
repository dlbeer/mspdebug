/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009-2012 Daniel Beer
 * Copyright (C) 2012 Peter Bägel
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
 * 2012-10-03 Peter Bägel (DF5EQ)
 */

#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "output.h"
#include "pif.h"
#include "jtaglib.h"
#include "ctrlc.h"

/*============================================================================*/
/* pif MSP430 JTAG operations */

/*----------------------------------------------------------------------------*/
/* Read a word-aligned block from any kind of memory */
static int read_words( struct jtdev *p, address_t addr,
		       address_t len,
		       uint8_t*  data )
{
  unsigned int index;
  unsigned int word;

  for ( index = 0; index < len; index += 2 ) {
    word = jtag_read_mem( p, 16, addr+index );
    data[index]   =  word       & 0x00ff;
    data[index+1] = (word >> 8) & 0x00ff;
  }

  return p->failed ? -1 : 0;
}

/*----------------------------------------------------------------------------*/
/* Write a word to RAM */
int write_ram_word( struct jtdev *p, address_t addr,
		    uint16_t  value )
{
  unsigned int word;

  word = ((value & 0x00ff) << 8) | ((value & 0xff00) >> 8);
  jtag_write_mem( p, 16, addr, word );

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

/*----------------------------------------------------------------------------*/
/* Write a single byte by reading and rewriting a word. */
static int write_byte( struct jtdev *p,
		       address_t addr,
		       uint8_t   value )
{
  address_t aligned = addr & ~1;
  uint8_t data[2];
  unsigned int word;

  read_words(p, aligned, 2, data);
  data[addr & 1] = value;

  if ( (addr >= 0x1000 && addr <= 0x10ff)
       ||
       addr >= 0x4000) {
    /* program in FLASH */
    write_flash_block(p, aligned, 2, data);
  } else {
    /* write to RAM */
    word = (uint16_t)data[1] | ((uint16_t)data[0] << 8);
    write_ram_word(p, aligned, word);
  }

  return p->failed ? -1 : 0;
}

/*----------------------------------------------------------------------------*/
static int init_device(struct jtdev *p)
{
  unsigned int jtag_id;
  unsigned int chip_id;

  printc_dbg("Starting JTAG\n");
  jtag_id = jtag_init(p);
  printc("JTAG ID: 0x%02x\n", jtag_id);
  if (jtag_id != 0x89 && jtag_id != 0x91) {
    printc_err("pif: unexpected JTAG ID: 0x%02x\n", jtag_id);
    jtag_release_device(p, 0xfffe);
    return -1;
  }

  chip_id =jtag_chip_id(p);
  printc_dbg("Chip ID: %04X\n", chip_id);

  return 0;
}

/*===== MSPDebug Device interface ============================================*/

struct pif_device {
  struct device		base;
  struct jtdev		jtag;
};

/*----------------------------------------------------------------------------*/
static int pif_readmem( device_t  dev_base,
			address_t addr,
			uint8_t*  mem,
			address_t len )
{
  struct pif_device *dev = (struct pif_device *)dev_base;
  uint8_t  data[2];

  dev->jtag.failed = 0;

  if ( len > 0 ) {
    /* Handle unaligned start */
    if (addr & 1) {
      if (read_words(&dev->jtag, addr & ~1, 2, data) < 0)
	return -1;
      mem[0] = data[1];
      addr++;
      mem++;
      len--;
    }

    /* Read aligned blocks */
    if (len >= 2) {
      if (read_words(&dev->jtag, addr, len & ~1, mem) < 0)
	return -1;
      addr += len & ~1;
      mem += len & ~1;
      len &= 1;
    }

    /* Handle unaligned end */
    if (len == 1) {
      if (read_words(&dev->jtag, addr, 2, data) < 0)
	return -1;
      mem[0] = data[0];
    }
  }
  return 0;
}

/*----------------------------------------------------------------------------*/
static int pif_writemem( device_t       dev_base,
			 address_t      addr,
			 const uint8_t* mem,
			 address_t      len )
{
  struct pif_device *dev = (struct pif_device *)dev_base;

  dev->jtag.failed = 0;

  if (len > 0)
  {
    /* Handle unaligned start */
    if (addr & 1) {
      if (write_byte(&dev->jtag, addr, mem[0]) < 0)
	return -1;
      addr++;
      mem++;
      len--;
    }

    /* Write aligned blocks */
    while (len >= 2) {
      if ( (addr >= 0x1000 && addr <= 0x10ff)
	   ||
	   addr >= 0x4000) {
	if (write_flash_block(&dev->jtag, addr, len & ~1, mem) < 0)
	  return -1;
	addr += len & ~1;
	mem += len & ~1;
	len &= 1;
      } else {
	if (write_ram_word(&dev->jtag, addr,
		(uint16_t)mem[1] | ((uint16_t)mem[0] << 8)) < 0)
	  return -1;
	addr += 2;
	mem  += 2;
	len  -= 2;
      }
    }

    /* Handle unaligned end */
    if (len == 1) {
      if (write_byte(&dev->jtag, addr, mem[0]) < 0)
	return -1;
    }
  }
  return 0;
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
      /* start program execution at current PC */
      jtag_release_device(&dev->jtag, 0xffff);
      break;

    case DEVICE_CTL_HALT:
      /* take device under JTAG control */
      jtag_get_device(&dev->jtag);
      break;

    case DEVICE_CTL_STEP:
      printc_err("pif: single-stepping not implemented\n");
      return -1;
  }

  return dev->jtag.failed ? -1 : 0;
}

/*----------------------------------------------------------------------------*/
static device_status_t pif_poll(device_t dev_base)
{
  if (delay_ms(100) < 0 || ctrlc_check())
    return DEVICE_STATUS_INTR;

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
  dev->base.max_breakpoints = 0;

  if (jtdev_open(&dev->jtag, args->path) < 0) {
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
static void pif_destroy(device_t dev_base)
{
  struct pif_device *dev = (struct pif_device *)dev_base;

  dev->jtag.failed = 0;

  jtag_release_device(&dev->jtag, 0xfffe);
  jtdev_close(&dev->jtag);
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
  .erase    = pif_erase
};
