//
// devrand.h
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
//  I can be contacted as sroberts@uniserve.com, or sam@cogent.ca.
//

#ifndef DEVRAND_H
#define DEVRAND_H

#include <sys/io_msg.h>
#include <sys/sys_msg.h>
#include <sys/fsys_msg.h>

union Msg
{
	msg_t	type;
	msg_t	status;

	struct _io_open 				open;
	struct _io_open_reply 			open_reply;

	struct _io_close 				close;
	struct _io_close_reply 			close_reply;

//	struct _fsys_mkspecial			mkspec;
//	struct _fsys_mkspecial_reply	mkspec_reply;

//	struct _fsys_readlink			rdlink;
//	struct _fsys_readlink_reply		rdlink_reply;

	struct _fsys_umount				umount;
	struct _fsys_umount_reply		umount_reply;

	struct _io_dup 					dup;
	struct _io_dup_reply 			dup_reply;

	struct _io_write 				write;
	struct _io_write_reply 			write_reply;

	struct _io_read 				read;
	struct _io_read_reply 			read_reply;

	struct _io_lseek 				seek;
	struct _io_lseek_reply 			seek_reply;

	struct _io_fstat				fstat;
	struct _io_fstat_reply			fstat_reply;

//	struct _io_readdir 				readdir;
//	struct _io_readdir_reply	 	readdir_reply;

//	struct _io_rewinddir 			rewinddir;
//	struct _io_rewinddir_reply 		rewinddir_reply;

	struct _io_open					remove;
	struct _io_open_reply			remove_reply;

	struct _io_chmod 				chmod;

	struct _io_chown 				chown;

	struct _io_utime 				utime;

	//struct _io_lock 			
	//struct _io_config 			
	//struct _io_config_reply 			
	//struct _io_flags 			
	//struct _io_flags_reply 			
	//struct _io_ioctl 			
	//struct _io_ioctl_reply 			
	//struct _io_qioctl 			
	//struct _io_qioctl_reply 			
	//struct _select_set 			
	//struct _io_select 			
	//struct _io_select_reply 			

	// the system message structure is lame, build a better one here
	struct _sysmsg {
		struct _sysmsg_hdr	hdr;
		union {
			struct _sysmsg_signal	signal;
			struct _sysmsg_version	version;
		} body;
	};

	struct _sysmsg_reply {
		struct _sysmsg_hdr_reply	hdr;
		union {
			struct _sysmsg_version_reply	version;
		} body;
	};

	struct _sysmsg					sysmsg;
	struct _sysmsg_reply			sysmsg_reply;

	// reserve space because struct _io_open and such have buffers of
	// unspecified length as members, a symlink create msg has two paths
	// in that buffer!
	char reserve [2 * (PATH_MAX+1) + 2 * sizeof(struct _io_open)];
};

typedef union Msg Msg;

struct Device
{
	struct stat stat;
};

typedef struct Device Device;

struct Ocb
{
	int	links;

	int	unit;
	int	oflag;
	int	mode;
};

typedef struct Ocb Ocb;


#endif

