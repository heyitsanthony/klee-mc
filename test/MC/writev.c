// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -pipe-solver - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
#include <stdio.h>
#include <sys/uio.h>


static struct iovec iov[2] = {
	{ .iov_base = "Hello ", .iov_len = 6},
	{ .iov_base = "World", .iov_len = 5}
};

int main(int argc, char* argv[])
{
	writev(1, iov, 2);
	return 0;
}
