// RUN: gcc %s -I../../../include/ -O0 -o %t1
//
// Ensure we smash the stack in reality
// RUN: ./%t1 2>&1 | grep Oops
//
// KLEE should detect the smash.
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
//
// If we're running stack smashed code, it'll be reported as ignored.
// RUN: ls klee-last | not grep smash_ignored.err
// 
// If we catch the stack smash, we're should report it.
// RUN: ls klee-last | grep smash.err
#include <unistd.h>
#include <stdio.h>
#include <klee/klee.h>	/* for SYS_klee */

static void smashed(void)
{
	ksys_report_error(
		__FILE__, __LINE__,
		"Stack illegally smashed!!!",
		"smash_ignored.err");
	fprintf(stderr, "Oops. Stack Smashed.\n");
	_exit(-1);
}

/* important not to have this on the stack; we would 
 * overwrite it when smashing otherwise */
unsigned int	i;

int main(int argc, char* argv[])
{
	intptr_t	hello[2];

	/* Stack is structured ala: 
	 * i
	 * hello[0]
	 * hello[1]
	 * ...
	 * return address
	 *
	 * Want to overwrite return address with 'smashed'
	 */
	for (i = 2; i < 64; i++) {
		intptr_t	mask = ~((intptr_t)0xfff);
		if ((hello[i] & mask) == (((intptr_t)&hello) & mask)) {
			/* don't mess up any stack pointers, silly */
			continue;
		}

		hello[i] = (intptr_t)&smashed;
	}

	/* goodnight, sweet prince */
	return 0;
}
