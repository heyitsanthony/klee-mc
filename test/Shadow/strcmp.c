// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -print-new-ranges -pipe-solver -shadow-func-file=../strcmp.txt -use-taint - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep err

#include "klee/klee.h"
#include <string.h>
#include <unistd.h>

typedef int(*strcmp_f)(const char*, const char*);


int main(int argc, char *argv[])
{
	int		v;
	char		buf[17];
	strcmp_f	f;

	if (read(0, buf, 16) != 16)
		return 1;

	buf[16] = 0;

	/* necessary so that strcmp is not optimized out
	 * XXX: where should optimized strcmps be caught with tainting? */
	f = strcmp;
	v = f("abcdef", buf);

	if (!ksys_is_shadowed(v)) {
		ksys_error("strcmp was not shadowed", "noshadow.err");
		return 1;
	}
	return 0;
}
