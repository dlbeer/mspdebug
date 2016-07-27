//
// C++ Interface: gpio
//
// Description: 
//
//
// Author: Paolo Zebelloni <p.zebelloni@c-labs-wt.com>, (C) 2014
//
// Copyright: See COPYING file that comes with this distribution
//
//

#ifndef _GPIO_H
#define _GPIO_H

int gpio_is_exported ( unsigned int gpio );
int gpio_export ( unsigned int gpio );
int gpio_unexport ( unsigned int gpio );
int gpio_set_dir ( unsigned int gpio, unsigned int out_flag );
int gpio_set_value ( unsigned int gpio, unsigned int value );
int gpio_set_value_fd (int fd, int value);
int gpio_get_value ( unsigned int gpio );
int gpio_get_value_fd (int fd, unsigned int gpio);
int gpio_open_fd (unsigned int gpio);
#endif
