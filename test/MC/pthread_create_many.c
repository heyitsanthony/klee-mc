// RUN: gcc -lpthread %s -O0 -o %t1
// RUN: klee-mc -pipe-solver -stop-after-n-tests=100 - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
#include <assert.h>
#include <pthread.h>

void* f(void* x) { return x; }

int main(int argc, char* argv[])
{
	pthread_t	pt[10];
	int		i;

	for (i = 0; i < 10; i++)
		pthread_create(&pt[i], NULL, f, (void*)((long)i));


	for (i = 0; i < 10; i++) {
		int	r = 0;
		pthread_join(&pt[i], &r);
		assert (r == i);
	}

	return 0;
}
