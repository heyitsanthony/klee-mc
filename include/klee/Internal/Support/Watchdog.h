#ifndef KLEE_WATCHDOG_H
#define KLEE_WATCHDOG_H

#include <signal.h>

namespace klee
{
class Watchdog
{
public:
	Watchdog(unsigned secs);
	virtual ~Watchdog(void);
private:
	Watchdog();
	unsigned	secs;
	pthread_t	thread;
};
};

#endif
