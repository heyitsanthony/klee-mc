// RUN: gcc %s -O0 -o %t1
// RUN: ../../../scripts/save_guest.sh ./%t1 guest-physmem
// RUN: klee-mc -branch-hint=false -max-stp-time=5 -dump-states-on-halt=false -stop-after-n-tests=2 -use-softfp -validate-test -guest-type=sshot -max-time=120 -watchdog -  2>%t1.err >%t1.out
// RUN: rm -rf guest-physmem
// RUN: find -L klee-last -name *validate | xargs cat | not grep FAIL

#include <unistd.h>
#include <stdio.h>

/* Return the amount of physical memory available.  */
double physmem_available (void)
{
/* This works on linux-gnu, solaris2 and cygwin.  */
	double pages = sysconf (_SC_AVPHYS_PAGES);
	double pagesize = sysconf (_SC_PAGESIZE);
	if (0 <= pages && 0 <= pagesize)
		return pages * pagesize;
}

int main(void)
{
	if (physmem_available() != 0)
		puts("OK");
	return 0;
}
