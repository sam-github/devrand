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
#include "devrandirq.h"
#include "random.h"

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
void	FdGetPrio(pid_t pid, int* priority);

const char* MessageName(msg_t type);
const char* HandleOflagName(short int oflag);
const char* SysmsgSubtypeName(short unsigned subtype);

int Fstat(Ocb* ocb, pid_t pid);
int Open(pid_t pid, int unit, int fd, int oflag, int mode);
int Write(Ocb* ocb, pid_t pid, int nbytes, const char* data, int datasz);
int Read(Ocb* ocb, pid_t pid, int nbytes);
int Select(pid_t pid, struct _io_select* msg);

void DoReadQueue(void);

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

	if(format[strlen(format) - 1] != '\n')
		fprintf(stderr, "\n");

	exit(1);
}
void Log(const char* format, ...)
{
	va_list	al;
	va_start(al, format);
	vfprintf(stderr, format, al);
	va_end(al);

	if(format[strlen(format) - 1] != '\n')
		printf("\n");
}

/*
* Device Info
*/

Device units[2];

int	link_count;

#define UNIT_RANDOM 0
#define UNIT_URANDOM 1

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
		s->st_mode	= S_IFCHR | 0444; /* r-- r-- r-- */
		s->st_nlink	= 1;
	}
	/* despite the loop above, we only have 2 units, one unlimited,
	* one not.
	*/
	assert(unit == 2);
	link_count = unit;

	units[UNIT_RANDOM].unlimited = 0;
	units[UNIT_URANDOM].unlimited = 1;
}

/*
* Request Queues
*/

ReadRequest* readq;

// TODO - queue must be maintained in process priority order
void QueueReadRequest(ReadRequest* r)
{
	ReadRequest** rq = &readq;

	FdGetPrio(r->pid, &r->priority);

	while(*rq && (*rq)->priority >= r->priority)
		rq = &(*rq)->next;

	r->next = *rq;

	*rq = r;
}

ArmedPid*	armedq;

int SelectArm(pid_t pid, pid_t proxy)
{
	ArmedPid* a = (struct ArmedPid*) malloc(sizeof(struct ArmedPid));

	if(!a)
		return ENOMEM;

	a->pid = pid;
	a->proxy = proxy;

	a->next = armedq;

	armedq = a;

	return EOK;
}
void SelectDisarm(pid)
{
	ArmedPid** ap = &armedq;

	while(*ap && (*ap)->pid != pid)
		ap = &(*ap)->next;

	if(*ap) {
		*ap = (*ap)->next;
		free(*ap);
	}
}
void SelectTrigger()
{
	while(armedq) {
		ArmedPid* a = armedq;
		armedq = a->next;

		Trigger(a->proxy);
		free(a);
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
int		optIrq		= 1;

char usage[] =
	"Usage: %s [-hd] [-i <irq>]\n"
	;
char help[] =
	"  -h   print this helpful message\n"
	"  -d   debug mode, don't fork into the background\n"
	"  -i   irq to use for source of entropy (default is 1, the\n"
	"       PC keyboard)\n"
	"\n"
	"Unmount /dev/random and /dev/urandom to unload the driver\n"
	"nicely, it will exit when there are no mounted devices and\n"
	"no open files.\n"
	"\n"
	"/dev/random acts like a pipe, it will return as much data as\n"
	"is available, or at least one byte if it blocks. /dev/urandom\n"
	"will return successive cryptographic hashes of the same data,\n"
	"so it's not as random but sill useful, and it never blocks.\n"
	"\n"
	"Use a good irq as a source for entropy, not the timer interrupt!\n"
	"The mouse or keyboard interrupt would be a good choice.\n"
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

	if(optIrq == 0) {
		Error("A source of randomness must be specified!\n");
	}
}

/*
* Main
*/
int main(int argc, char* argv[])
{
	GetOpts(argc, argv);

	Fork();

	SetProcessFlags();

	rand_initialize();

	DeviceInit();
	FdInit();

	AttachPrefix("/dev/random", UNIT_RANDOM);
	AttachPrefix("/dev/urandom", UNIT_URANDOM);

	HookIrqs();

	Daemonize();

	return Loop();
}

/*
* Implementation
*/
void HookIrqs()
{
	if(HookIrqNo(optIrq) == -1)
		Error("Attach to %d failed: [%d] %s\n", optIrq, ERR(errno));
	if(!rand_initialize_irq(optIrq)) {
		Error("Attach to %d failed: [%d] %s\n", optIrq, ERR(ENOMEM));
	}
}
void SetProcessFlags()
{
	// set our process flags
	long pflags =
		  _PPF_PRIORITY_REC		// receive requests in priority order
		| _PPF_SERVER			// we're a server, send us version requests
		| _PPF_PRIORITY_FLOAT	// float our priority to clients
		| _PPF_SIGCATCH		// catch our clients signals, to clean up
		;

	if(qnx_pflags(pflags, pflags, 0, 0) == -1)
		Error("qnx_pflags %#x failed: [%d] %s\n",
			pflags, ERR(errno));
}
void AttachPrefix(const char* prefix, int unit)
{
	if(qnx_prefix_attach(prefix, 0, unit) == -1)
		Error("qnx_prefix_attach %s failed: [%d] %s",
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
				Error("Receive from parent %d failed: [%d] %s",
					getppid(), ERR(errno));

			qnx_scheduler(0, 0, SCHED_RR, -1, 1);

			signal(SIGHUP, SIG_IGN);
			signal(SIGINT, SIG_IGN);

			setsid();

			chdir("/");

			break;

			// the parent will wait for the Reply() to indicate that it has
			// started running
		default:
			if(Send(child, 0, 0, 0, 0) == -1)
				exit(1);
			exit(0);
		}
	}
}
void Daemonize()
{
	if(!optDebug) {
/*
	can use this to keep device times up-to-date
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
int Loop()
{
	pid_t pid;
	int  status;

	while(link_count > 0)
	{
		pid = Receive(0, &msg, sizeof(msg));

		if(pid == -1) {
			if(errno != EINTR) {
				Log("Receive() failed: [%d] %s", ERR(errno));
			}
			continue;
		}
		if(pid == IrqProxy()) {
			while(Creceive(pid, 0, 0) == pid)
				; // clear out any proxy overruns

			add_interrupt_randomness(optIrq);

//			Log("Irq: random size %d\n", get_random_size());

			// now that we have more entropy...
			SelectTrigger();
			DoReadQueue();

			continue;
		}

		status = Service(pid, &msg);

		Log("Service() returned status %d (%s), link_count %d",
			status, status == -1 ? "none" : strerror(status), link_count);

		if(status >= EOK) {
			msg.status = status;
			ReplyMsg(pid, &msg, sizeof(msg.status));
		}
	}
	return 0;
}
int CheckPerms(pid_t pid, mode_t mode, int unit)
{
	static const basemodes[] = { S_IROTH, S_IWOTH, S_IROTH|S_IWOTH, 0 };
	int	uid;
	int	gid;
	unsigned fuid	= Unit(unit)->stat.st_ouid;
	unsigned fgid	= Unit(unit)->stat.st_ogid;
	unsigned fmode	= Unit(unit)->stat.st_mode;
	unsigned okmode	= 0;

	FdGetIds(pid, &uid, &gid);

	mode = basemodes[mode & O_ACCMODE];

	if (uid == 0) {
		okmode = S_IRWXO;
	} else if (uid == fuid) {
		okmode = (fmode >> 6) & 007;
	} else if (gid == fgid) {
		okmode = (fmode >> 3) & 007;
	} else {
		okmode = fmode & 007;
	}
	return (mode & okmode) == mode ? EOK : EPERM;
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

		if(!ocb) {
			status = EBADF;
		} else if(!FdMap(pid, msg->dup.dst_fd, ocb)) {
			status = errno;
		} else {
			status = EOK;
		}

	  } break;

	case _IO_SELECT:
		status = Select(pid, &msg->select);

		break;

	case _FSYS_UMOUNT: {
		char* path = 0;

		if(msg->remove.path[0] != '\0') {
			status = EINVAL;
			break;
		}
		switch(msg->remove.unit) {
		case UNIT_RANDOM:	path = "/dev/random"; break;
		case UNIT_URANDOM:	path = "/dev/urandom"; break;
		}

		if(!path) {
			status = ENOENT;
		} else {
			int uid;
			int gid;
			int fuid = Unit(msg->remove.unit)->stat.st_ouid;
			int fgid = Unit(msg->remove.unit)->stat.st_ogid;

			FdGetIds(pid, &uid, &gid);

			if(uid != fuid && gid != fgid) {
				status = EPERM;
				break;
			}

			// reply with EOK, then exit
			status = EOK;
			if(qnx_prefix_detach(path) == -1) {
				Log("detach %s failed: [%d] %s\n", path, ERR(errno));
				status = errno;
			} else {
				link_count--;
			}
		}
	  } break;

	// operations supported directly by ocbs
	case _IO_FSTAT:
	case _IO_WRITE:
	case _IO_READ:
	case _IO_LSEEK:
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

		case _SYSMSG_SUBTYPE_SIGNAL:
			// pid got a signal...

			ReadUnblock(pid);

			status = -1;

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
	Ocb* ocb = 0;

	if(!Unit(unit))
		return ENOENT;

	if((oflag&(O_CREAT|O_EXCL)) == (O_CREAT|O_EXCL))
		return EEXIST;
	
	if(CheckPerms(pid, oflag, unit) != EOK)
		return EPERM;

	ocb = (Ocb*) malloc(sizeof(Ocb));

	memset(ocb, '\0', sizeof(Ocb));

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
	// I don't see a point to this, so its not implemented.

	return ENOSYS;
}
/*
* These implementations closely parallel the implementation of Linux's
* random_read() and random_read_unlimited().
*/
void ReadUnblock(pid_t pid)
{
	ReadRequest** rq = &readq;
	ReadRequest* r = 0;

	while(*rq && (*rq)->pid != pid)
		rq = &(*rq)->next;

	r = *rq;

	*rq = r->next;

	if(r->reply.nbytes == 0)
		r->reply.status = EINTR;

	Reply(r->pid, &r->reply, sizeof(r->reply) - sizeof(r->reply.data));

	free(r);
}
void DoReadQueue(void)
{
	ReadRequest** rq = &readq;

	while(*rq)
	{
		// Linux makes this work like a pipe, i.e. you block until
		// some data is available, not necessarily as much as you
		// asked for.

		ReadRequest* r = *rq;

		char	entropy[BUFSIZ];
		int		rdbytes = 0;
		int		wrbytes = 0;
		int		unlimited = Unit(r->ocb->unit)->unlimited;

		while(r->reply.nbytes < r->nbytes) {
			rdbytes = min(BUFSIZ, r->nbytes - r->reply.nbytes);

			if(!unlimited)
				rdbytes = min(get_random_size(), rdbytes);

			if(rdbytes == 0)
				break;

			get_random_bytes(entropy, rdbytes);

			wrbytes = Writemsg(r->pid,
				sizeof(r->reply) - sizeof(r->reply.data) + r->reply.nbytes,
				entropy, rdbytes);

			if(wrbytes == -1 && r->reply.nbytes == 0) {
				r->reply.status = errno;
				break;
			}

			r->reply.nbytes += wrbytes;

			if(wrbytes < rdbytes)
				break;
		}

		// set status to EAGAIN if no data was read, and status is ok,
		// and non-blocking
		if(r->reply.nbytes == 0 && r->reply.status == EOK) {
			if(r->ocb->oflag & O_NONBLOCK) {
				r->reply.status = EAGAIN;
			}
		}

		// no data read and status still ok, so leave blocked
		if(!r->reply.nbytes && r->reply.status == EOK) {
			rq = &((*rq)->next);
			continue;
		}

		// else, we're done with this request
		Reply(r->pid, &r->reply, sizeof(r->reply) - sizeof(r->reply.data));

		*rq = r->next;

		free(r);
	}
}
int Read(Ocb* ocb, pid_t pid, int nbytes)
{
	// deal summarily with zero-length reads

	if(nbytes == 0) {
		struct _io_read_reply reply;

		reply.status = EOK;
		reply.zero = 0;
		reply.nbytes = 0;

		Reply(pid, &reply, sizeof(reply) - sizeof(reply.data));
	} else {
		ReadRequest* r = malloc(sizeof(struct ReadRequest));

		if(!r)
			return ENOMEM;

		memset(r, 0, sizeof(*r));

		r->pid = pid,
		r->ocb = ocb;
		r->nbytes = nbytes;

		QueueReadRequest(r);

		DoReadQueue();
	}
	return -1;
}
/*
void BitSet(short unsigned* flag, short unsigned mask)
{
	*flag |= mask;
}
void BitClear(short unsigned* flag, short unsigned mask)
{
	*flag &= ~mask;
}
*/
int Select(pid_t pid, struct _io_select* msg)
{
	struct _io_select_reply* reply = 0;
	int sz = 0;
	int	armed = 0;
	int i = 0;

	Log("Select pid %d mode %#x proxy %d nfds %d\n",
		msg->pid, msg->mode, msg->proxy, msg->nfds);

	// the msg and reply are identical sizes, so we can sizeof msg
	sz = sizeof(struct _io_select) + msg->nfds * sizeof(struct _select_set);

	reply = (struct _io_select_reply*) alloca(sz);

	if(!reply)
		return ENOMEM;

	if(Readmsg(pid, 0, reply, sz) == -1) {
		return errno;
	}

	reply->status	= EOK;
	reply->nfds		= 0;
	memset(reply->zero, 0, sizeof(reply->zero));

	/* look for fds that we own */
	for(i = 0; i < msg->nfds; i++) {
		Ocb* ocb = FdGet(pid, reply->set[i].fd);
		unsigned short request	= reply->set[i].flag & 07;
		unsigned short response	= 0;

		if(!ocb)
			continue;

		reply->set[i].flag |= _SEL_POLLED;

		/* Never an exceptional condition and unwriteable... so lie
		* and they'll find out what they want is impossible?
		*/
		if(reply->set[i].flag & _SEL_EXCEPT) {
			reply->set[i].flag |= _SEL_IS_EXCEPT;
			reply->nfds++;
		}

		if(reply->set[i].flag & _SEL_OUTPUT) {
			reply->set[i].flag |= _SEL_IS_OUTPUT;
			reply->nfds++;
		}

		if(reply->set[i].flag & _SEL_INPUT) {
			Device *device = Unit(ocb->unit);
			if(device->unlimited || (get_random_size() > 0)) {
				reply->set[i].flag |= _SEL_IS_INPUT;
				reply->nfds++;
			} else {
				reply->set[i].flag &= ~_SEL_IS_INPUT;
			}
		}
		response = (reply->set[i].flag >> 4) & 07;

		if(msg->mode & _SEL_ARM) {
			if(request & response) {
				reply->set[i].flag &= ~_SEL_ARMED;
			} else {
				reply->set[i].flag |= _SEL_ARMED;
				armed++;
			}
		} else if(msg->mode & _SEL_POLL) {
			reply->set[i].flag &= ~_SEL_ARMED;
		}
	}

	if(msg->mode & _SEL_POLL) {
		SelectDisarm(pid);
	} else if(msg->mode & _SEL_ARM) {
		if(armed) {
			int e = SelectArm(pid, msg->proxy);
			if(e != EOK)
				return e;
		}
	}

	Reply(pid, reply, sz);

	return -1;
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
	if(index >= FDMAX) {
		errno = ENOMEM;
		return 0;
	}
	if(qnx_fd_attach(pid, fd, 0, 0, 0, 0, index) == -1) {
		Log("qnx_fd_attach(pid %d fd %d) failed: [%d] %s",
			pid, fd, ERR(errno));

		if(ocb)
			ocb->links--;

		return 0;
	}

	ocbs_[index] = ocb;

	if(ocb)
		link_count++;

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

	link_count--;

	if(ocb->links == 0)
		free(ocb);

	return 1;
}
void FdGetPrio(pid_t pid, int* priority)
{
	struct _psinfo3 psdata3;

	__get_pid_info(pid, &psdata3, ctrl_);

	*priority = psdata3.priority;
}
void FdGetIds(pid_t pid, int* uid, int* gid)
{
	struct _psinfo3 psdata3;

	__get_pid_info(pid, &psdata3, ctrl_);

	*uid = psdata3.euid;
	*gid = psdata3.egid;
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
