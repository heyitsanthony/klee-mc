// RUN: gcc %s -O0 -ldl -I../../../include/  -o %t1
// RUN: klee-mc -print-new-ranges -pipe-solver -concrete-vfs - ./%t1 2>%t1.err >%t1.out
//
// Should not have any errors.
// RUN: ls klee-last | not grep err
// RUN: grep getxattr %t1.err

#include "klee/klee.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <dlfcn.h>

int main(int argc, char* argv[])
{
	void	*h;
	ssize_t	(*g)(const char*, const char*, void*, size_t);

	h = dlopen("/lib/libattr.so.1", RTLD_LAZY);
	if (h == NULL) {
		ksys_report_error(
			__FILE__,
			__LINE__, 
			"Could not open /lib/libattr.so.1. Concretes on?",
			"open.err");
		return -1;
	}

	g = dlsym(h, "getxattr");
	g("x", "x", NULL, 0);
	dlclose(h);

	return 0;
}