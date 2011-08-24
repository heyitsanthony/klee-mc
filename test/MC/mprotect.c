// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
// RUN: not grep mprotect klee-last/warnings.txt
// RUN: grep "exitcode=0" %t1.out
#include <sys/mman.h>
#include <stdio.h>

int main(int argc, char* argv[])
{
	void	*base;

	base = mmap(
		NULL,
		4096, 
		PROT_READ,
		MAP_ANONYMOUS | MAP_PRIVATE,
		-1,
		0);
	if (base == MAP_FAILED)
		return 1;

	if (mprotect(base, 4096, PROT_READ | PROT_WRITE) == -1)
		return 2;

	*((char*)base) = 1;
	munmap(base, 4096);

	return 0;
}
