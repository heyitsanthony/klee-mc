// RUN: gcc %s -O3 -I../../../include/  -o %t1
// RUN: klee-mc -pipe-solver -use-sym-mmu - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep err

/* this tests loading a primitive with the mmu */
/* right now it's not a very good test-- needs to 
 * check patterns; not 0 */
#include "klee/klee.h"
#include <stdint.h>
#include <string.h>
#include <unistd.h>

char x[4096];
typedef int(*strcmp_f)(const char*, const char*);

int main(int argc, char *argv[])
{
	void		*p;
	uint8_t		c;
	int		cmp;
	strcmp_f	sf;

	p = x;
	if (read(0, &c, 1) != 1) return 1;

	p = ((char*)p + c);
	memset(p, 1, 256);
	sf = strcmp;
	cmp = sf(p, "abcdefghijklmnopqrstuv");
	ksys_assume_ne(cmp, 0);

	return 2;
}
