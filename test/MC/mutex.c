// RUN: gcc %s  -lpthread -O0 -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
//
// RUN: ls klee-last | not grep .err
//
#include <pthread.h>

int main(int argc, char* argv[])
{
	pthread_mutex_t	m;

	pthread_mutex_init(&m, NULL);
	pthread_mutex_lock(&m);
	pthread_mutex_unlock(&m);
	pthread_mutex_destroy(&m);

	return 0;
}