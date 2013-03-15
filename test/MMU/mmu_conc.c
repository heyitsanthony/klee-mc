// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: rm -rf guest-conc
// RUN: ../../../scripts/save_guest.sh ./%t1 guest-conc
// RUN: klee-mc -validate-test -logregs -print-new-ranges -pipe-solver -use-sym-mmu -sconc-mmu-type=cnull -guest-type=sshot -guest-sshot=guest-conc -  2>%t1.err >%t1.out
// RUN: ls klee-last | not grep err
// RUN: find klee-last/ | grep validate | xargs cat | not grep  FAIL
// RUN: klee-mc -validate-test -logregs -print-new-ranges -pipe-solver -use-sym-mmu -sconc-mmu-type=cnulltlb -guest-type=sshot -guest-sshot=guest-conc -  2>%t1.err >%t1.out
// RUN: ls klee-last | not grep err
// RUN: find klee-last/ | grep validate | xargs cat | not grep  FAIL


#include "klee/klee.h"
#include <unistd.h>
#include <string.h>

char x[4096];
char y[4096];

int main(int argc, char *argv[])
{
	int	i;

	memset(y, 'a', 4095);
	y[4095] = '\0';
	for (i = 0; i < 50; i++)
		strcpy(x, y);

	return x[0];
}
