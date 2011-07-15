// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
//
// There should be errors.
// RUN: ls klee-last | grep ".err"
//
// There should be exactly *one* error.
// RUN: ls klee-last | grep ".err" | wc -l | grep 1
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

int z=0;

int main(void)
{
	return 10 / z;
}