// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
//
// Should error out on an illegal instruction or a jump to symbolic code
// By default everything "works".
// RUN: ls klee-last/ | grep err
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>

typedef void(*w_f)(void);
char whatever[] = {0xcc, 0xcc, 0xcc, 0xcc};

int main(int argc, char* argv[])
{
	char	buf;
	ssize_t	sz;
	w_f	f;

	mprotect(
		(void*)((intptr_t)whatever & ~0xfff),
		4096, 
		PROT_EXEC | PROT_WRITE | PROT_READ);

	sz = read(STDIN_FILENO, &buf, 1);
	if (sz == 0) buf = '0';
	buf = (buf > '0') ? 0xcc : 0xf0;

	whatever[0] = buf;
	f = (w_f)whatever;
	f();
	// TRAP EXPECTED.
	return 0;
}