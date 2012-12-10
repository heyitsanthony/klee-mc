// RUN: gcc %s -O0 -o %t1
// RUN: rm -rf guest-tpp klee-out-tpp-ktest/  klee-out-tpp-paths/  klee-out-tpp-replay/
// RUN: ../../../scripts/save_guest.sh ./%t1 guest-tpp
// GET KTESTS
// RUN: klee-mc -use-cex-cache=false -pipe-solver -guest-type=sshot -guest-sshot=guest-tpp -use-sym-mmu -sym-mmu-type=forkall -verify-path -output-dir=klee-out-tpp-ktest - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-out-tpp-ktest | not grep "ptr.err"
// RUN: ls klee-out-tpp-ktest | grep ktest
//
// RUN: klee-mc -use-cex-cache=false -pipe-solver -only-replay -write-paths -guest-type=sshot -replay-suppress-forks=false -dump-states-on-halt=false -replay-ktest-dir=klee-out-tpp-ktest -verify-path -guest-sshot=guest-tpp -use-sym-mmu -sym-mmu-type=forkall -output-dir=klee-out-tpp-paths - ./%t1 2>%t1.err >%t1.out
//
// RUN: klee-mc -use-cex-cache=false -pipe-solver -only-replay -guest-type=sshot -replay-suppress-forks=false -dump-states-on-halt=false -replay-path-dir=klee-out-tpp-paths -verify-path -guest-sshot=guest-tpp -use-sym-mmu -sym-mmu-type=forkall -output-dir=klee-out-tpp-replay ./%t1 2>%t1.err >%t1.out
// RUN: rm -rf guest-replay klee-out-tpp-ktest klee-out-tpp-paths klee-out-tpp-replay

#include <unistd.h>

const char x[100] = {
0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
40, 41, 44, 43, 44, 45, 46, 47, 48, 49,
50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
90, 91, 92, 93, 94, 95, 96, 97, 98, 99 };

int main(void)
{
	int i;
	if (read(0, &i, sizeof(i)) != sizeof(i)) return 0;
	if (i < 0 || i > 20) return 0;

	if (x[i] & 1) return -x[i];

	return x[i];	
}