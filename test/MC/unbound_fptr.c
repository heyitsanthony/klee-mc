// RUN: gcc %s -O0 -o %t1
//
// Don't crash on run
// RUN: klee-mc -pipe-solver - ./%t1 2>%t1.err >%t1.out
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


typedef int(*fptr)(void);

int main(int argc, char* argv[])
{
	fptr	f;
	read(0, &f, sizeof(f));
	return f();
}
