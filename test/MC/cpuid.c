// RUN: gcc %s -I../../../include/ -O0 -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep err
#include <stdint.h>
int main(void)
{
	uint32_t	eax = 0, ebx, ecx, edx;
	__asm__ __volatile__(
		"cpuid"
		: "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
		: "a" (eax));
	return 0;
}