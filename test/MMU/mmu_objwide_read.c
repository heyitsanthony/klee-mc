// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: rm -rf guest-objwide-read
// RUN: ../../../scripts/save_guest.sh ./%t1 guest-objwide-read
// RUN: klee-mc -validate-test -logregs -print-new-ranges -pipe-solver -use-sym-mmu -sym-mmu-type=objwide -guest-type=sshot -guest-sshot=guest-objwide-read -  2>%t1.err >%t1.out
// RUN: ls klee-last | not grep err
// RUN: find klee-last/ | grep validate | xargs cat | not grep  FAIL

#include "klee/klee.h"
#include <unistd.h>
#include <string.h>

char x[4096*2];
typedef int(*strcmp_f)(const char*, const char*);

int main(int argc, char *argv[])
{
	void		*p;
	uint16_t	c;
	strcmp_f	sf;

	if (read(0, &c, 2) != 2) return 0;
	if (c > (4096*2)-5) return 1;

	ksys_print_expr("c", c);
	p = (x + c);
	ksys_print_expr("ppp", p);
	if (*(uint32_t*)p == 0) return 2;
	return 3;
}
