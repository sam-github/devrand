/*
* Common support files.
*/

#include <stdarg.h>
#include <unistd.h>

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
			options.optDebug = 1;
			break;

		case 'i':
			options.optIrq = atoi(optarg);
			break;

		default:	
			Usage(stderr);
			exit(1);
		}
	}

	if(options.optIrq == 0) {
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

