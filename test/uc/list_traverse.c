// RUN: %llvmgcc %s -I../../../include -emit-llvm -g -c -o %t1.bc
// RUN: %klee -sym-mmu-type=uc -emit-all-errors -pipe-solver %t1.bc  2>%t.err
// RUN: ls klee-last | grep empty.err | wc -l | grep 10
// RUN: ls klee-last | grep of.err | wc -l | grep 9
#include "klee/klee.h"
#include "list.h"

struct list l;

int main(void)
{
	struct list_item	*li;
	unsigned		i;

	klee_make_symbolic(&l, sizeof(l), "list");
	l.lst_offset = 16;

	list_for_all(&l, li) {
		char *dat = list_get_data(&l, li);

		dat[0] = 1;
		i++;
		if (i == 10)
			break;
	}

	return 0;
}