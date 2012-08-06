// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -pipe-solver -shadow-func-file=../strcmp.txt -use-taint - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep err

#include "klee/klee.h"
#include <string.h>
#include <unistd.h>

typedef int(*strcmp_f)(const char*, const char*);


void test_shadowed_buf(char* buf, int len)
{
	unsigned	i;
	for (i = 0; i < len; i++)
		if (!ksys_is_shadowed(buf[i]))
			ksys_error("buf not shadowed", "noshadow.err");
}

int main(int argc, char *argv[])
{
	int		v;
	char		buf[17];
	unsigned	i;
	strcmp_f	f;

	if (read(0, buf, 16) != 16)
		return 1;

	buf[16] = 0;

	/* necessary so that strcmp is not optimized out
	 * XXX: where should optimized strcmps be caught with tainting? */
	f = strcmp;
	v = f("abcdef", buf);

	v++;
	if (!ksys_is_shadowed(v))
		ksys_error("inc was not shadowed", "noshadow.err");

	buf[0] = v*v;
	if (!ksys_is_shadowed(buf[0]))
		ksys_error("buf[0] not shadowed", "noshadow.err");

	for (i = 0; i < 16; i++)
		buf[i] = v;

	test_shadowed_buf(buf, 16);

	v = 0;
	if (ksys_is_shadowed(v))
		ksys_error("overwritten was shadowed", "shadow.err");


	return 0;
}
