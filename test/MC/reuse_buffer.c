// RUN: gcc %s -O0 -o %t1
//
// Please don't crash on this test.
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
//
// And this shouldn't cause any errors
// RUN: ls klee-last | not grep .err
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char* argv[])
{
	struct stat	s;
	int		ret;

	ret = stat("/dev/null", &s);
	ret = stat("/dev/zero", &s);

	return 0;
}