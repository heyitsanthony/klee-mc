// RUN: gcc %s -I../../../include/ -O0 -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
// RUN: grep "exitcode=0" %t1.err
#include <klee/klee.h>
#include <stdint.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>

/* pivots around cleft */
void cleft_test(char* cleft)
{
	*((volatile uint16_t*)(cleft - 1)) = 0x1234;
	if (*((volatile uint16_t*)(cleft - 1)) != 0x1234) {
		ksys_report_error(
			__FILE__, __LINE__,
			"16-bit write to bound is bad",
			"badwr.err");
	}

	*((volatile uint32_t*)(cleft - 2)) = 0x12345678;
	if (*((volatile uint32_t*)(cleft - 2)) != 0x12345678) {
		ksys_report_error(
			__FILE__, __LINE__,
			"32-bit write to bound is bad",
			"badwr.err");
	}

	*((volatile uint64_t*)(cleft - 4)) = 0x12345678abcdef01;
	if (*((volatile uint64_t*)(cleft - 4)) != 0x12345678abcdef01) {
		ksys_report_error(
			__FILE__, __LINE__,
			"64-bit write to bound is bad",
			"badwr.err");
	}
}

/* Pointers have become meaningless */
int main(int argc, char* argv[])
{
	void	*base;
	char	*cleft;

	base = mmap(
		NULL,
		8192, 
		PROT_READ,
		MAP_ANONYMOUS | MAP_PRIVATE,
		-1,
		0);
	if (base == MAP_FAILED)
		return 1;

	memset(base, 0, 8192);
	cleft = (char*)((intptr_t)base + 4096);
	cleft_test(cleft);
	cleft_test(cleft-1);
	cleft_test(cleft+1);
	cleft_test(cleft-2);
	cleft_test(cleft+2);
	cleft_test(cleft+3);
	cleft_test(cleft-3);
	cleft_test(cleft+4);
	cleft_test(cleft-4);

	munmap(base, 8192);

	return 0;
}