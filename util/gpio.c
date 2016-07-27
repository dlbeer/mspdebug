/*
*  C Implementation: gpio
*
* Description: 
*
*
* Author: Paolo Zebelloni <p.zebelloni@c-labs-wt.com>, (C) 2014
*
* Copyright: See COPYING file that comes with this distribution
*
*/

#include "gpio.h"
#include "output.h"

#ifdef __Windows__
/* Added for building on Windows
 *
 * Daniel Beer <dlbeer@gmail.com>, 3 Mar 2015
 */
int gpio_is_exported ( unsigned int gpio )
{
	printc_err("gpio: GPIO interface not supported on Windows\n");
	return -1;
}

int gpio_export ( unsigned int gpio )
{
	printc_err("gpio: GPIO interface not supported on Windows\n");
	return -1;
}

int gpio_unexport ( unsigned int gpio )
{
	printc_err("gpio: GPIO interface not supported on Windows\n");
	return -1;
}

int gpio_set_dir ( unsigned int gpio, unsigned int out_flag )
{
	printc_err("gpio: GPIO interface not supported on Windows\n");
	return -1;
}

int gpio_set_value ( unsigned int gpio, unsigned int value )
{
	printc_err("gpio: GPIO interface not supported on Windows\n");
	return -1;
}

int gpio_set_value_fd (int fd, int value)
{
	printc_err("gpio: GPIO interface not supported on Windows\n");
	return -1;
}

int gpio_get_value ( unsigned int gpio )
{
	return 0;
}

int gpio_get_value_fd (int fd, unsigned int gpio)
{
	return 0;
}

int gpio_open_fd (unsigned int gpio)
{
	printc_err("gpio: GPIO interface not supported on Windows\n");
	return -1;
}

#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define SYSFS_GPIO_DIR		"/sys/class/gpio"
#define MAX_BUF 64

/**
 * @return 1 if the gpio is already exported, 0 otherwise, -1 on error
 */
int gpio_is_exported ( unsigned int gpio )
{
	char dir_name[100] = {};
	snprintf(dir_name, sizeof(dir_name) - 1, SYSFS_GPIO_DIR "/gpio%d", gpio);

	struct stat s;
	int err = stat(dir_name, &s);
	if(-1 == err)
	{
		if(errno == ENOENT)
		{
			return 0;
		} else {
			return -1;
		}
	} else {
		if(S_ISDIR(s.st_mode))
		{
			return 1;
		} else {
			return -1;
		}
	}

	return -1;
}

/**
 * Before a Linux application can configure and use a GPIO, the GPIO first has to be exported to user.
 * Each GPIO is not accessible from user space until the GPIO has been exported. 
 * You can only export a GPIO that isn't owned by a Linux kernel driver 
 * @param gpio GPIO number
 * @return 0 if OK, -1 if fails
 */
int gpio_export ( unsigned int gpio )
{
	int fd, len;
	char buf[MAX_BUF];

	fd = open ( SYSFS_GPIO_DIR "/export", O_WRONLY );
	if ( fd < 0 )
	{
		pr_error ( "gpio/export" );
		return -1;
	}

	len = snprintf ( buf, sizeof ( buf ), "%d", gpio );
	write ( fd, buf, len );
	close ( fd );

	return 0;
}

/**
 * Complement of gpio_export().
 * @param gpio  GPIO number
 * @return 0 if OK, -1 if fails
 */
int gpio_unexport ( unsigned int gpio )
{
	int fd, len;
	char buf[MAX_BUF];

	fd = open ( SYSFS_GPIO_DIR "/unexport", O_WRONLY );
	if ( fd < 0 )
	{
		pr_error ( "gpio/unexport" );
		return -1;
	}

	len = snprintf ( buf, sizeof ( buf ), "%d", gpio );
	write ( fd, buf, len );
	close ( fd );

	return 0;
}

/**
 * To avoid hardware issues where two devices are driving the same signal, GPIOs default to be configured as an input.
 * If you want to use the GPIO as an output, you need to change the configuration 
 * @param gpio GPIO number
 * @param out_flag TRUE means OUT, FALSE means IN
 * @return 0 if OK, -1 if fails
 */
int gpio_set_dir ( unsigned int gpio, unsigned int out_flag )
{
	int fd;
	char buf[MAX_BUF];

	snprintf ( buf, MAX_BUF, SYSFS_GPIO_DIR  "/gpio%d/direction", gpio );

	fd = open ( buf, O_WRONLY );
	if ( fd < 0 )
	{
		pr_error ( "gpio/direction" );
		return -1;
	}

	if ( out_flag )
		write ( fd, "out", 4 );
	else
		write ( fd, "in", 3 );

	close ( fd );

	return 0;
}

/**
 * Set HI or LO state to GPIO (output) pin.
 * @param gpio GPIO number
 * @param value TRUE means HI, FALSE means LO
 * @return 0 if OK, -1 if fails
 */
int gpio_set_value ( unsigned int gpio, unsigned int value )
{
	int fd;
	char buf[MAX_BUF];

	snprintf ( buf, MAX_BUF, SYSFS_GPIO_DIR "/gpio%d/value", gpio );

	fd = open ( buf, O_WRONLY );
	if ( fd < 0 )
	{
		pr_error ( "gpio/set-value" );
		return -1;
	}

	write ( fd, value ? "1" : "0", 2 );

	close ( fd );

	return 0;
}

/**
 * Read state of GPIO (input) pin.
 * @param gpio GPIO number
 * @return 0 if LO, 1 if HI, -1 if fails
 */
int gpio_get_value ( unsigned int gpio )
{
	int fd;
	char buf[MAX_BUF];
	char ch;

	snprintf ( buf, MAX_BUF, SYSFS_GPIO_DIR "/gpio%d/value", gpio );

	fd = open ( buf, O_RDONLY );
	if ( fd < 0 )
	{
		pr_error ( "gpio/get-value" );
		return -1;
	}

	read ( fd, &ch, 1 );

	close ( fd );

	return ( ch != '0' );
}
/**
 * Opens GPIO (input/output) pin file descriptor
 * @param gpio GPIO number
 * @return file descriptor, -1 if fails
 */

int gpio_open_fd ( unsigned int gpio ) {
	int fd;
	char buf[MAX_BUF];

	snprintf ( buf, MAX_BUF, SYSFS_GPIO_DIR "/gpio%d/value", gpio );

	fd = open ( buf, O_RDWR );
	if ( fd < 0 )
	{
		pr_error ( "gpio/get-value" );
		return -1;
	}
	return fd;
}
/**
 * Sets GPIO (output) value with given file descriptor
 * @param file descriptor
 * @param gpio GPIO number
 * @return 0 if OK, -1 if fails
 */

int gpio_set_value_fd (int fd, int value)
{
    ssize_t ret;
    char gpio_value = value + '0';

    ret = write (fd, &gpio_value, 1);
    if (ret != 1)
    {
      printf("Error setting value gpio\n");
        return -1;
    }

    return 0;
}
/**
 * Read state of GPIO (input) pin, with file descriptor
 * @param file descriptor
 * @param gpio GPIO number
 * @return 0 if LO, 1 if HI, -1 if fails
 */

int gpio_get_value_fd (int fd, unsigned int gpio)
{
    ssize_t ret;
    char value;

    ret = pread (fd, &value, 1, 0);

    if (ret != 1)
    {
        printf("Error getting value of gpio %u\n", gpio);
        return -1;
    }

    return value == '1';
}
#endif
