// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
//
// There should be no pointer errors.
// RUN: ls klee-last | not grep "ptr.err"
//
#include <sys/types.h>
#include <sys/syscall.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

int main(void)
{
	uint8_t buf[16];
	int	r;

	buf[0] = 0;
	r = read(0, buf, 1);
	if (r <= 0) return -1;

	/* limit buf[0] to < 5
	 * (don't try *every* possible syscall [0,255]) */
	if (buf[0] > 5) {
		return -1;
	}

	return syscall(buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);
}