// RUN: gcc %s -O0 -o %t1
// RUN: rm -rf guest-ktmerge ktmerge-ktest/  ktmerge-paths/
// RUN: ../../../scripts/save_guest.sh ./%t1 guest-ktmerge
// GET KTESTS
// RUN: klee-mc -emit-all-errors -symargs -use-cex-cache=false -guest-type=sshot -guest-sshot=guest-ktmerge -output-dir=ktmerge-ktest -stop-after-n-tests=30 - ./%t1 2>%t1.0.err >%t1.0.out
// RUN: ls ktmerge-ktest | grep ktest
//
// MERGE KTESTS
// RUN: klee-mc -symargs -use-cex-cache=false -only-replay -replay-suppress-forks=false -replay-ktest-dir=ktmerge-ktest -ktest-merge-dir=ktmerge-merges -dump-states-on-halt=false -guest-type=sshot -guest-sshot=guest-ktmerge -output-dir=ktmerge-paths - ./%t1 2>%t1.1.err >%t1.1.out
// RUN: ls ktmerge-merges | grep ktest
// xxx rm -rf guest-replay ktmerge-ktest ktmerge-paths

#include <unistd.h>

int main(int argc, char* argv[])
{
	if (strcmp("abcdefg", argv[2]) < 0) {
		// support here
		return 2;
	}

	if (strcmp("abcdefg", argv[1]) < 0) {
		// support here
		return 0;
	}

	// support here: multiple suffixes that can have equal prefix

	if (open("abc", 0) < 0)
		return -1;
	*((char*)0x0) = 1;
	return 1;
}