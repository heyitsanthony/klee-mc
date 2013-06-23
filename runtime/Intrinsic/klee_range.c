//===-- klee_range.c ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <assert.h>
#include <klee/klee.h>

int64_t klee_range(int64_t start, int64_t end, const char* name)
{
	int64_t x;

	if (start >= end)
		klee_uerror("invalid range", "user");

	if (start+1==end)
		return start;

	klee_make_symbolic(&x, sizeof x, name); 

	if (start==0) {
		/* Make nicer constraint when simple... */
		klee_assume_ult(x, end);
	} else {
		klee_assume_sge(x, start);	/* x >= start */
		klee_assume_slt(x, end);	/* x < end */
	}

	return x;
}
