// RUN: gcc -I../../../include/ %s -O0 -o %t1
//
// RUN: klee-mc -pipe-solver - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep err
// RUN: grep p.ok %t1.err
// RUN: grep p_new.ok %t1.err
// RUN: grep realloc2.ok %t1.err
#define _GNU_SOURCE
#include <klee/klee.h>
#include <sys/mman.h>
#include <string.h>

static char	*null_ptr = NULL;

int main(int argc, char* argv[])
{
	int	err;
	void	*p, *p2, *p_new, *m, *m2;

	p = mmap(0, 4096,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		return -1;

	/* 1. test fixed maps-- no maymove */
	p2 = mremap(p, 4096, 8192, 0);
	if (p2 != MAP_FAILED) {
		/* verify mapping worked */
		ksys_print_expr("p", p);
		ksys_print_expr("p2", p2);
		memset(p, 0, 8192);
		ksys_print_expr("p.ok", p);
	}

	/* 2. test unfixed maps */
	p_new = mremap(p, 4096, 8192, MREMAP_MAYMOVE);
	if (p_new != MAP_FAILED) {
		/* verify again */
		ksys_print_expr("p_new", p_new);
		memset(p_new, 0, 8192);
		ksys_print_expr("p_new.ok", p_new);
	}

	ksys_print_expr("free", p_new);

	/* 3. free up memory */
	if (p_new != MAP_FAILED) munmap(p_new, 4096);
	else if (p != MAP_FAILED) munmap(p, 4096);

	ksys_print_expr("free.ok", p_new);

	/* mapping should be invalid now; crash if not */
	if (ksys_is_valid_addr(p_new) || ksys_is_valid_addr(p))
		null_ptr[0] = 0;

	/*********/
	/* because fuck you, that's why. */
	/*********/
	m = malloc(1024*1024);
	ksys_print_expr("realloc", m);
	m2 = realloc(m, 1024*512);
	ksys_print_expr("realloc.ok", m2);

	*((char*)m2) = 0;
	free(m2);

	m = malloc(512*1024);
	ksys_print_expr("realloc2", m);
	m2 = realloc(m, 1024*1024);
	ksys_print_expr("realloc2.ok", m2);
	*((char*)m2) = 0;
	free(m2);

	return 0;
}