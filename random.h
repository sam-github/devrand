/**
*
*/

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
*/

void wake_up_interruptible(struct wait_queue** wq);

typedef struct poll_table poll_table;

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

int copy_from_user(void* dst, const void* src, size_t bytes)
{
	memcpy(dst, src, bytes);

	return bytes;
}

#endif

