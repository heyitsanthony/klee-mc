// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
// 
// If running obnoxious error codes enabled, 
// 	Two tests: wr < -1 and wr >= 0.
// If running with occasional error codes,
// 	One test: wr == 1
// RUN: ls klee-last/ | not grep err
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

int main(void)
{
	ssize_t wr;
	wr = write(1, NULL, 1);
	if (wr != 1) ((char*)NULL)[0] = 1;
	return 0;
}