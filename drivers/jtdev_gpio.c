/* MSPDebug - gpio device interface
 *  Copyright (C) 2014 TTI GmbH - TGU Smartmote
 *  Author(s): Jan Willeke (willeke@smartmote.de)
 *
 * Linux /sys/class/gpio interface to msp430 jtag
 * inspired by urjtag and jtdev.c
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

#include <stdint.h>
#include "jtdev.h"
#include "output.h"

#if defined(__linux__) || \
    ( defined(__FreeBSD__) || defined(__DragonFly__) )
/*===== includes =============================================================*/

#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include "gpio.h"

/* pin mapping */
enum {
    GPIO_TDI = 0,
    GPIO_TCK,
    GPIO_TMS,
    GPIO_TDO,
    GPIO_RST,
    GPIO_TST,
    GPIO_REQUIRED
};

unsigned int jtag_gpios[6];
int          fd_gpios[6];

static int
gpio_open ()
{
    int i, ret;

    /* Export all gpios */
    for (i = 0; i < GPIO_REQUIRED; i++)
    {
        unsigned int gpio = jtag_gpios[i];

        ret = gpio_export (gpio);
        if (ret)
        {
	    printf("gpio[%d] %u cannot be exported\n", i, gpio);
            return -1;
        }
        gpio_set_dir (gpio, (i == GPIO_TDO) ? 0 : 1);

        fd_gpios[i] = gpio_open_fd(gpio);
        if (fd_gpios[i] < 0)
        {
            printf("gpio: cannot open gpio[%d] %u\n", i, gpio);
            return -1;
        }
    }
    return 0;
}

static int
gpio_parse_config (const char *params)
{
    struct option{
      char* name;
      int  num;
	};
    static const struct option ops[] = {
      {"tms=",GPIO_TMS},
      {"tdi=",GPIO_TDI},
      {"tdo=",GPIO_TDO},
      {"tck=",GPIO_TCK},
      {"rst=",GPIO_RST},
      {"tst=",GPIO_TST}
    };
    int i;

    for( i = 0;i < 6; i++) {
      char* help;
      help = strstr(params,ops[i].name);
      if (help)
        jtag_gpios[ops[i].num] = atoi(help+4);
      else
	return -1;
      printf("gpio %s %d\n", ops[i].name,jtag_gpios[ops[i].num]);
    }
    return 0;
}


static int jtgpio_open(struct jtdev *p, const char *device)
{
  if (gpio_parse_config(device)){
    printf("gpio: failed parsing parameters\n");
    return -1;
  }
  return gpio_open();
}

static void jtgpio_close(struct jtdev *p)
{
  int i;
  printf("JTAG_CLOSE\n");

  for (i = 0; i < GPIO_REQUIRED; i++)
    {
      if (fd_gpios[i])
	close (fd_gpios[i]);
      gpio_unexport (jtag_gpios[i]);
    }

}

static void jtgpio_power_on(struct jtdev *p)
{
  printf("JTAG_power on\n");
}

static void jtgpio_power_off(struct jtdev *p)
{
  printf("JTAG_power off\n");
}

static void jtgpio_connect(struct jtdev *p)
{
  printf("JTAG_connct \n");
}

static void jtgpio_release(struct jtdev *p)
{
  printf("JTAG_release\n");
}

static void jtgpio_tck(struct jtdev *p, int out)
{
  gpio_set_value_fd (fd_gpios[GPIO_TCK], out);
}

static void jtgpio_tms(struct jtdev *p, int out)
{
  gpio_set_value_fd (fd_gpios[GPIO_TMS], out);
}

static void jtgpio_tdi(struct jtdev *p, int out)
{
  gpio_set_value_fd (fd_gpios[GPIO_TDI], out);
}

static void jtgpio_rst(struct jtdev *p, int out)
{
  gpio_set_value_fd (fd_gpios[GPIO_RST], out);
}

static void jtgpio_tst(struct jtdev *p, int out)
{
  gpio_set_value_fd (fd_gpios[GPIO_TST], out);
}

static int jtgpio_tdo_get(struct jtdev *p)
{
  return gpio_get_value_fd (fd_gpios[GPIO_TDO],
		  jtag_gpios[GPIO_TDO]);
}

static void jtgpio_tclk(struct jtdev *p, int out)
{
   gpio_set_value_fd (fd_gpios[GPIO_TDI], out);
}

static int jtgpio_tclk_get(struct jtdev *p)
{
  return gpio_get_value_fd (fd_gpios[GPIO_TDI],
			 jtag_gpios[GPIO_TDI]);
}

static void jtgpio_tclk_strobe(struct jtdev *p, unsigned int count)
{
  int i;
  for (i=0;i<count;i++){
    gpio_set_value_fd (fd_gpios[GPIO_TDI], 1);
    gpio_set_value_fd (fd_gpios[GPIO_TDI], 0);
  }
}

static void jtgpio_led_green(struct jtdev *p, int out)
{
  printf("led green\n");
}

static void jtgpio_led_red(struct jtdev *p, int out)
{
  printf("led red\n");
}


#else /* __linux__ */


static int jtgpio_open(struct jtdev *p, const char *device)
{
	printc_err("jtdev: driver is not supported on this platform\n");
	p->failed = 1;
	return -1;
}

static void jtgpio_close(struct jtdev *p) { }

static void jtgpio_power_on(struct jtdev *p) { }
static void jtgpio_power_off(struct jtdev *p) { }
static void jtgpio_connect(struct jtdev *p) { }
static void jtgpio_release(struct jtdev *p) { }

static void jtgpio_tck(struct jtdev *p, int out) { }
static void jtgpio_tms(struct jtdev *p, int out) { }
static void jtgpio_tdi(struct jtdev *p, int out) { }
static void jtgpio_rst(struct jtdev *p, int out) { }
static void jtgpio_tst(struct jtdev *p, int out) { }
static int jtgpio_tdo_get(struct jtdev *p) { return 0; }

static void jtgpio_tclk(struct jtdev *p, int out) { }
static int jtgpio_tclk_get(struct jtdev *p) { return 0; }
static void jtgpio_tclk_strobe(struct jtdev *p, unsigned int count) { }

static void jtgpio_led_green(struct jtdev *p, int out) { }
static void jtgpio_led_red(struct jtdev *p, int out) { }
#endif


const struct jtdev_func jtdev_func_gpio = {
  .jtdev_open        = jtgpio_open,
  .jtdev_close       = jtgpio_close,
  .jtdev_power_on    = jtgpio_power_on,
  .jtdev_power_off   = jtgpio_power_off,
  .jtdev_connect     = jtgpio_connect,
  .jtdev_release     = jtgpio_release,
  .jtdev_tck	     = jtgpio_tck,
  .jtdev_tms	     = jtgpio_tms,
  .jtdev_tdi	     = jtgpio_tdi,
  .jtdev_rst	     = jtgpio_rst,
  .jtdev_tst	     = jtgpio_tst,
  .jtdev_tdo_get     = jtgpio_tdo_get,
  .jtdev_tclk	     = jtgpio_tclk,
  .jtdev_tclk_get    = jtgpio_tclk_get,
  .jtdev_tclk_strobe = jtgpio_tclk_strobe,
  .jtdev_led_green   = jtgpio_led_green,
  .jtdev_led_red     = jtgpio_led_red
};
