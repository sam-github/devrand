//
// random.h
//
// Copyright (c) 2000, Sam Roberts
// 
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 1, or (at your option)
//  any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
//  I can be contacted as sroberts@uniserve.com.
//

#ifndef RANDOM_H
#define RANDOM_H

#ifndef __QNX__
#	error "This is only for using random.c under QNX4 and NTO!"
#endif

/*
* API exported by random.c
*/

void rand_initialize(void);
int rand_initialize_irq(int irq);
void add_interrupt_randomness(int irq);
void get_random_bytes(void *buf, int nbytes);
int  get_random_size(void);
#include <sys/types.h>

/*
* Definitions used by the module: provided to fake out <linux/...>
*/

#ifdef RANDOM

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#ifndef __QNXNTO__
#	define __QNX4__
#	include <unix.h>
#	include "rdtsc64.h"
#endif

typedef unsigned int __u32;
typedef   signed int __s32;

typedef unsigned short __u16;
typedef unsigned short __u8;

#ifndef __QNXNTO__
typedef __s32 ssize_t;
#endif
typedef __u32 loff_t;

#define inline
#define static

#define timeval timespec
#define do_gettimeofday(X) clock_gettime(CLOCK_REALTIME, X)
#define tv_usec tv_nsec

#define __initfunc(X) X

#define kmalloc(X, Y) malloc(X)

#ifndef __QNXNTO__
#	define __i386__
#endif

#ifndef __QNXNTO__
#	define rotate_left(I, WORD) _lrotl(WORD, I)
#endif

#ifdef __i386__
#	define NR_IRQS 16
#else
#	error "NR_IRQS must be configured for this platform!"
#endif

static int Jiffies(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ts.tv_nsec;
}

#define jiffies Jiffies()

#endif /* RANDOM */

#endif

