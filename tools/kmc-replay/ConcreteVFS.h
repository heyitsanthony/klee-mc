#ifndef CONCRETEVFS_H
#define CONCRETEVFS_H

#include "syscall/syscalls.h"
typedef std::map<int, int>	guestfd2fd_ty;

class ConcreteVFS
{
public:
	ConcreteVFS() {}
	virtual ~ConcreteVFS() {}
	bool apply(Guest* g, const SyscallParams& sp, int xlate_nr);
private:
	guestfd2fd_ty		gfd2fd;
};
#endif
