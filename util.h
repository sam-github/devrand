/**
* Common support files.
*/

#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>

struct Options
{
	char*	arg0;
	int		debug;
	int		irq;
};

extern struct Options options;

void	Help();
void	Usage(FILE* out);
void	GetOpts(int argc, char* argv[]);
void    Error(const char* format, ...);
void    Log(const char* format, ...);

#define ERR(E)  (E), strerror(E)

#endif

