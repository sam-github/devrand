//
// devrand.cc
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

#include "devrand.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/fd.h>
#include <sys/kernel.h>
#include <sys/prfx.h>
#include <sys/psinfo.h>
#include <sys/sched.h>
#include <sys/types.h>

void	Error(const char* format, ...);
void	Log(const char* format, ...);
int		Service(pid_t pid, Msg* msg);
int		Loop();
void	Help();
void	Usage(FILE* out);
void	GetOpts(int argc, char* argv[]);
void	SetProcessFlags();
void	AttachPrefix(const char* prefix, int unit);
void	Fork();
void	Deamonize();
void	ReplyMsg(pid_t pid, const void* msg, size_t size);

Ocb*	FdGet(pid_t pid, int fd);
int		FdMap(pid_t pid, int dst_fd, Ocb* ocb);
int		FdUnMap(pid_t pid, int fd);

const char* MessageName(msg_t type);
const char* HandleOflagName(short int oflag);
const char* SysmsgSubtypeName(short unsigned subtype);

int Fstat(Ocb* ocb, pid_t pid);
int Open(pid_t pid, int unit, int fd, int oflag, int mode);
int Write(Ocb* ocb, pid_t pid, int nbytes, const char* data, int datasz);
int Read(Ocb* ocb, pid_t pid, int nbytes);

/*
* Error reporting
*/
#define ERR(E)  (E), strerror(E)

void Error(const char* format, ...)
{
	va_list	al;
	va_start(al, format);
	vfprintf(stderr, format, al);
	va_end(al);

	exit(1);
}
void Log(const char* format, ...)
{
	va_list	al;
	va_start(al, format);
	vfprintf(stderr, format, al);
	va_end(al);
}

/*
* Device Info
*/

Device units[2];

Device* Unit(int unit)
{
	if(unit < 0 || unit >= sizeof(units))
		return 0;

	return &units[unit];
}

void DeviceInit()
{
	int unit;
	dev_t rdev = qnx_device_attach();

	if(rdev == -1)
		Error("qnx_device_attach failed: [%d] %s\n", ERR(errno));

	for(unit = 0; unit < sizeof(units)/sizeof(Device); ++unit) {
		Device* d = Unit(unit);
		struct stat* s = &d->stat;

		s->st_ino	= unit;
		s->st_dev	= (getnid() << 16) | (rdev << 10) | unit;
		s->st_rdev	= s->st_dev;
		s->st_ouid	= geteuid();
		s->st_ogid	= getegid();
		s->st_ftime	=
		s->st_mtime	=
		s->st_atime	= 
		s->st_ctime	= time(0);
		s->st_mode	= S_IFCHR | 0640; /* rw- r-- --- */
		s->st_nlink	= 1;
	}
}

/*
* Message Buffers
*/

struct _sysmsg_version_reply version = {
		"Random",
		__DATE__,
		100,
		'A',
		0
	};

Msg	msg;

/*
* Options
*/
char*	arg0		= 0;
int		optDebug	= 0;
int		optIrq		= 0;

char usage[] =
	"Usage: %s [-hd] [-i <irq>]\n"
	;
char help[] =
	"  -h   print this useful help message\n"
	"  -d   debug mode, don't fork into a daemon\n"
	"  -i   irq to use for source of randomness\n"
	;

void Help()
{
	printf("%s", help);
}
void Usage(FILE* out)
{
	fprintf(out, usage, arg0);
}
void GetOpts(int argc, char* argv[])
{
	int opt;

	arg0 = strrchr(argv[0], '/');
	arg0 = arg0 ? arg0 : argv[0];

	while((opt = getopt(argc, argv, "hdi:")) != -1) {
		switch(opt) {
		case 'h':
			Usage(stdout);
			Help();
			exit(0);

		case 'd':
			optDebug = 1;
			break;

		case 'i':
			optIrq = atoi(optarg);
			break;

		default:	
			Usage(stderr);
			exit(1);
		}
	}
}
void SetProcessFlags()
{
	// set our process flags
	long pflags =
		  _PPF_PRIORITY_REC		// receive requests in priority order
		| _PPF_SERVER			// we're a server, send us version requests
		| _PPF_PRIORITY_FLOAT	// float our priority to clients
		//| _PPF_SIGCATCH		// catch our clients signals, to clean up
		;

	if(qnx_pflags(pflags, pflags, 0, 0) == -1)
		Error("qnx_pflags %#x failed: [%d] %s\n",
			pflags, ERR(errno));
}
void AttachPrefix(const char* prefix, int unit)
{
	if(qnx_prefix_attach(prefix, 0, unit) == -1)
		Error("qnx_prefix_attach failed: [%d] %s",
			prefix, ERR(errno));
}
void Fork()
{
	if(!optDebug) {
		pid_t	child = fork();

		switch(child) {
		case -1:
			Error("fork failed: [%d] %s", ERR(errno));

		case 0:
			if(Receive(getppid(), 0, 0) == -1)
				Error("Receive from parent failed: [%d] %s",
					getppid(), ERR(errno));

			break;

			// the parent will wait for the Reply() to indicate that it has
			// started running
		default:
			if(Send(child, 0, 0, 0, 0) == -1)
				Error(0, "Send to child failed: [%d] %s",
					ERR(errno));
			exit(0);
		}
	}

	qnx_scheduler(0, 0, SCHED_RR, -1, 1);

	signal(SIGHUP, SIG_IGN);
	signal(SIGINT, SIG_IGN);

	setsid();

	chdir("/");

}
void Daemonize()
{
	if(!optDebug) {
/*
		struct _osinfo osdata;
		if(qnx_osinfo(0, &osdata) == -1) {
		perror("osinfo");
		return -1;
		}
		timep = MK_FP(osdata.timesel, offsetof(struct _timesel, seconds));
*/

		// free up our parent, if we have one
		Reply(getppid(), 0, 0);

		close(0);
		close(1);
		close(2);
	}
}
void Loop()
{
	pid_t pid;
	int  status;

	while(1)
	{
		pid = Receive(0, &msg, sizeof(msg));

		if(pid == -1) {
			if(errno != EINTR) {
				Log("Receive() failed: [%d] %s", strerror(errno));
			}
			continue;
		}

		status = Service(pid, &msg);

		Log("Service() returned status %d (%s)",
			status, status == -1 ? "none" : strerror(status));

		if(status >= EOK) {
			msg.status = status;
			ReplyMsg(pid, &msg, sizeof(msg.status));
		}
	}
}
int Service(pid_t pid, Msg* msg)
{
	int status = -1;

	Log("Service() pid %d type %s (%#x)",
		pid, MessageName(msg->type), msg->type);

	switch(msg->type)
	{
	case _IO_STAT:
		if(msg->open.path[0] || (msg->open.eflag & _IO_EFLAG_DIR)) {
			status = ENOTDIR;
		} else {
			status = Stat(pid, msg->open.unit);
		}
		break;
/*
	case _IO_HANDLE:
		// XXX handle must have different permissions checks than Open!
		// XXX the uname, chmod, chown calls should be supported!

		switch(msg->open.oflag)
		{
		case _IO_HNDL_RDDIR:
			status = Open(pid, msg->open.unit, msg->open.fd,
									msg->open.oflag, msg->open.mode);
			break;

		default:
			Log("unknown msg type IO_HANDLE subtype %s (%d) path \"%s\"",
				HandleOflagName(msg->open.oflag), msg->open.oflag, msg->open.path);
			status = ENOSYS;
			break;
		}
		break;
*/
	case _IO_OPEN:
		status = Open(pid, msg->open.unit, msg->open.fd,
								msg->open.oflag, msg->open.mode);
		break;

	case _IO_CLOSE:
		status = EOK;

		if(!FdUnMap(pid, msg->close.fd)) {
			status = errno;
		}

		break;

	case _IO_DUP: {
		Ocb* ocb = FdGet(msg->dup.src_pid, msg->dup.src_fd);

		if(!ocb)
		{
			status = EBADF;
		}
		else if(!FdMap(pid, msg->dup.dst_fd, ocb))
		{
			status = errno;
		}
		else
		{
			status = EOK;
		}

		} break;

	case _FSYS_UMOUNT: {
		if(msg->remove.path[0] != '\0') {
			status = EINVAL;
			break;
		}

		// reply with EOK, then exit
		msg->status = EOK;
		ReplyMsg(pid, msg, sizeof(msg->status));

		Log("umount - exiting manager");
		exit(0);

		} break;

	// operations supported directly by ocbs
	case _IO_FSTAT:
	case _IO_WRITE:
	case _IO_READ:
	case _IO_LSEEK:
	case _IO_READDIR:
	case _IO_REWINDDIR:
	{
		// These messages all have the fd at the same offset, so the
		// following works:
		Ocb* ocb = FdGet(pid, msg->write.fd);

		if(!ocb)
		{
			status = EBADF;
			break;
		}

		switch(msg->type)
		{
		case _IO_FSTAT:
			status = Fstat(ocb, pid);
			break;
		case _IO_WRITE:
			status = Write(ocb, pid, msg->write.nbytes,
							&msg->write.data[0], sizeof(*msg) - sizeof(msg->write));
			break;
		case _IO_READ:
			status = Read(ocb, pid, msg->read.nbytes);
			break;
		case _IO_LSEEK:
			status = ESPIPE;
			break;
		default:
			status = ENOSYS;
		}
	}	break;

//	case _IO_CHMOD: break;
//	case _IO_CHOWN: break;
//	case _IO_IOCTL: break;
//	case _IO_SELECT: break;
//	case _IO_QIOCTL: break;

	case _SYSMSG: {
		short unsigned subtype = msg->sysmsg.hdr.subtype;

		switch(subtype)
		{
		case _SYSMSG_SUBTYPE_VERSION:
			msg->sysmsg_reply.hdr.status	= EOK;
			msg->sysmsg_reply.hdr.zero		= 0;
			msg->sysmsg_reply.body.version	= version;

			Reply(pid, msg, sizeof(msg->sysmsg_reply));

			break;
		
		default:
			Log("unknown msg type SYSMSG subtype %s (%d)",
				SysmsgSubtypeName(subtype), subtype);
			status = ENOSYS;
			break;
		}
	} break;

	default:
		Log("unknown msg type %s (%#x)", MessageName(msg->type), msg->type);

		status = ENOSYS;
		break;

	} // end switch(msg->type)

	return status;
}

void ReplyMsg(pid_t pid, const void* msg, size_t size)
{
	if(Reply(pid, msg, size) == -1) {
		Log("Reply() Reply(%d) failed: [%d] %s",
			pid, errno, strerror(errno)
			);
	}
}

void main(int argc, char* argv[])
{
	GetOpts(argc, argv);

	Fork();

	SetProcessFlags();

	DeviceInit();
	FdInit();

	AttachPrefix("/dev/random", 0);
	AttachPrefix("/dev/urandom", 1);

	Daemonize();

	Loop();
}

const char* MessageName(msg_t type)
{
	switch(type)
	{
	case 0x0000: return "SYSMSG";

	case 0x0101: return "IO_OPEN";
	case 0x0102: return "IO_CLOSE";
	case 0x0103: return "IO_READ";
	case 0x0104: return "IO_WRITE";
	case 0x0105: return "IO_LSEEK";
	case 0x0106: return "IO_RENAME";
	case 0x0107: return "IO_GET_CONFIG";
	case 0x0108: return "IO_DUP";
	case 0x0109: return "IO_HANDLE";
	case 0x010A: return "IO_FSTAT";
	case 0x010B: return "IO_CHMOD";
	case 0x010C: return "IO_CHOWN";
	case 0x010D: return "IO_UTIME";
	case 0x010E: return "IO_FLAGS";
	case 0x010F: return "IO_LOCK";
	case 0x0110: return "IO_CHDIR";
	case 0x0112: return "IO_READDIR";
	case 0x0113: return "IO_REWINDDIR";
	case 0x0114: return "IO_IOCTL";
	case 0x0115: return "IO_STAT";
	case 0x0116: return "IO_SELECT";
	case 0x0117: return "IO_QIOCTL";

	case 0x0202: return "FSYS_MKSPECIAL";
	case 0x0203: return "FSYS_REMOVE";
	case 0x0204: return "FSYS_LINK";
	case 0x0205: return "FSYS_MOUNT_RAMDISK";
	case 0x0206: return "FSYS_UNMOUNT_RAMDISK";
	case 0x0207: return "FSYS_BLOCK_READ";
	case 0x0208: return "FSYS_BLOCK_WRITE";
	case 0x0209: return "FSYS_DISK_GET_ENTRY";
	case 0x020A: return "FSYS_SYNC";
	case 0x020B: return "FSYS_MOUNT_PART";
	case 0x020C: return "FSYS_MOUNT";
	case 0x020D: return "FSYS_GET_MOUNT";
	case 0x020E: return "FSYS_DISK_SPACE";
	case 0x020F: return "FSYS_PIPE";
	case 0x0210: return "FSYS_TRUNC";
	case 0x0211: return "FSYS_OLD_MOUNT_DRIVER";
	case 0x0212: return "FSYS_XSTAT";
	case 0x0213: return "FSYS_MOUNT_EXT_PART";
	case 0x0214: return "FSYS_UMOUNT";
	case 0x0215: return "FSYS_RESERVED";
	case 0x0216: return "FSYS_READLINK";
	case 0x0217: return "FSYS_MOUNT_DRIVER";
	case 0x0218: return "FSYS_FSYNC";
	case 0x0219: return "FSYS_INFO";
	case 0x021A: return "FSYS_FDINFO";
	case 0x021B: return "FSYS_MOUNT_DRIVER32";

	case 0x0310: return "DEV_TCGETATTR";
	case 0x0311: return "DEV_TCSETATTR";
	case 0x0312: return "DEV_TCSENDBREAK";
	case 0x0313: return "DEV_TCDRAIN";
	case 0x0314: return "DEV_TCFLUSH";
	case 0x0315: return "DEV_TCFLOW";
	case 0x0316: return "DEV_TCGETPGRP";
	case 0x0317: return "DEV_TCSETPGRP";
	case 0x0318: return "DEV_INSERTCHARS";
	case 0x0319: return "DEV_MODE";
	case 0x031A: return "DEV_WAITING";
	case 0x031B: return "DEV_INFO";
	case 0x031C: return "DEV_ARM";
	case 0x031D: return "DEV_STATE";
	case 0x031E: return "DEV_READ";
	case 0x031F: return "DEV_WRITE";
	case 0x0320: return "DEV_FDINFO";
	case 0x0321: return "DEV_TCSETCT";
	case 0x0322: return "DEV_TCDROPLINE";
	case 0x0323: return "DEV_SIZE";
	case 0x0324: return "DEV_READEX";
	case 0x0325: return "DEV_OSIZE";
	case 0x0326: return "DEV_RESET";

	default: return "UNKNOWN";
	}
}
const char* HandleOflagName(short int oflag)
{
	switch(oflag)
	{
	case 1:		return "IO_HNDL_INFO";
	case 2:		return "IO_HNDL_RDDIR";
	case 3:		return "IO_HNDL_CHANGE";
	case 4:		return "IO_HNDL_UTIME";
	case 5:		return "IO_HNDL_LOAD";
	case 6:		return "IO_HNDL_CLOAD";
	default:	return "undefined";
	}
}
const char* SysmsgSubtypeName(short unsigned subtype)
{
	switch(subtype)
	{
	case 0:		return "DEATH";
	case 1:		return "SIGNAL";
	case 2:		return "TRACE";
	case 3:		return "VERSION";
	case 4:		return "SLIB";
	default:	return "undefined";
	}
}
int Stat(pid_t pid, int unit)
{
	struct _io_fstat_reply r;
	Device* d = Unit(unit);

	if(!d)
		return ENOENT;

	r.status = EOK;
	r.zero = 0;
	r.stat = d->stat;

	Reply(pid, &r, sizeof(r));

	return -1;
}
int Fstat(Ocb* ocb, pid_t pid)
{
	return Stat(pid, ocb->unit);
}
int Open(pid_t pid, int unit, int fd, int oflag, int mode)
{
	Ocb* ocb = (Ocb*) malloc(sizeof(Ocb));

	memset(ocb, '\0', sizeof(Ocb));

	assert(unit == 0);

	ocb->links	= 0;
	ocb->unit	= unit;
	ocb->oflag	= oflag;
	ocb->mode	= mode;

	// at the very least, Ocb must contain the RD, WR, and NONBLOCK state
	if(!FdMap(pid, fd, ocb)) {
		free(ocb);
		return errno;
	}

	return EOK;
}
int Write(Ocb* ocb, pid_t pid, int nbytes, const char* data, int datasz)
{
	return ENOSYS;
}
int Read(Ocb* ocb, pid_t pid, int nbytes)
{
	return ENOSYS;
}

/*
* fd <--> ocb Map
*
* This is a dumb fixed-size iplementation, good enough for now.
*/

#define FDMAX 256

void*	ctrl_ = 0;
Ocb*	ocbs_[FDMAX];

void FdInit()
{
	pid_t pid = getpid();
	ctrl_ = __init_fd(pid);
}
Ocb* FdGet(pid_t pid, int fd)
{
	int index = (int) __get_fd(pid, fd, ctrl_);

	if(index <= 0 || index >= FDMAX) {
		errno = EBADF;
		return 0;
	}

	if(!ocbs_[index])
		errno = EBADF;

	return ocbs_[index];
}
int FdMap(pid_t pid, int fd, Ocb* ocb)
{
	// index 0 is never used, it means "unmapped"
	int index = 0;

	if(ocb)
		ocb->links++;

	if(ocb) {
		index = 1;
		while(ocbs_[index])
			index++;
	}
	if(qnx_fd_attach(pid, fd, 0, 0, 0, 0, index) == -1) {
		Log("qnx_fd_attach(pid %d fd %d) failed: [%d] %s",
			pid, fd, ERR(errno));

		if(ocb)
			ocb->links--;

		return 0;
	}

	ocbs_[index] = ocb;

	return 1;
}
int FdUnMap(pid_t pid, int fd)
{
	Ocb* ocb = FdGet(pid, fd);

	if(!ocb)
		return 0; // attempt by client to close a bad fd

	// zero the mapping to invalidate the fd
	if(!FdMap(pid, fd, 0))
		return 0;

	ocb->links--;

	if(ocb->links == 0)
		free(ocb);

	return 1;
}

