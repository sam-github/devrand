//
// devn-random.c: the named random device
//

struct Device;

#define IOFUNC_ATTR_T	struct Device

#include <sys/iofunc.h>

#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/dispatch.h>
#include <sys/neutrino.h>
#include <sys/rsrcdbmgr.h>

#include "util.h"
#include "random.h"

int IoRead	(resmgr_context_t*	ctp, io_read_t* msg, RESMGR_OCB_T* ocb);
int IoNotify(resmgr_context_t*	ctp, io_notify_t* msg, RESMGR_OCB_T* ocb);
int IoStat	(resmgr_context_t*	ctp, io_stat_t* msg, RESMGR_OCB_T* ocb);
int IoLseek	(resmgr_context_t*	ctp, io_lseek_t* msg, RESMGR_OCB_T* ocb);
int IoPulse	(message_context_t*	ctp, int code, unsigned flags, void* handle);

//
// resmgr globals
//

static resmgr_connect_funcs_t	connect_funcs;
static resmgr_io_funcs_t		io_funcs;
static resmgr_attr_t			resmgr_attr;

//
// Our device extends the posix layer's attribute structure
//

struct Device
{
	iofunc_attr_t	ioa;

	const char*		name;
	int				unlimited;
};

typedef struct Device Device;

static Device	attrs[] = {
		{ {}, "/dev/random", 0 },
		{ {}, "/dev/urandom", 1 },
	};

iofunc_notify_t	notifications[3];

void DeviceAttach(dispatch_t* dpp)
{
	int	i;

	for(i = 0; i < sizeof(attrs)/sizeof(attrs[0]); ++i) {
		// initialize attribute structures
		iofunc_attr_init(&attrs[i].ioa, S_IFCHR | 0444, 0, 0);

		attrs[i].ioa.uid = geteuid();
		attrs[i].ioa.gid = getegid();
		attrs[i].ioa.rdev = 
			rsrcdbmgr_devno_attach((char*)attrs[i].name, i, 0);

		// attach our device names
		if(resmgr_attach(dpp, &resmgr_attr, attrs[i].name,
				_FTYPE_ANY, 0, &connect_funcs, &io_funcs, &attrs[i]) == -1) {
			Error("resmgr_attach of '%s' failed: [%d] %s\n",
				attrs[i].name, ERR(errno));
		}
	}
}


//
// Attach to our entropy source
//

//#define	IRQ_PULSE_CODE	_PULSE_CODE_MINAVAIL

static int irqId = -1;

void AttachEntropy(dispatch_t* dpp)
{
	struct sigevent se;

	// get i/o priviledges
	if(ThreadCtl( _NTO_TCTL_IO, 0 ) == -1) {
		Error("ThreadCtl(_IO) failed: [%d] %s\n", ERR(errno));
	}

	// attach a pulse
	se.sigev_code = pulse_attach(dpp,MSG_FLAG_ALLOC_PULSE,0,IoPulse,&irqId);
	if(se.sigev_code == -1) {
		Error("pulse_attach failed: [%d] %s\n", ERR(errno));
	}

	se.sigev_coid = message_connect(dpp, MSG_FLAG_SIDE_CHANNEL);
	if(se.sigev_coid == -1) {
		Error("message_connect failed: [%d] %s\n", ERR(errno));
	}
	se.sigev_notify = SIGEV_PULSE;
 	se.sigev_priority = -1;
	se.sigev_value.sival_int = options.irq; // set to the irq no

	rand_initialize();

	if(!rand_initialize_irq(options.irq)) {
		Error("rand initialize irq failed!\n");
	}
	irqId = InterruptAttachEvent(options.irq, &se,
		_NTO_INTR_FLAGS_PROCESS|_NTO_INTR_FLAGS_TRK_MSK);

	if(irqId == -1) {
		Error("InterruptAttach %d failed: [%d] %s\n", ERR(errno));
	}
}

//
// main
//

int main(int argc, char *argv[])
{
	dispatch_t*			dpp = 0;
	dispatch_context_t*	ctp = 0;

	GetOpts(argc, argv);

	Fork();

	// initialize dispatch interface
	dpp = dispatch_create();

	if(!dpp) {
		Error("dispatch_create failed: [%d] %s\n", ERR(errno));
	}

	// initialize functions for handling messages
	iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs,
		_RESMGR_IO_NFUNCS, &io_funcs);

	io_funcs.read = IoRead;
	io_funcs.write = 0; // ENOSYS
	io_funcs.notify = IoNotify;
	io_funcs.stat = IoStat;
	io_funcs.lseek = IoLseek;

	// initialize resource manager attributes
	resmgr_attr.nparts_max = 1;
	resmgr_attr.msg_max_size = BUFSIZ;

	// attach devices

	DeviceAttach(dpp);

	// allocate a context structure
	ctp = dispatch_context_alloc(dpp);

	if(!ctp) {
		Error("unable to alloc context!\n");
	}

	// attach to our entropy source

	AttachEntropy(dpp);

	// start the resource manager message loop

	Daemonize();

	while(1) {
		dispatch_context_t* c = dispatch_block(ctp);

		if(!c) {
			Log("dispatch_block() failed: [%d] %s\n", ERR(errno));
			continue;
		}
		dispatch_handler(c);
	}
}
int IoRead (resmgr_context_t *ctp, io_read_t *msg, RESMGR_OCB_T *ocb)
{
	int		nleft;
	int		nbytes;
	char	buffer[BUFSIZ];
	int		status;
	int		nonblock;

	if ((status = iofunc_read_verify (ctp, msg, ocb, &nonblock)) != EOK)
		return (status);

	// don't support pread(), etc.
	if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE)
		return (ENOSYS);

	//  on all reads (first and subsequent) calculate
	//  how many bytes we can return to the client,
	//  based upon the number of bytes available (nleft)
	//  and the client's buffer size

	if(ocb->attr->unlimited)
		nleft = sizeof(buffer);
	else
		nleft = get_random_size();

	nbytes = min (msg->i.nbytes, nleft);

//	Log("IoRead: unlimited %d nleft %d returning %d of %d\n",
//		ocb->attr->unlimited, nleft, nbytes, msg->i.nbytes);

	if (nbytes > 0) {
		// write the data into the clients buffer

		get_random_bytes(buffer, nbytes);

//		Log("IoRead: remaining %d\n", get_random_size());

		resmgr_msgwrite(ctp, buffer, nbytes, 0);

		//  set up the number of bytes (returned by client's read())

		_IO_SET_READ_NBYTES (ctp, nbytes);

		// dirty the access time
		ocb->attr->ioa.flags |= IOFUNC_ATTR_ATIME;
	} else {
		// they've asked for zero bytes or they've already previously
		// read everything
		
		_IO_SET_READ_NBYTES (ctp, 0);
	}

	return (EOK);
}
int IoPulse(message_context_t* ctp, int code, unsigned flags, void* handle)
{
	union sigval sv = ctp->msg->pulse.value;
	int irqid = *(int*)handle;

	add_interrupt_randomness(options.irq);

	InterruptUnmask(sv.sival_int, irqid);

	Log("IoPulse: nbytes %d\n", get_random_size());

	iofunc_notify_trigger(
		notifications, get_random_size(), IOFUNC_NOTIFY_INPUT);

	return 0;
}
int IoNotify(resmgr_context_t* ctp, io_notify_t* msg, RESMGR_OCB_T* ocb)
{
	int trig = _NOTIFY_COND_OUTPUT|_NOTIFY_COND_OBAND;
	int	e;
	int	a = 0;

	if(ocb->attr->unlimited || get_random_size() > 0) {
		trig |= _NOTIFY_COND_INPUT;
	}
	e = iofunc_notify(ctp, msg, notifications, trig, 0, &a);

	Log("IoNotify: input rdy %d armed %d\n", trig & _NOTIFY_COND_INPUT, a);

	return e;
}
int IoStat(resmgr_context_t* ctp, io_stat_t* msg, RESMGR_OCB_T* ocb)
{
	int sz = get_random_size();

	ocb->attr->ioa.nbytes = sz;

//	Log("IoStat: nbytes %d\n", sz);

	return iofunc_stat_default(ctp, msg, ocb);
}
int IoLseek(resmgr_context_t* ctp, io_lseek_t* msg, RESMGR_OCB_T* ocb)
{
	ctp = ctp, msg = msg, ocb = ocb;

	return ESPIPE;
}

