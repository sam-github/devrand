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

#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unix.h>

#ifdef __QNX__
#	include "rdtsc64.h"
#endif

void rand_initialize(void);
int rand_initialize_irq(int irq);
void add_interrupt_randomness(int irq);
void get_random_bytes(void *buf, int nbytes);
int  get_random_size(void);

/*
struct file
{
	int	pid;
	int	f_flags;
};

struct poll_table
{
	int unused;
};

struct wait_queue
{
	struct file* f;
	struct wait_queue* q;
};

void wake_up_interruptible(struct wait_queue** wq);

typedef struct poll_table poll_table;
*/

/*
* Typedefs used by the module.
*/

typedef unsigned int __u32;
typedef   signed int __s32;

typedef unsigned short __u16;
typedef unsigned short __u8;

#define NR_IRQS 16

typedef __s32 ssize_t;
typedef __u32 loff_t;

#define inline
#define static

#define timeval timespec
#define do_gettimeofday(X) clock_gettime(CLOCK_REALTIME, X)
#define tv_usec tv_nsec

#define __initfunc(X) X

#define kmalloc(X, Y) malloc(X)

#ifdef __QNX__
#	define __i386__
#endif

#define rotate_left(I, WORD) _lrotl(WORD, I)

/*
int copy_from_user(void* dst, const void* src, size_t bytes)
{
	memcpy(dst, src, bytes);

	return bytes;
}
*/

#endif

