/**
* This code is derived from example code from QUICS. The original
* author(s) are uncredited.
*/


#ifndef RDTSC64_H
#define RDTSC64_H

typedef union {
	struct {
		unsigned long lo,hi;
	};
	struct {
		unsigned short lolo, lohi, hilo, hihi;
	};
#define NBPL 8
	unsigned char b[NBPL];
} long64;

typedef struct {
    long l, h;
} int64;

unsigned _lladd(long64 *r, long64 a, long64 b);
unsigned _llsub(long64 *r, long64 a, long64 b);
unsigned _llmul(long64 *res, long64 a, long64 b);
//unsigned _lldiv(long64 *quo, long64 *rem, long64 num, long64 denom);

#pragma aux _lladd = \
	"mov  edx,[esp]" \
	"add  edx,8[esp]" \
	"mov  [eax],edx" \
	"mov  edx,4[esp]" \
	"adc  edx,12[esp]" \
	"mov  4[eax],edx" \
	"setc al" \
	"movzx eax,al" \
	parm caller [eax] modify [eax edx] value [eax];

#pragma aux _llsub = \
	"mov edx,[esp]" \
	"sub edx,8[esp]" \
	"mov [eax],edx" \
	"mov edx,4[esp]" \
	"sbb edx,12[esp]" \
	"mov 4[eax],edx" \
	"setc al" \
	"movzx eax,al" \
	parm caller [eax] modify [eax edx] value [eax];

#pragma aux _llmul =\
	"mov    ecx, eax" \
	"mov    eax, [esp]" \
	"mul    dword ptr 8[esp]" \
	"mov    [ecx], eax" \
	"mov    ebx, edx" \
	"mov    eax, [esp]" \
	"mul    dword ptr 12[esp]" \
	"add    ebx, eax" \
	"mov    eax, 4[esp]"\
	"mul    dword ptr 12[esp]" \
	"add    ebx, eax" \
	"mov    4[ecx], ebx" \
	"xor    eax,eax" \
	parm caller [eax] modify [eax ebx ecx edx] value [eax];


void rdtsc64( long64 *ptr );

#pragma aux rdtsc64 = "db 0fh,31h" "mov [ebx],eax" "mov [ebx+4],edx" \
	parm nomemory [ebx] modify exact nomemory [eax edx];

/**
* Read the 64 Bit Pentium Time Stamp Counter.
*/
void rdtsc64( long64 *ptr );

#pragma aux rdtsc64 = "db 0fh,31h" "mov [ebx],eax" "mov [ebx+4],edx" \
	parm nomemory [ebx] modify exact nomemory [eax edx];


// rdtsc32 - Read Time Stamp Counter 32
// Read only the least significant 32 bits of the 64 bit cycle counter
// edx contains the most significant 32 bits, but it not returned by this
// pragma.
unsigned long rdtsc32( void );
#pragma aux rdtsc32 = "db 0fh,31h" \
	parm nomemory [] modify exact nomemory [eax edx];

#endif

