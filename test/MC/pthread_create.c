// RUN: gcc -lpthread %s -O0 -o %t1
// RUN: klee-mc -pipe-solver - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
#include <assert.h>
#include <pthread.h>

void* f(void* x) { return 1; }

int main(int argc, char* argv[])
{
	pthread_t	pt;
	int		r = 0;

	pthread_create(&pt, NULL, f, NULL);
	pthread_join(&pt, &r);
	assert (r == 1);

	return 0;
}
