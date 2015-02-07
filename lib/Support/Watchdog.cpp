#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>

#include "klee/Internal/Support/Watchdog.h"

using namespace klee;

static void* watchdog_thread(void* secs)
{
	struct timespec		ts, rem;
	int			rc;
	ssize_t			sz;

	/* don't expect any one to try to join with watchdog  */
	pthread_detach(pthread_self());

	ts.tv_sec = (unsigned)((unsigned long)secs);
	ts.tv_nsec = 0;
	do {
		rc = nanosleep(&ts, &rem);
		ts = rem;
	} while (rc == -1);

	sz = write(2, "watchdog timeout\n", 17);
	assert (sz == 17);

	/* the _exit() is important because this is a signal,
	 * otherwise we segfault! */
	_exit(-1);

	return NULL;
}

Watchdog::Watchdog(unsigned _secs)
: secs(_secs)
{
	if (secs == 0) return;
	pthread_create(&thread, NULL, watchdog_thread, (void*)(long)secs);
}

Watchdog::~Watchdog(void)
{
	if (secs == 0) return;
	pthread_cancel(thread);
}
