/*
* Common support files.
*/

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/sched.h>

#ifdef __QNXNTO__
#include <sys/neutrino.h>
#include <sys/procmgr.h>
#endif

#include "util.h"

struct Options options =
	{
		0,
		0,
		1
	};

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
	fprintf(out, usage, options.arg0);
}
void GetOpts(int argc, char* argv[])
{
	int opt;

	options.arg0 = strrchr(argv[0], '/');
	options.arg0 = options.arg0 ? options.arg0 : argv[0];

	while((opt = getopt(argc, argv, "hdi:")) != -1) {
		switch(opt) {
		case 'h':
			Usage(stdout);
			Help();
			exit(0);

		case 'd':
			options.debug = 1;
			break;

		case 'i':
			options.irq = atoi(optarg);
			break;

		default:	
			Usage(stderr);
			exit(1);
		}
	}

	if(options.irq == 0) {
		Error("A source of randomness must be specified!\n");
	}
}


/*
* Error reporting
*/

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
* Daemon setup
*/

void Fork()
{
	if(!options.debug) {
#ifndef __QNXNTO__
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
			umask(0);

			chdir("/");

			break;

			// the parent will wait for the Reply() to indicate that it has
			// started running
		default:
			if(Send(child, 0, 0, 0, 0) == -1)
				exit(1);
			exit(0);
		}
#endif
	}
}
void Daemonize()
{
	if(!options.debug) {

#ifndef __QNXNTO__
/*
	could use this to keep device times up-to-date
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
#else
	int coid = procmgr_daemon(0, 0);
	if(coid == -1) {
		Error("daemon mode failed: [%d] %s", ERR(errno));
	}
	ConnectDetach(coid);
#endif

	}
}

